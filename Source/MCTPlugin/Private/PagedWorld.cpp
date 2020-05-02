#include "PagedWorld.h"
#include "PagedRegion.h"
#include "TerrainPagingComponent.h"
#include "leveldb/filter_policy.h"
#include "leveldb/cache.h"
#include "Networking/Public/Common/TcpSocketBuilder.h"
#include "Networking/Public/Interfaces/IPv4/IPv4Address.h"
#include <PolyVox/MaterialDensityPair.h>
#include "Kismet/GameplayStatics.h"
#include "ISavableWithRegion.h"
#include "ExtractionThreads.h"
#include "WorldGenThreads.h"
#include "StorageProviderBase.h"
#include "StorageProviderLevelDB.h"
#include "Async/Async.h"
#include "WorldGeneratorPlains.h"
#include "StorageProviderNull.h"

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

APagedWorld::~APagedWorld() {
}

void APagedWorld::BeginPlay() {
	Super::BeginPlay();
	VoxelWorldThreadPool = FQueuedThreadPool::Allocate();

	const int32 NUMBER_OF_POOLS = 1;
	const int32 NumCoresPerPool = bShareCores ? FPlatformMisc::NumberOfCoresIncludingHyperthreads() : FPlatformMisc::NumberOfCoresIncludingHyperthreads() / NUMBER_OF_POOLS;
	
	VoxelWorldThreadPool->Create(NumCoresPerPool, 256 * 1024); // thread pool for extraction and worldgen tasks
	
	//URuntimeMesh::InitializeMultiThreading(NumCoresPerPool); // thread pool for RMC //// currently runs stuff that throws a check (IsInGameThread()) so useless

}

void APagedWorld::EndPlay(const EEndPlayReason::Type EndPlayReason) {
	SaveAndShutdown();
}

void APagedWorld::VoxelUpdatesTick() {
	while (!voxelUpdateQueue.IsEmpty()) {
		FVoxelUpdate update;
		voxelUpdateQueue.Dequeue(update);

		try {
			for (int32 x = 0; x < update.radius; x++) {
				for (int32 y = 0; y < update.radius; y++) {
					for (int32 z = 0; z < update.radius; z++) {
						const int32 n = update.radius / 2;
						const int32 nx = x - n;
						const int32 ny = y - n;
						const int32 nz = z - n;

						const auto oldMaterial = VoxelVolume->getVoxel(update.origin.X + nx, update.origin.Y + ny, update.origin.Z + nz).getMaterial();

						VoxelVolume->setVoxel(update.origin.X + nx, update.origin.Y + ny, update.origin.Z + nz, PolyVox::MaterialDensityPair88(update.material, update.density));
						MarkRegionDirtyAndAdjacent(VoxelToRegionCoords(FIntVector(update.origin.X + nx, update.origin.Y + ny, update.origin.Z + nz)));

						if (VoxelWorldUpdate_Event.IsBound()) { VoxelWorldUpdate_Event.Broadcast(update.causeActor, update.origin, oldMaterial, update.material, update.bShouldDrop); }
					}
				}
			}
		}
		catch (...) {
			UE_LOG(LogTemp, Error, TEXT("Caught exception in voxelUpdateQueue processing."));
			continue;
		}
	}
}

void APagedWorld::WorldNewRegionsTick() {
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
			if(RegionGenerated_Event.IsBound()) RegionGenerated_Event.Broadcast(gen.pos);
		}
	}
}

void APagedWorld::VoxelNetClientTick() {
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
			MarkRegionDirtyAndAdjacent(FIntVector(data.x, data.y, data.z));
		}
	}
}

void APagedWorld::VoxelNetServerTick() {
	while (!VoxelNetServer_packetQueue.IsEmpty()) {
		FPacketTaskOutput output;
		VoxelNetServer_packetQueue.Dequeue(output);
		VoxelNetServer_regionPackets.Emplace(output.region, output.packet);
	}
}

