#include "PagedWorld.h"
#include "PagedRegion.h"
#include "TerrainPagingComponent.h"
#include "leveldb/filter_policy.h"
#include "leveldb/cache.h"
#include "Networking/Public/Common/TcpSocketBuilder.h"
#include "Networking/Public/Interfaces/IPv4/IPv4Address.h"
#include <PolyVox/MaterialDensityPair.h>
#include "MemoryReader.h"
#include "Kismet/GameplayStatics.h"
#include "ISavableWithRegion.h"
#include "ExtractionThreads.h"
#include "WorldGenThreads.h"

#ifdef WORLD_TICK_TRACKING
	DECLARE_CYCLE_STAT(TEXT("World Process New Regions"), STAT_WorldNewRegions, STATGROUP_VoxelWorld);
	DECLARE_CYCLE_STAT(TEXT("World Do Voxel Updates"), STAT_WorldVoxelUpdates, STATGROUP_VoxelWorld);
	DECLARE_CYCLE_STAT(TEXT("World Process Dirty Regions"), STAT_WorldDirtyRegions, STATGROUP_VoxelWorld);
	DECLARE_CYCLE_STAT(TEXT("World Clear Extraction Queue"), STAT_WorldClearExtractionQueue, STATGROUP_VoxelWorld);
#endif

APagedWorld::APagedWorld() {
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
}

APagedWorld::~APagedWorld() {}

void APagedWorld::BeginPlay() { Super::BeginPlay(); }

void APagedWorld::EndPlay(const EEndPlayReason::Type EndPlayReason) {

	if (bIsVoxelNetServer) {
		UE_LOG(LogTemp, Warning, TEXT("Stopping VoxelNet server..."));
		for (auto& elem : VoxelNetServer_ServerThreads) {
			if (elem)
				elem->Kill(true);
		}
		for (auto& elem : VoxelNetServer_VoxelServers) { elem.Reset(); }
		if (VoxelNetServer_ServerListener.IsValid())
			VoxelNetServer_ServerListener.Get()->Stop();
		VoxelNetServer_ServerListener.Reset();
		UE_LOG(LogTemp, Warning, TEXT("VoxelNet server has stopped."));
	}
	else if (!bIsVoxelNetSingleplayer){
		UE_LOG(LogTemp, Warning, TEXT("Stopping VoxelNet client..."));
		if (VoxelNetClient_ClientThread)
			VoxelNetClient_ClientThread->Kill(true);
		VoxelNetClient_VoxelClient.Reset();
		UE_LOG(LogTemp, Warning, TEXT("VoxelNet client has stopped."));
	}

	if (bIsVoxelNetServer || bIsVoxelNetSingleplayer)
	UE_LOG(LogTemp, Warning, TEXT("Saving and disconnecting world database..."));

	const auto beforeSave = FDateTime::UtcNow();

	PreSaveWorld();

	VolumeMutex.Lock();
	VoxelVolume.Reset();
	delete worldDB;
	VolumeMutex.Unlock();

	PostSaveWorld();

	const auto afterSave = FDateTime::UtcNow() - beforeSave;

	if (bIsVoxelNetServer || bIsVoxelNetSingleplayer)
	UE_LOG(LogTemp, Warning, TEXT("World database saved in %f ms."), afterSave.GetTotalMicroseconds()*0.001);
}

void APagedWorld::Tick(float DeltaTime) {
	Super::Tick(DeltaTime);
	// get any recently generated packets and put them in the queue
	if (bIsVoxelNetServer) {
		while (!VoxelNetServer_packetQueue.IsEmpty()) {
			FPacketTaskOutput output;
			VoxelNetServer_packetQueue.Dequeue(output);
			VoxelNetServer_regionPackets.Emplace(output.region, output.packet);
		}
	}
#ifdef WORLD_TICK_TRACKING
	{
		SCOPE_CYCLE_COUNTER(STAT_WorldNewRegions);
#endif
		VolumeMutex.Lock();
		// pop render queue
		// queue is multi input single consumer
		if (bIsVoxelNetServer || bIsVoxelNetSingleplayer) {
			// only the server / singleplayer generates world
			while (!worldGenerationQueue.IsEmpty()) {
				// doing x per tick reduces hitches by a good amount, but causes slower loading times
				FWorldGenerationTaskOutput gen;
				worldGenerationQueue.Dequeue(gen);

				for (int32 x = 0; x < REGION_SIZE; x++) {
					for (int32 y = 0; y < REGION_SIZE; y++) {
						for (int32 z = 0; z < REGION_SIZE; z++) {
							// 
							VoxelVolume->setVoxel(x + gen.pos.X, y + gen.pos.Y, z + gen.pos.Z, gen.voxel[x][y][z]);
						}
					}
				}
				remainingRegionsToGenerate--;
				MarkRegionDirtyAndAdjacent(gen.pos);
			}
		}

#ifdef WORLD_TICK_TRACKING
	}
	{
		SCOPE_CYCLE_COUNTER(STAT_WorldVoxelUpdates);
#endif
		// also voxelmodify queue
		while (!voxelUpdateQueue.IsEmpty()) {
			FVoxelUpdate update;
			voxelUpdateQueue.Dequeue(update);

			try {
				//lock
				for (int32 x = 0; x < update.radius; x++) {
					// todo evaluate performance
					for (int32 y = 0; y < update.radius; y++) {
						for (int32 z = 0; z < update.radius; z++) {
							const int32 n = update.radius / 2;
							const int32 nx = x - n;
							const int32 ny = y - n;
							const int32 nz = z - n;

							const auto oldMaterial = VoxelVolume->getVoxel(update.origin.X + nx, update.origin.Y + ny, update.origin.Z + nz).getMaterial();

							VoxelVolume->setVoxel(update.origin.X + nx, update.origin.Y + ny, update.origin.Z + nz, PolyVox::MaterialDensityPair88(update.material, update.density));
							MarkRegionDirtyAndAdjacent(VoxelToRegionCoords(FIntVector(update.origin.X + nx, update.origin.Y + ny, update.origin.Z + nz)));

							if (VoxelWorldUpdate_Event.IsBound()) { VoxelWorldUpdate_Event.Broadcast(update.causeActor, update.origin, oldMaterial, update.material); }
						}
					}
				}
			}
			catch (...) {
				UE_LOG(LogTemp, Error, TEXT("Caught exception in voxelUpdateQueue processing."));
				continue;
			}
		}
#ifdef WORLD_TICK_TRACKING
	}
	{
		SCOPE_CYCLE_COUNTER(STAT_WorldDirtyRegions);
#endif
		auto dirtyClone = dirtyRegions; // we dont want to include the following dirty regions til next time 
		dirtyRegions.Reset(); 
		if (!bIsVoxelNetServer) {
			if (VoxelNetClient_VoxelClient.IsValid()) {
				auto client = VoxelNetClient_VoxelClient.Get();

				while (!client->handshakes.IsEmpty()) {
					// while we have a reference to the client may as well process handshakes
					int64 cookie = 0;
					client->handshakes.Dequeue(cookie);
					if (VoxelNetHandshake_Event.IsBound()) { VoxelNetHandshake_Event.Broadcast(cookie); }
				}

				while (!client->downloadedRegions.IsEmpty()) {
					Packet::RegionData data;
					client->downloadedRegions.Dequeue(data);

					for (int x = 0; x < REGION_SIZE; x++) {
						for (int y = 0; y < REGION_SIZE; y++) {
							for (int z = 0; z < REGION_SIZE; z++) {
								VoxelVolume->setVoxel(x + data.x, y + data.y, z + data.z, PolyVox::MaterialDensityPair88(data.data[0][x][y][z], data.data[1][x][y][z]));
							}
						}
					}
					const auto pos = FIntVector(data.x, data.y, data.z);
					MarkRegionDirtyAndAdjacent(pos);
				}
			}
		}

		VolumeMutex.Unlock();

		for (auto& region : dirtyClone) {
			// if we render unloaded regions we get cascading world gen
			if (regions.Contains(region) || (!bIsVoxelNetServer && !bIsVoxelNetSingleplayer)) // if it is not in regions it will get discarded
				QueueRegionRender(region);
		}

#ifdef WORLD_TICK_TRACKING
	}
	{
		SCOPE_CYCLE_COUNTER(STAT_WorldClearExtractionQueue);
#endif
		TArray<FIntVector> VoxelNetServer_justCookedRegions;

		while (!extractionQueue.IsEmpty()) {
			FExtractionTaskOutput gen;
			extractionQueue.Dequeue(gen);
			auto reg = getRegionAt(gen.region);

			if (reg != nullptr) {
				reg->RenderParsed(gen);
				reg->UpdateNavigation();

				if (bIsVoxelNetServer) { VoxelNetServer_justCookedRegions.Add(gen.region); }
			}
			else { // 12/2 does this still happen? do i need to peek and not deque these cuz theyre pending repl?
				UE_LOG(LogTemp, Warning, TEXT("%d Tried to render null region %s."), bIsVoxelNetServer, *gen.region.ToString());
				//DrawDebugBox(this->GetWorld(), FVector(gen.region * 3200), FVector(1600), FColor::Emerald);
			}
		}

		if (bIsVoxelNetServer) {
			if (VoxelNetServer_justCookedRegions.Num() > 0) {
				for (auto& pager : pagingComponents) {
					TArray<TArray<uint8>> packets;

					for (auto& region : VoxelNetServer_justCookedRegions) {
						if (pager->waitingForPackets.Contains(region) && VoxelNetServer_regionPackets.Contains(region)) {
							packets.Add(VoxelNetServer_regionPackets.FindRef(region));
							pager->waitingForPackets.Remove(region);
						}
					}

					// send packets to the pager's owner
					if (packets.Num() > 0) {
						auto pagingPawn = Cast<APawn>(pager->GetOwner());
						if (pagingPawn != nullptr) {
							auto controller = Cast<APlayerController>(pagingPawn->GetController());
							if (controller != nullptr) {
								if (VoxelNetServer_PlayerVoxelServers.Contains(controller)) {
									auto server = VoxelNetServer_PlayerVoxelServers.Find(controller);
									server->Get()->UploadRegions(packets);
								}
								else { UE_LOG(LogTemp, Warning, TEXT("Server Paging Component Tick: VoxelNetServer_PlayerVoxelServers does not contain this controller.")); }
							}
							else { UE_LOG(LogTemp, Warning, TEXT("Server Paging Component Tick: Controller is not player controller.")); }
						}
						else { UE_LOG(LogTemp, Warning, TEXT("Server Paging Component Tick: Paging component owner is not pawn.")); }
					}
				}
			}
		}


#ifdef WORLD_TICK_TRACKING
	}
#endif
}