void APagedWorld::Tick(float DeltaTime) {
	Super::Tick(DeltaTime);
	// get any recently generated packets and put them in the queue
	if (bIsVoxelNetServer) { VoxelNetServerTick(); }
#ifdef WORLD_TICK_TRACKING
	{
		SCOPE_CYCLE_COUNTER(STAT_WorldNewRegions);
#endif
		VolumeMutex.Lock();

		WorldNewRegionsTick();

#ifdef WORLD_TICK_TRACKING
	}
	{
		SCOPE_CYCLE_COUNTER(STAT_WorldVoxelUpdates);
#endif
		// also voxelmodify queue
		VoxelUpdatesTick();
#ifdef WORLD_TICK_TRACKING
	}
	{
		SCOPE_CYCLE_COUNTER(STAT_WorldDirtyRegions);
#endif
		auto dirtyClone = dirtyRegions; // we dont want to include the following dirty regions til next time
		dirtyRegions.Reset();

		if (!bIsVoxelNetServer) { VoxelNetClientTick(); }

		VolumeMutex.Unlock();

		if ((PagingComponentTickTimer += DeltaTime) >= PagingComponentTickRate) {
			PagingComponentTickTimer = 0.f;
			PagingComponentTick();
		}

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
		//TArray<FIntVector> VoxelNetServer_justCookedRegions;

		while (!extractionQueue.IsEmpty()) {
			FExtractionTaskOutput gen;
			extractionQueue.Dequeue(gen);
			auto reg = getRegionAt(gen.region);

			if (reg != nullptr) {
				reg->RenderParsed(gen);
				reg->UpdateNavigation();

				//if (bIsVoxelNetServer) { VoxelNetServer_justCookedRegions.Add(gen.region); }
			}
			else { OnRegionError(gen.region); }
		}

		if (bIsVoxelNetServer) {
			for (auto& pager : pagingComponents) {
				TArray<TArray<uint8>> packets;
				TSet<FIntVector> hits;

				TArray<FIntVector> cachedPackets;
				VoxelNetServer_regionPackets.GetKeys(cachedPackets);

				TSet<FIntVector> cacheSet = TSet<FIntVector>(cachedPackets);

				for (auto& waitingFor : pager->waitingForPackets.Intersect(cacheSet)) {
					// where waitingFor and the cache intersect send packets
					packets.Add(VoxelNetServer_regionPackets.FindRef(waitingFor));
					hits.Emplace(waitingFor);
				}

				// send packets to the pager's owner
				if (VoxelNetServer_SendPacketsToPagingComponent(pager, packets)) {
					// this is the typical send that most packets go thru
					for (auto& hit : hits) { pager->waitingForPackets.Remove(hit); }
					pager->bIsConnectedToVoxelnet = true;
					pager->OnSentRegionPacket(hits.Num());
				}
				else { pager->bIsConnectedToVoxelnet = false; }
			}
		}


#ifdef WORLD_TICK_TRACKING
	}
#endif
}

void APagedWorld::ConnectToDatabase(FString Name) {
	if (bIsVoxelNetServer || bIsVoxelNetSingleplayer) {
		bHasStarted = true;
		//WorldStorageProvider = new StorageProviderLevelDB(true);
		//WorldStorageProvider = new StorageProviderFlatfile();
		//WorldStorageProvider = new StorageProviderTMap(true);
		WorldStorageProvider = new StorageProviderNull();

		auto status = WorldStorageProvider->Open(TCHAR_TO_UTF8(*Name), true);

		UE_LOG(LogTemp, Warning, TEXT("Database connection to %hs using provider %hs: %s"), WorldStorageProvider->GetDatabasePath(TCHAR_TO_UTF8(*Name)).c_str(),
		       WorldStorageProvider->GetProviderName(), status ? TEXT("Success") : TEXT("Failure"));

		const auto compatible = WorldStorageProvider->VerifyDatabaseFormat(DB_VERSION);

		UE_LOG(LogTemp, Warning, TEXT("Database version for %s: Compatible? %s."), *Name, compatible? TEXT("Yes") : TEXT("No"));
		ensure(compatible);
	}
}

void APagedWorld::PostInitializeComponents() {
	VoxelVolume = MakeShareable(new PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>(new WorldPager(this), 256 * 1024 * 1024,REGION_SIZE));
	//VoxelVolume = MakeShareable(new PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>(new WorldPager(this), 2 * 1024 * 1024 * 1024,REGION_SIZE));
	Super::PostInitializeComponents();
}

void APagedWorld::BeginDestroy() {
	Super::BeginDestroy();
	SaveAndShutdown();
}