void APagedWorld::ConnectToDatabase(FString Name) {
	if (bIsVoxelNetServer || bIsVoxelNetSingleplayer) {
		leveldb::Options options;
		options.create_if_missing = true;

		//options.write_buffer_size = 120 * 1048576; // todo make configurable in BP?
		options.block_cache = leveldb::NewLRUCache(8 * 1048576);
		options.filter_policy = leveldb::NewBloomFilterPolicy(10);

		DatabaseName = Name;
		FString dbname = FPaths::ProjectSavedDir() + "World_" + DatabaseName;

		leveldb::Status status = leveldb::DB::Open(options, std::string(TCHAR_TO_UTF8(*dbname)), &worldDB);
		
		UE_LOG(LogTemp, Warning, TEXT("Database connection to %s: %s"), *dbname, status.ok() ? TEXT("Success") :  TEXT("Failure"));
		ensure(status.ok());

		TArray<uint8> versionArchive;
		if (LoadGlobalDataFromDatabase(worldDB, DB_VERSION_TAG, versionArchive)) {
			FMemoryReader versionReader(versionArchive);
			int32 db_version;
			versionReader << db_version; // read version if one existed
			versionReader.FlushCache();
			versionReader.Close();
			UE_LOG(LogTemp, Warning, TEXT("Database version for %s: %d. Compatible? %s."), *dbname, db_version, db_version==DB_VERSION? TEXT("Yes") : TEXT("No"));
			assert(db_version == DB_VERSION);
		}
		else {
			FBufferArchive version;
			int32 dbVersion = DB_VERSION;
			version << dbVersion;
			SaveGlobalDataToDatabase(worldDB, DB_VERSION_TAG, version);
		}
	}
}

void APagedWorld::PostInitializeComponents() {
	VoxelVolume = MakeShareable(new PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>(new WorldPager(this),256*1024*1024,REGION_SIZE));
	Super::PostInitializeComponents();
}

void APagedWorld::RegisterPagingComponent(UTerrainPagingComponent* pagingComponent) { pagingComponents.AddUnique(pagingComponent); }

APagedRegion* APagedWorld::getRegionAt(FIntVector pos) {
	if (regions.Contains(pos))
		return regions.FindRef(pos);

	if (!bIsVoxelNetServer && !bIsVoxelNetSingleplayer) {
		UE_LOG(LogTemp, Verbose, TEXT("Client tried to access nonexistant region %s."), *pos.ToString());
		return nullptr;
	}

	const FVector fpos = FVector(pos * VOXEL_SIZE);

	try {
		APagedRegion* region = GetWorld()->SpawnActorDeferred<APagedRegion>(APagedRegion::StaticClass(), FTransform(FRotator::ZeroRotator), GetOwner());
		region->World = this;
		UGameplayStatics::FinishSpawningActor(region, FTransform(FRotator::ZeroRotator, fpos));

		regions.Add(pos, region);

		MarkRegionDirtyAndAdjacent(pos);
		return region;
	}
	catch (...) {
		UE_LOG(LogTemp, Warning, TEXT("Exception spawning a new region actor at %s."), *pos.ToString());
		return nullptr;
	}
}

void APagedWorld::QueueRegionRender(FIntVector pos) {
	if(bRenderMarchingCubes){
		(new FAutoDeleteAsyncTask<ExtractionThreads::MarchingCubesExtractionTask>(this, pos))->StartBackgroundTask();
	} else {
		(new FAutoDeleteAsyncTask<ExtractionThreads::CubicExtractionTask>(this, pos))->StartBackgroundTask();
	}
}

void APagedWorld::MarkRegionDirtyAndAdjacent(FIntVector pos) {
	dirtyRegions.Emplace(pos);
	dirtyRegions.Emplace(pos + FIntVector(REGION_SIZE, 0, 0));
	dirtyRegions.Emplace(pos + FIntVector(0, REGION_SIZE, 0));
	dirtyRegions.Emplace(pos + FIntVector(0, 0, REGION_SIZE));
	dirtyRegions.Emplace(pos + FIntVector(-REGION_SIZE, 0, 0));
	dirtyRegions.Emplace(pos + FIntVector(0, -REGION_SIZE, 0));
	dirtyRegions.Emplace(pos + FIntVector(0, 0, -REGION_SIZE));
}


void APagedWorld::PrefetchRegionsInRadius(FIntVector pos, int32 radius) const {
	auto reg = PolyVox::Region(pos.X, pos.Y, pos.Z, pos.X + REGION_SIZE, pos.Y + REGION_SIZE, pos.Z + REGION_SIZE);
	reg.grow(radius * REGION_SIZE);
	VoxelVolume.Get()->prefetch(reg);
}

bool APagedWorld::ModifyVoxel(FIntVector VoxelLocation, uint8 Radius, uint8 Material, uint8 Density, AActor* cause, bool bIsSpherical) {
	voxelUpdateQueue.Enqueue(FVoxelUpdate(VoxelLocation, Radius, Material, Density, cause, bIsSpherical));
	return true;
}

FIntVector APagedWorld::VoxelToRegionCoords(FIntVector VoxelCoords) {
	const FVector tmp = FVector(VoxelCoords) / (float)REGION_SIZE;
	return FIntVector(FMath::FloorToInt(tmp.X), FMath::FloorToInt(tmp.Y), FMath::FloorToInt(tmp.Z)) * 32;
}

FIntVector APagedWorld::WorldToVoxelCoords(FVector WorldCoords) { return FIntVector(WorldCoords / VOXEL_SIZE); }