void APagedWorld::SaveAndShutdown() {
	if(bHasShutdown || !bHasStarted) return;
	bHasShutdown = true;
	VoxelWorldThreadPool->Destroy();
	
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
	else if (!bIsVoxelNetSingleplayer) {
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

	if (bIsVoxelNetServer || bIsVoxelNetSingleplayer){
		UE_LOG(LogTemp, Warning, TEXT("Saving data for regions..."));
		UnloadRegionsExcept(TSet<FIntVector>()); // force save all entities
		UE_LOG(LogTemp, Warning, TEXT("Regional data saved."));
	}
	
	VolumeMutex.Lock();
	VoxelVolume.Reset();

	if (bIsVoxelNetSingleplayer || bIsVoxelNetServer)
		WorldStorageProvider->Close();

	VolumeMutex.Unlock();

	PostSaveWorld();

	const auto afterSave = FDateTime::UtcNow() - beforeSave;

	if (bIsVoxelNetServer || bIsVoxelNetSingleplayer)
	UE_LOG(LogTemp, Warning, TEXT("World database saved in %f ms."), afterSave.GetTotalMicroseconds()*0.001);
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
	if (bRenderMarchingCubes) { (new FAutoDeleteAsyncTask<ExtractionThreads::MarchingCubesExtractionTask>(this, pos))->StartBackgroundTask(VoxelWorldThreadPool); }
	else { (new FAutoDeleteAsyncTask<ExtractionThreads::CubicExtractionTask>(this, pos))->StartBackgroundTask(VoxelWorldThreadPool); }
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


FIntVector APagedWorld::VoxelToRegionCoords(FIntVector VoxelCoords) {
	const FVector tmp = FVector(VoxelCoords) / (float)REGION_SIZE;
	return FIntVector(FMath::FloorToInt(tmp.X), FMath::FloorToInt(tmp.Y), FMath::FloorToInt(tmp.Z)) * 32;
}

FIntVector APagedWorld::WorldToVoxelCoords(FVector WorldCoords) { return FIntVector(WorldCoords / VOXEL_SIZE); }

FVector APagedWorld::VoxelToWorldCoords(FIntVector VoxelCoords) { return FVector(VoxelCoords * VOXEL_SIZE); }

bool APagedWorld::Server_ModifyVoxel_Validate(FIntVector VoxelLocation, uint8 Radius, uint8 Material, uint8 Density, AActor* cause, bool bIsSpherical, bool bShouldDrop) {
	//cause is valid and cause is less than x away , maybe make one without radius because thats usually server side 
	return true;
}

void APagedWorld::Server_ModifyVoxel_Implementation(FIntVector VoxelLocation, uint8 Radius, uint8 Material, uint8 Density, AActor* cause, bool bIsSpherical, bool bShouldDrop) {
	Multi_ModifyVoxel(VoxelLocation, Radius, Material, Density, cause, bIsSpherical, bShouldDrop);
}

void APagedWorld::Multi_ModifyVoxel_Implementation(FIntVector VoxelLocation, uint8 Radius, uint8 Material, uint8 Density, AActor* cause, bool bIsSpherical, bool bShouldDrop) {
	voxelUpdateQueue.Enqueue(FVoxelUpdate(VoxelLocation, Radius, Material, Density, cause, bIsSpherical, bShouldDrop));
}

void APagedWorld::BeginWorldGeneration(FIntVector RegionCoords) {
	if (bIsVoxelNetServer || bIsVoxelNetSingleplayer) {
		remainingRegionsToGenerate++;
		(new FAutoDeleteAsyncTask<WorldGenThreads::RegionGenerationTask>(this, RegionCoords))->StartBackgroundTask(VoxelWorldThreadPool);
	}
}

float APagedWorld::GetHeightmapZ(int32 VoxelX, int32 VoxelY, uint8 HeightmapIndex) {
	return GetNoiseGeneratorArray()[HeightmapIndex]->GetNoise2D(VoxelX, VoxelY);
}

int32 APagedWorld::getVolumeMemoryBytes() const { return VoxelVolume.Get()->calculateSizeInBytes(); }

void APagedWorld::Flush() const { VoxelVolume.Get()->flushAll(); }

void APagedWorld::LaunchServer() {
	bIsVoxelNetServer = true;
	ConnectToDatabase("WorldDatabase");
	VoxelNetServer_StartServer();
}

void APagedWorld::LaunchSingleplayer() {
	bIsVoxelNetSingleplayer = true;
	ConnectToDatabase("WorldDatabase");
}

void APagedWorld::LaunchClient(FString Host, int32 Port) {
	VoxelNetClient_ConnectToServer(Host, Port);
}

void APagedWorld::ForceSaveWorld() {
	PreSaveWorld();
	VolumeMutex.Lock();
	Flush();
	VolumeMutex.Unlock();
	PostSaveWorld();
}

void APagedWorld::SaveAllDataForRegions(TSet<FIntVector> Regions) {
	TArray<AActor*> outActors;
	UGameplayStatics::GetAllActorsWithInterface(this, UISavableWithRegion::StaticClass(), outActors);

	for (auto& unload : Regions) {
		TArray<AActor*> saved;
		TArray<FVoxelWorldActorRecord> regionActorRecords;
		for (auto& elem : outActors) {
			const FTransform Transform = IISavableWithRegion::Execute_GetSaveTransform(elem);
			const FIntVector saveRegion = VoxelToRegionCoords(WorldToVoxelCoords(Transform.GetLocation()));
			if (saveRegion == unload) {
				IISavableWithRegion::Execute_OnPreSave(elem);
				FVoxelWorldActorRecord record;

				record.ActorClass = elem->GetClass()->GetPathName();
				record.ActorTransform = Transform;

				FMemoryWriter writer(record.ActorData, true);
				FVoxelWorldSaveGameArchive proxy(writer);

				elem->Serialize(proxy);

				proxy.Flush();
				proxy.Close();
				writer.Flush();
				writer.Close();

				regionActorRecords.Add(record);
				saved.Add(elem);

				IISavableWithRegion::Execute_OnSaved(elem);
				elem->Destroy();
			}
		}
		outActors.RemoveAll([saved](AActor* a) { return saved.Contains(a); });

		FBufferArchive region_actors(true);
		region_actors << regionActorRecords;

		WorldStorageProvider->PutRegionalData(unload, 0, region_actors);

		if (regionActorRecords.Num() > 0)
		UE_LOG(LogTemp, Warning, TEXT("[db] Saved region %s %d B data: %s."), *unload.ToString(),region_actors.Num(), *BytesToHex(region_actors.GetData(),region_actors.Num()));
	}
}

void APagedWorld::LoadAllDataForRegions(TSet<FIntVector> Regions) {
	// load actors
	for (auto& load : Regions) {
		TArray<uint8> region_actors;
		if (WorldStorageProvider->GetRegionalData(load, 0, region_actors)) {
			TArray<FVoxelWorldActorRecord> regionActorRecords;

			FMemoryReader reader(region_actors, true);
			reader << regionActorRecords;

			for (auto& record : regionActorRecords) {
				FActorSpawnParameters params;

				UClass* recordClass = FindObject<UClass>(ANY_PACKAGE, *record.ActorClass);

				AActor* NewActor = GetWorld()->SpawnActor<AActor>(recordClass, record.ActorTransform.GetLocation(), record.ActorTransform.GetRotation().Rotator(), params);

				FMemoryReader MemoryReader(record.ActorData, true);
				FVoxelWorldSaveGameArchive Ar(MemoryReader);
				NewActor->Serialize(Ar);

				IISavableWithRegion::Execute_OnLoaded(NewActor);
				UE_LOG(LogTemp, Warning, TEXT("[db] Loaded region %s  %d B data %s."), *load.ToString(),region_actors.Num(),*BytesToHex(region_actors.GetData(),region_actors.Num()));
			}
		}
	}
}

bool APagedWorld::VoxelNetServer_SendPacketsToPagingComponent(UTerrainPagingComponent*& pager, TArray<TArray<uint8>> packets) {
	if (packets.Num() > 0) {
		auto pagingPawn = Cast<APawn>(pager->GetOwner());
		APlayerController* controller = pagingPawn == nullptr ? Cast<APlayerController>(pager->GetOwner()) : Cast<APlayerController>(pagingPawn->GetController());

		if (controller != nullptr) {
			if (VoxelNetServer_PlayerVoxelServers.Contains(controller)) {
				auto server = VoxelNetServer_PlayerVoxelServers.Find(controller);
				server->Get()->UploadRegions(packets);
				return true;
			}
			else {
				// this gets spammy
				//UE_LOG(LogTemp, Warning, TEXT("Server Paging Component Tick: VoxelNetServer_PlayerVoxelServers does not contain this controller."));
				return false;
			}
		}
		else {
			UE_LOG(LogTemp, Warning, TEXT("Server Paging Component Tick: Unable to find controller for pager."));
			return false;
		}
	}
	return true;
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
			FIntVector pos = VoxelToRegionCoords(WorldToVoxelCoords(pager->GetPagingLocation()));

			for (int z = radius - 1; z >= -radius; z--) {
				for (int y = -radius; y < radius; y++) {
					for (int x = -radius; x < radius; x++) {
						FIntVector surrounding = pos + FIntVector(REGION_SIZE * x, REGION_SIZE * y, REGION_SIZE * z);
						pager->subscribedRegions.Emplace(surrounding);
						regionsToLoad.Emplace(surrounding);
					}
				}
			}

			if (bIsVoxelNetServer) {
				/*auto toUpload = pager->subscribedRegions.Difference(previousSubscribedRegions); // to load
											 
															 // since the vast majority of packets are delayed maybe we should just delay all of them to simplify
															 for (auto& uploadRegion : toUpload) {
																	 pager->waitingForPackets.Add(uploadRegion);
															 }*/

				pager->waitingForPackets.Append(pager->subscribedRegions.Difference(previousSubscribedRegions));
			}
		}
	}

	for (auto& elem : toRemove) { pagingComponents.Remove(elem); }
	toRemove.Reset();
	
	regionsToLoad.Append(ForceLoadedRegions);
	UnloadRegionsExcept(regionsToLoad);
}