FVector APagedWorld::VoxelToWorldCoords(FIntVector VoxelCoords) { return FVector(VoxelCoords * VOXEL_SIZE); }

void APagedWorld::BeginWorldGeneration(FIntVector RegionCoords) {
	if (bIsVoxelNetServer || bIsVoxelNetSingleplayer) {
		remainingRegionsToGenerate++;
		(new FAutoDeleteAsyncTask<WorldGenThreads::RegionGenerationTask>(this, RegionCoords))->StartBackgroundTask();
	}
}

int32 APagedWorld::getVolumeMemoryBytes() const { return VoxelVolume.Get()->calculateSizeInBytes(); }

void APagedWorld::Flush() const { VoxelVolume.Get()->flushAll(); }

void APagedWorld::ForceSaveWorld() {
	PreSaveWorld();
	VolumeMutex.Lock();
	Flush();
	VolumeMutex.Unlock();
	PostSaveWorld();
}

void APagedWorld::PagingComponentTick() {
	if (!bIsVoxelNetServer && !bIsVoxelNetSingleplayer)
		return;

	TSet<FIntVector> regionsToLoad;
	TSet<UTerrainPagingComponent*> toRemove;

	for (auto& pager : pagingComponents) {
		if (!IsValid(pager)) { toRemove.Add(pager); } // fixed concurrent modification 8/24/19
		else {
			auto previousSubscribedRegions = pager->subscribedRegions;
			pager->subscribedRegions.Reset();

			int radius = pager->viewDistance;
			FIntVector pos = VoxelToRegionCoords(WorldToVoxelCoords(pager->GetOwner()->GetActorLocation()));

			for (int z = -radius; z <= radius; z++) {// top down makes it feel faster
				for (int y = -radius; y <= radius; y++) {
					for (int x = -radius; x <= radius; x++) {
						FIntVector surrounding = pos + FIntVector(REGION_SIZE * x, REGION_SIZE * y, -REGION_SIZE * z);
						pager->subscribedRegions.Emplace(surrounding);
						regionsToLoad.Emplace(surrounding);
					}
				}
			}
			if (bIsVoxelNetServer) {
				auto toUpload = pager->subscribedRegions.Difference(previousSubscribedRegions); // to load

				TArray<TArray<uint8>> packets;
				for (auto& uploadRegion : toUpload) {
					if (VoxelNetServer_regionPackets.Contains(uploadRegion))
						packets.Add(VoxelNetServer_regionPackets.FindRef(uploadRegion));
						// this is before the regions even get loaded on the server. i need to, in extraction results, check if anyone is subbed
					else {
						pager->waitingForPackets.Add(uploadRegion);
					}
				}

				if (packets.Num() > 0) {
					auto pagingPawn = Cast<APawn>(pager->GetOwner());
					if (pagingPawn != nullptr) {
						auto controller = Cast<APlayerController>(pagingPawn->GetController());
						if (controller != nullptr) {
							if (VoxelNetServer_PlayerVoxelServers.Contains(controller)) {
								auto server = VoxelNetServer_PlayerVoxelServers.Find(controller);
								server->Get()->UploadRegions(packets);
							}
							else { UE_LOG(LogTemp, Warning, TEXT("Server Paging Component Tick: VoxelNetServer_PlayerVoxelServers does not contain this controller.")); }
						}
						else { UE_LOG(LogTemp, Warning, TEXT("Server Paging Component Tick: Controller is not player controller.")); }
					}
					else { UE_LOG(LogTemp, Warning, TEXT("Server Paging Component Tick: Paging component owner is not pawn.")); }
				}
			}
		}
	}

	for (auto& elem : toRemove) { pagingComponents.Remove(elem); }
	toRemove.Reset();

	UnloadRegionsExcept(regionsToLoad);
}

void APagedWorld::UnloadRegionsExcept(TSet<FIntVector> regionsToLoad) {
	const TArray<FIntVector> currentRegionsArr;
	const TSet<FIntVector> currentRegions(currentRegionsArr);

	auto toUnload = currentRegions.Difference(regionsToLoad); // to unload
	auto toLoad = regionsToLoad.Difference(currentRegions); // to load

	for (auto& unload : toUnload) { regions.FindAndRemoveChecked(unload)->SetLifeSpan(.1); }

	for (auto& load : toLoad) {
		if (!regions.Contains(load)) {
			getRegionAt(load);
			MarkRegionDirtyAndAdjacent(load);
		}
	}
}

void APagedWorld::RegisterPlayerWithCookie(APlayerController* player, int64 cookie) {
	if (bIsVoxelNetServer) {
		if (VoxelNetServer_SentHandshakes.Contains(cookie)) {
			const auto handshake = VoxelNetServer_SentHandshakes.FindRef(cookie);
			VoxelNetServer_SentHandshakes.Remove(cookie);

			if (handshake.IsValid() && Role == ROLE_Authority) {
				VoxelNetServer_PlayerVoxelServers.Add(player, handshake);
				UE_LOG(LogTemp, Warning, TEXT("Registered player controller with cookie %llu."), cookie);
			}
			else { UE_LOG(LogTemp, Warning, TEXT("There was no valid server for the cookie %llu."), cookie); }
		}
		else { UE_LOG(LogTemp, Warning, TEXT("There was no handshake sent with the cookie %llu."), cookie); }
	}
}


WorldPager::WorldPager(APagedWorld* World)
	: world(World) {
}

void WorldPager::pageIn(const PolyVox::Region& region, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk* pChunk) {
	if (world->bIsVoxelNetServer || world->bIsVoxelNetSingleplayer) {
		const auto pos = FIntVector(region.getLowerX(), region.getLowerY(), region.getLowerZ());
		const auto bRegionExists = world->ReadChunkFromDatabase(world->worldDB, pos, pChunk);
		if (!bRegionExists) { world->BeginWorldGeneration(pos); }
	}
	return;
}


void WorldPager::pageOut(const PolyVox::Region& region, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk* pChunk) {
	if (world->bIsVoxelNetServer || world->bIsVoxelNetSingleplayer) {
		const FIntVector pos = FIntVector(region.getLowerX(), region.getLowerY(), region.getLowerZ());

		// get savablewithregion actors in this region
		TArray<AActor*> outActors;
		UGameplayStatics::GetAllActorsWithInterface(this->world, UISavableWithRegion::StaticClass(), outActors);

		for (auto& elem : outActors) {
			// todo have savables register themselves 
			const FTransform Transform = IISavableWithRegion::Execute_GetSaveTransform(elem);
			const FIntVector saveRegion = world->VoxelToRegionCoords(world->WorldToVoxelCoords(Transform.GetLocation()));
			if (saveRegion == pos) {
				// todo how to save these

			}
		}

#ifndef DONT_SAVE
		world->SaveChunkToDatabase(world->worldDB, pos, pChunk);
#endif
		//UE_LOG(LogTemp, Warning, TEXT("[db] Saved region to db:  %s."), *pos.ToString());
	}
}

std::string ArchiveToString(TArray<uint8>& archive) {
	const auto out = std::string((char*)archive.GetData(), archive.Num());
	return out;
}

void ArchiveFromString(std::string input, TArray<uint8>& archive) {
	const int len = input.length();
	if (len <= 0)
		return;

	if (archive.Num() > 0)
		archive.Empty(len);
	archive.AddZeroed(len);

	for (int i = 0; i < len; i++) { archive[i] = (unsigned char)input[i]; }
}

std::string SerializeLocationString(int32_t X, int32_t Y, int32_t Z, uint8 W) {
	FBufferArchive tempBuffer(true);
	tempBuffer << X;
	tempBuffer << Y;
	tempBuffer << Z;
	tempBuffer << W;

	return ArchiveToString(tempBuffer);
}