void APagedWorld::UnloadRegionsExcept(TSet<FIntVector> regionsToLoad) {
	TArray<FIntVector> currentRegionsArr;
	regions.GetKeys(currentRegionsArr);

	const TSet<FIntVector> currentRegions(currentRegionsArr);

	auto toUnload = currentRegions.Difference(regionsToLoad); // to unload
	auto toLoad = regionsToLoad.Difference(currentRegions); // to load

	SaveAllDataForRegions(toUnload);
	LoadAllDataForRegions(toLoad);

	for (auto& unload : toUnload) {
		regions.FindAndRemoveChecked(unload)->SetLifeSpan(.1);
		//UE_LOG(LogTemp, Warning, TEXT("Paging out region %s"), *unload.ToString());
	}

	for (auto& load : toLoad) {
		if (!regions.Contains(load)) {
			getRegionAt(load);
			MarkRegionDirtyAndAdjacent(load);
		}
		//see what happens if i put this here: it will refresh all regions once per paging comp tick not sure if that would be terrible or cover up some issues
		//MarkRegionDirtyAndAdjacent(load); // cant tell if this makes any difference. maybe helps w falling thru floors?
	}
}

void APagedWorld::RegisterPlayerWithCookie(APlayerController* player, int64 cookie) {
	if (bIsVoxelNetServer) {
		if (VoxelNetServer_SentHandshakes.Contains(cookie)) {
			const auto handshake = VoxelNetServer_SentHandshakes.FindRef(cookie);
			VoxelNetServer_SentHandshakes.Remove(cookie);

			if (handshake.IsValid() && GetLocalRole() == ROLE_Authority) {
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
		const auto bRegionExists = world->WorldStorageProvider->GetRegion(pos, pChunk);
		if (!bRegionExists) { world->BeginWorldGeneration(pos); }
	}
	return;
}

void WorldPager::pageOut(const PolyVox::Region& region, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk* pChunk) {
	// sometimes but not always this occurs outside Game thread, world storage provider should be ok with that
	if (world->bIsVoxelNetServer || world->bIsVoxelNetSingleplayer) {
		const FIntVector pos = FIntVector(region.getLowerX(), region.getLowerY(), region.getLowerZ());

		// get savablewithregion actors in this region
		// AsyncTask(ENamedThreads::GameThread, [this, pos]() { // for now this will have to do
		// 	
		// });

#ifndef DONT_SAVE
		world->WorldStorageProvider->PutRegion(pos, pChunk);
#endif
		//UE_LOG(LogTemp, Warning, TEXT("[db] Saved region to db:  %s."), *pos.ToString());
	}
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
		int64 cookie = FMath::RandRange(-255, 255); // min and max 64 dont work at all, weird stuff going on here
		while (cookie == FMath::RandRange(-255, 255)) {
			// this happens on early connects
			cookie = FMath::RandRange(-255, 255);
			UE_LOG(LogTemp, Warning, TEXT("Failed to generate random cookie for %s, retrying..."), *endpoint.ToString());
			FPlatformProcess::Sleep(1);
		}

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