void APagedWorld::SaveChunkToDatabase(leveldb::DB* db, FIntVector pos, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk* pChunk) {
	// todo make batched write
	for (char w = 0; w < REGION_SIZE; w++) {
		// for each x,y layer
		char byteBuf[2 * REGION_SIZE * REGION_SIZE]; // 2kb for 32^2, material and density 1 byte each

		int n = 0; // this could be better if we were to calculate it from xy so it is independent of order?
		// x + (y*REGION_SIZE)
		
		for (char x = 0; x < REGION_SIZE; x++) {
			for (char y = 0; y < REGION_SIZE; y++) {
				auto uVoxel = pChunk->getVoxel(x, y, w);

				char mat = uVoxel.getMaterial(); // unsigned -> signed conversion
				char den = uVoxel.getDensity();

				byteBuf[n++] = mat;
				byteBuf[n++] = den;
			}
		}

		db->Put(leveldb::WriteOptions(), SerializeLocationString(pos.X, pos.Y, pos.Z, w), std::string(byteBuf, 2 * REGION_SIZE * REGION_SIZE));
	}
}

bool APagedWorld::ReadChunkFromDatabase(leveldb::DB* db, FIntVector pos, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk* pChunk) {
	bool containsNonZero = false;

	for (char w = 0; w < REGION_SIZE; w++) {
		std::string chunkData;
		auto status = db->Get(leveldb::ReadOptions(), SerializeLocationString(pos.X, pos.Y, pos.Z, w), &chunkData);

		if (status.IsNotFound()) {
			if (w > 0)
			UE_LOG(LogTemp, Warning, TEXT("Loading failed partway through %s region : failed at layer %d. Region data unrecoverable."), *pos.ToString(), w);
			return false;
		}

		int n = 0; // this could be better if we were to calculate it from xy so it is independent of order

		for (char x = 0; x < REGION_SIZE; x++) {
			for (char y = 0; y < REGION_SIZE; y++) {
				unsigned char mat = chunkData[n++]; // signed - > unsigned conversion
				unsigned char den = chunkData[n++];

				if (mat != 0)
					containsNonZero = true;
				pChunk->setVoxel(x, y, w, PolyVox::MaterialDensityPair88(mat, den));
			}
		}
	}
#ifdef REGEN_NULL_REGIONS
	return containsNonZero;
#else REGEN_NULL_REGIONS
	return true;
#endif

}

void APagedWorld::SaveRegionalDataToDatabase(leveldb::DB* db, FIntVector pos, uint8 index, TArray<uint8>& archive) {
	db->Put(leveldb::WriteOptions(), SerializeLocationString(pos.X, pos.Y, pos.Z, index + REGION_SIZE), ArchiveToString(archive));
}

bool APagedWorld::LoadRegionalDataFromDatabase(leveldb::DB* db, FIntVector pos, uint8 index, TArray<uint8>& archive) {
	std::string data;
	auto status = db->Get(leveldb::ReadOptions(), SerializeLocationString(pos.X, pos.Y, pos.Z, index + REGION_SIZE), &data);

	if (status.IsNotFound())
		return false;

	ArchiveFromString(data, archive);
	return true;
}

void APagedWorld::SaveGlobalDataToDatabase(leveldb::DB* db, std::string key, TArray<uint8>& archive) { db->Put(leveldb::WriteOptions(), DB_GLOBAL_TAG + key, ArchiveToString(archive)); }

bool APagedWorld::LoadGlobalDataFromDatabase(leveldb::DB* db, std::string key, TArray<uint8>& archive) {
	std::string data;
	auto status = db->Get(leveldb::ReadOptions(), DB_GLOBAL_TAG + key, &data);

	if (status.IsNotFound())
		return false;

	ArchiveFromString(data, archive);
	return true;
}

void APagedWorld::TempSaveTransformToGlobal(FString key, FTransform value) {
	FBufferArchive a;
	a << value;
	SaveGlobalDataToDatabase(worldDB, std::string(TCHAR_TO_UTF8(*key)), a);
}

FTransform APagedWorld::TempLoadTransformToGlobal(FString key) {
	TArray<uint8> a;

	if (!LoadGlobalDataFromDatabase(worldDB, std::string(TCHAR_TO_UTF8(*key)), a))
		return FTransform::Identity;

	FMemoryReader reader(a);

	FTransform out;
	reader << out;
	reader.FlushCache();
	reader.Close();

	return out;
}

void APagedWorld::SaveStringToGlobal(FString s) {
	FBufferArchive a;

	a << s;

	SaveGlobalDataToDatabase(worldDB, "globalstring", a);
}

FString APagedWorld::LoadStringFromGlobal() {
	TArray<uint8> a;

	if (!LoadGlobalDataFromDatabase(worldDB, "globalstring", a))
		return "no such string";

	FMemoryReader reader(a);

	FString out;
	reader << out;
	reader.FlushCache();
	reader.Close();

	return out;
}


bool APagedWorld::VoxelNetServer_StartServer() {
	if (bIsVoxelNetServer) {
		const FIPv4Endpoint Endpoint(FIPv4Address::Any,VOXELNET_PORT); //todo BindNextPort and communicate it to clients ingame on join
		VoxelNetServer_ServerListener = MakeShareable(new FTcpListener(Endpoint));
		auto listener = VoxelNetServer_ServerListener.Get();
		listener->OnConnectionAccepted().BindUObject(this, &APagedWorld::VoxelNetServer_OnConnectionAccepted);
		return listener->Init() && listener->OnConnectionAccepted().IsBound() && listener->IsActive();
	}
	return false;
}


bool APagedWorld::VoxelNetServer_OnConnectionAccepted(FSocket* socket, const FIPv4Endpoint& endpoint) {
	if (bIsVoxelNetServer) {
		int64 cookie = FMath::RandRange(INT64_MIN,INT64_MAX);

		UE_LOG(LogTemp, Warning, TEXT("Connection received from %s, starting thread..."), *endpoint.ToString());
		auto server = MakeShareable(new VoxelNetThreads::VoxelNetServer(cookie, socket, endpoint));

		FString threadName = "VoxelNetServer_";
		threadName.Append(endpoint.ToString());

		VoxelNetServer_ServerThreads.Add(FRunnableThread::Create(VoxelNetServer_VoxelServers.Add_GetRef(server).Get(), *threadName));
		VoxelNetServer_SentHandshakes.Add(cookie, server);
		return true;
	}
	return false;
}


bool APagedWorld::VoxelNetClient_ConnectToServer(FString Host, int32 Port) {
	if (!bIsVoxelNetServer) {
		FSocket* clientSocket = FTcpSocketBuilder("VoxelNetClient").AsReusable().WithReceiveBufferSize(64 * 1024 * 1024).Build(); // todo 
		uint32 resolvedIP = 0;
		TSharedPtr<FInternetAddr> addr;
		// resolve hostname
		const bool wasCached = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetHostByNameFromCache(TCHAR_TO_ANSI(*Host), addr);

		if (!wasCached) {
			auto ResolveInfo = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetHostByName(TCHAR_TO_ANSI(*Host));
			while (!ResolveInfo->IsComplete());
			if (ResolveInfo->GetErrorCode() == 0) {
				const FInternetAddr* add = &ResolveInfo->GetResolvedAddress();
				add->GetIp(resolvedIP);
			}

			if (resolvedIP == 0) {
				FIPv4Address ip;
				FIPv4Address::Parse(Host, ip);

				addr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
				addr->SetIp(ip.Value);
			}
			else {
				addr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
				addr->SetIp(resolvedIP);
			}
		}

		addr->SetPort(Port > 0 ? Port : VOXELNET_PORT);

		if (clientSocket->Connect(*addr)) {
			UE_LOG(LogTemp, Warning, TEXT("Client: Connecting to server %s..."), *addr->ToString(true));
			VoxelNetClient_VoxelClient = MakeShareable(new VoxelNetThreads::VoxelNetClient(this, clientSocket));
			VoxelNetClient_ClientThread = FRunnableThread::Create(VoxelNetClient_VoxelClient.Get(), TEXT("VoxelNetClient"));
		}
		else { UE_LOG(LogTemp, Warning, TEXT("Client: Failed to connect to server.")); }
		return true;
	}
	return false;
}

int32 APagedWorld::VoxelNetClient_GetPendingRegionDownloads() const {
	// todo this is not thread safe
	return VoxelNetClient_VoxelClient.Get()->remainingRegionsToDownload;
}
