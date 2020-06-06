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
#include "ISavableComponent.h"
#include "MCTPlugin.h"
#include "StorageProviderFlatfile.h"
#include "Net/UnrealNetwork.h"

#ifdef WORLD_TICK_TRACKING
DECLARE_CYCLE_STAT(TEXT("World Process New Regions"), STAT_WorldNewRegions, STATGROUP_VoxelWorld);
DECLARE_CYCLE_STAT(TEXT("World Do Voxel Updates"), STAT_WorldVoxelUpdates, STATGROUP_VoxelWorld);
DECLARE_CYCLE_STAT(TEXT("World Process Dirty Regions"), STAT_WorldDirtyRegions, STATGROUP_VoxelWorld);
DECLARE_CYCLE_STAT(TEXT("World Clear Extraction Queue"), STAT_WorldClearExtractionQueue, STATGROUP_VoxelWorld);
#endif

APagedWorld::APagedWorld() {
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
}

APagedWorld::~APagedWorld() {
}

void APagedWorld::BeginPlay() {
	Super::BeginPlay();
	VoxelWorldThreadPool = FQueuedThreadPool::Allocate();
	
	const int32 NUMBER_OF_POOLS = 1;
	//const int32 NumCoresPerPool = bShareCores ? FPlatformMisc::NumberOfCoresIncludingHyperthreads() : FPlatformMisc::NumberOfCoresIncludingHyperthreads() / NUMBER_OF_POOLS;
	const int32 NumCoresPerPool = 4; // todo testing
	VoxelWorldThreadPool->Create(NumCoresPerPool, 256 * 1024); // thread pool for extraction and worldgen tasks

	//URuntimeMesh::InitializeMultiThreading(NumCoresPerPool); // thread pool for RMC //// currently runs stuff that throws a check (IsInGameThread()) so useless
	FRandomStream Rand(WorldSeed);
	RegionSeedRandomX = Rand.GetUnsignedInt();
	RegionSeedRandomY = Rand.GetUnsignedInt();
	RegionSeedRandomZ = Rand.GetUnsignedInt();
}

void APagedWorld::EndPlay(const EEndPlayReason::Type EndPlayReason) { SaveAndShutdown(); }

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
			UE_LOG(LogVoxelWorld, Error, TEXT("Caught exception in voxelUpdateQueue processing."));
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

			if(!gen.bIsEmpty) {
				bool needed = false;
				for(auto pager:pagingComponents) {
					if(pager->subscribedRegions.Contains(gen.pos)) { // is anyone subscribed to this
						needed = true;
						break;
					}
				}

				if(needed) PacketsToSendOrResendToSubscribersNextExtraction.Emplace(gen.pos);
			}
			
			MarkRegionDirtyAndAdjacent(gen.pos);
			if (RegionGenerated_Event.IsBound())
				RegionGenerated_Event.Broadcast(gen.pos);
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
		
		if(!output.bIsEmpty) { // we only request non air regions to be resent
			if(PacketsToSendOrResendToSubscribersNextExtraction.Contains(output.region)) {
				PacketsReadyToSendOrResend.Emplace(output.region);
				PacketsToSendOrResendToSubscribersNextExtraction.Remove(output.region);
			}
		}

	}
}

void APagedWorld::Tick(float DeltaTime) {
	Super::Tick(DeltaTime);

	//auto ResendClone = PacketsReadyToSendOrResend; // because we add to this in WorldNewRegionsTick()
	//PacketsToSendOrResendToSubscribersNextExtraction.Reset();
	
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
			// if it is not in regions it will get discarded
			if (regions.Contains(region)) // this condition was causing client tried to access unrepped regions --> /*|| (!bIsVoxelNetServer && !bIsVoxelNetSingleplayer))*/ 
				QueueRegionRender(region);
		}

#ifdef WORLD_TICK_TRACKING
	}
	{
		SCOPE_CYCLE_COUNTER(STAT_WorldClearExtractionQueue);
#endif
		//TArray<FIntVector> VoxelNetServer_justCookedRegions;

		TSet<FIntVector> ForceSendRegions;
		
		while (!extractionQueue.IsEmpty()) {
			FExtractionTaskOutput gen;
			extractionQueue.Dequeue(gen);
			NumRegionsPendingExtraction--;
			auto reg = getRegionAt(gen.region);

			if(PacketsReadyToSendOrResend.Contains(gen.region)) {
				ForceSendRegions.Emplace(gen.region);
				PacketsReadyToSendOrResend.Remove(gen.region);
			}
			
			if (reg != nullptr) {
				reg->RenderParsed(gen);
				if(!gen.bIsEmpty) {
					reg->UpdateNavigation();
					SetHighestGeneratedRegionAt(gen.region.X,gen.region.Y,gen.region.Z);
				}
			}
			else {
				if(!gen.bIsEmpty)
					UE_LOG(LogVoxelWorld, Warning, TEXT("Non-air region was meshed but actor has not replicated."));
				
				OnRegionError(gen.region);
			}
		}

		//PacketsToSendOrResendToSubscribersNextExtraction.Append(ResendClone); // these were not extracted this frame

		if (bIsVoxelNetServer) {
			for (auto& pager : pagingComponents) {
				TArray<TArray<uint8>> packets;
				TSet<FIntVector> hits;

				TArray<FIntVector> cachedPackets;
				VoxelNetServer_regionPackets.GetKeys(cachedPackets);

				TSet<FIntVector> cacheSet = TSet<FIntVector>(cachedPackets);

				pager->waitingForPackets.Append(pager->subscribedRegions.Intersect(ForceSendRegions));
				
				for (auto& waitingFor : pager->waitingForPackets.Intersect(cacheSet)) {
					auto packetToSend = VoxelNetServer_regionPackets.FindRef(waitingFor);
					// where waitingFor and the cache intersect send packets					
					packets.Add(packetToSend);
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

void APagedWorld::PinRegionsInRadius(const FIntVector& VoxelCoords, int32 Radius) {
	FIntVector pos = VoxelToRegionCoords(VoxelCoords);

	for (int z = -Radius; z < Radius; z++) {
		for (int y = -Radius; y < Radius; y++) {
			for (int x = -Radius; x < Radius; x++) {
				FIntVector surrounding = pos + FIntVector(REGION_SIZE * x, REGION_SIZE * y, REGION_SIZE * z);
				ForceLoadedRegions.Emplace(surrounding);
			}
		}
	}
}

void APagedWorld::UnpinRegionsInRadius(const FIntVector& VoxelCoords, int32 Radius) {
	FIntVector pos = VoxelToRegionCoords(VoxelCoords);

	for (int z = -Radius; z < Radius; z++) {
		for (int y = -Radius; y < Radius; y++) {
			for (int x = -Radius; x < Radius; x++) {
				FIntVector surrounding = pos + FIntVector(REGION_SIZE * x, REGION_SIZE * y, REGION_SIZE * z);
				ForceLoadedRegions.Remove(surrounding);
			}
		}
	}
}

void APagedWorld::ClearPinnedRegions() { ForceLoadedRegions.Reset(); }

void APagedWorld::ConnectToDatabase(FString Name) {
	if (bIsVoxelNetServer || bIsVoxelNetSingleplayer) {
		bHasStarted = true;
		//WorldStorageProvider = new StorageProviderLevelDB(true);
		WorldStorageProvider = new StorageProviderFlatfile();
		//WorldStorageProvider = new StorageProviderTMap(true);
		//WorldStorageProvider = new StorageProviderNull();

		auto status = WorldStorageProvider->Open(TCHAR_TO_UTF8(*Name), true);

		UE_LOG(LogVoxelDatabase, Warning, TEXT("Database connection to %hs using provider %hs: %s"), WorldStorageProvider->GetDatabasePath(TCHAR_TO_UTF8(*Name)).c_str(),
		       WorldStorageProvider->GetProviderName(), status ? TEXT("Success") : TEXT("Failure"));

		const auto compatible = WorldStorageProvider->VerifyDatabaseFormat(DB_VERSION);

		UE_LOG(LogVoxelDatabase, Warning, TEXT("Database version for %s: %d. %s"), *Name, WorldStorageProvider->GetDatabaseFormat(), compatible? TEXT("Version is compatible.") : TEXT("Version is NOT compatible! Cannot load data."));
		ensure(compatible);

		TArray<uint8> propertiesBytes;
		if(WorldStorageProvider->GetGlobalData("WorldProperties", propertiesBytes)){
			FMemoryReader reader(propertiesBytes,true);

			reader << WorldSeed;

			reader.Flush();
			reader.Close();
		}
		
		// this might not be the best place for this but
		TArray<uint8> PIDbytes;
		if(WorldStorageProvider->GetGlobalData("NextPersistentActorID", PIDbytes)){
			
			FMemoryReader reader(PIDbytes,true);

			reader << NextPersistentActorID;

			reader.Flush();
			reader.Close();

			if(NextPersistentActorID <= 0) NextPersistentActorID = 1;
		}
	}
}

void APagedWorld::PostInitializeComponents() {
	Super::PostInitializeComponents();
	VoxelVolume = MakeShareable(new PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>(new WorldPager(this), VolumeTargetMemoryMB * 1024 * 1024,REGION_SIZE));
	//VoxelVolume = MakeShareable(new PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>(new WorldPager(this), 256 * 1024 * 1024,REGION_SIZE));
	//VoxelVolume = MakeShareable(new PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>(new WorldPager(this)));
	//VoxelVolume = MakeShareable(new PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>(new WorldPager(this), 2 * 1024 * 1024 * 1024,REGION_SIZE));
}

void APagedWorld::BeginDestroy() {
	Super::BeginDestroy();
	SaveAndShutdown();
}

void APagedWorld::SaveAndShutdown() {
	if (bHasShutdown || !bHasStarted)
		return;
	
	bHasShutdown = true;
	VoxelWorldThreadPool->Destroy();

	if (bIsVoxelNetServer) {
		UE_LOG(LogVoxelNet, Warning, TEXT("Stopping VoxelNet server..."));
		for (auto& elem : VoxelNetServer_ServerThreads) {
			if (elem)
				elem->Kill(true);
		}
		for (auto& elem : VoxelNetServer_VoxelServers) { elem.Reset(); }
		if (VoxelNetServer_ServerListener.IsValid())
			VoxelNetServer_ServerListener.Get()->Stop();
		VoxelNetServer_ServerListener.Reset();
		UE_LOG(LogVoxelNet, Warning, TEXT("VoxelNet server has stopped."));
	}
	else if (!bIsVoxelNetSingleplayer) {
		UE_LOG(LogVoxelNet, Warning, TEXT("Stopping VoxelNet client..."));
		if (VoxelNetClient_ClientThread)
			VoxelNetClient_ClientThread->Kill(true);
		VoxelNetClient_VoxelClient.Reset();
		UE_LOG(LogVoxelNet, Warning, TEXT("VoxelNet client has stopped."));
	}

	if (bIsVoxelNetServer || bIsVoxelNetSingleplayer)
	UE_LOG(LogVoxelDatabase, Warning, TEXT("Saving and disconnecting world database..."));

	const auto beforeSave = FDateTime::UtcNow();

	PreSaveWorld();

	if (bIsVoxelNetServer || bIsVoxelNetSingleplayer) {
		TArray<uint8> worldProperties;
		FMemoryWriter writer(worldProperties);

		writer << WorldSeed;

		writer.Flush();
		writer.Close();
		
		WorldStorageProvider->PutGlobalData("WorldProperties", worldProperties);
		
		UE_LOG(LogVoxelDatabase, Warning, TEXT("Saving data for regions..."));
		UnloadRegionsExcept(TSet<FIntVector>()); // force save all entities
		UE_LOG(LogVoxelDatabase, Warning, TEXT("Regional data saved."));
	}

	VolumeMutex.Lock();
	Flush();
	VoxelVolume.Reset();

	if (bIsVoxelNetSingleplayer || bIsVoxelNetServer)
		WorldStorageProvider->Close();

	VolumeMutex.Unlock();

	PostSaveWorld();

	const auto afterSave = FDateTime::UtcNow() - beforeSave;

	if (bIsVoxelNetServer || bIsVoxelNetSingleplayer)
	UE_LOG(LogVoxelDatabase, Warning, TEXT("World database saved in %f ms."), afterSave.GetTotalMicroseconds()*0.001);
}

void APagedWorld::RegisterPagingComponent(UTerrainPagingComponent* pagingComponent) { pagingComponents.AddUnique(pagingComponent); }

void APagedWorld::ResendRegion(const FIntVector& region) {
	PacketsToSendOrResendToSubscribersNextExtraction.Emplace(region);
}

void APagedWorld::OnFatalError() {
}

APagedRegion* APagedWorld::getRegionAt(const FIntVector& pos) {
	if (regions.Contains(pos))
		return regions.FindRef(pos);

	if (!bIsVoxelNetServer && !bIsVoxelNetSingleplayer) {
		UE_LOG(LogVoxelWorld, Warning, TEXT("Client tried to access nonexistant region %s."), *pos.ToString());
		return nullptr;
	}

	const FVector fpos = FVector(pos * VOXEL_SIZE);

	try {
		APagedRegion* region = GetWorld()->SpawnActorDeferred<APagedRegion>(APagedRegion::StaticClass(), FTransform(FRotator::ZeroRotator), GetOwner());
		region->World = this;
		UGameplayStatics::FinishSpawningActor(region, FTransform(FRotator::ZeroRotator, fpos));

		regions.Add(pos, region);
		return region;
	}
	catch (...) {
		UE_LOG(LogVoxelWorld, Error, TEXT("Exception spawning a new region actor at %s."), *pos.ToString());
		return nullptr;
	}
}

bool APagedWorld::isRegionReadyServer(const FIntVector& pos) {
	if (regions.Contains(pos)) {
		auto region = *regions.Find(pos);
		return region->bReadyServer;
	}
	return false;
}

bool APagedWorld::isRegionReadyLocal(const FIntVector& pos) {
	if (regions.Contains(pos)) {
		auto region = *regions.Find(pos);
		return region->bReadyLocally;
	}
	return false;
}

bool APagedWorld::isRegionEmptyServer(const FIntVector& pos) {
	if (regions.Contains(pos)) {
		auto region = *regions.Find(pos);
		return region->bEmptyServer;
	}
	return false;
}


int32 APagedWorld::GetHighestGeneratedRegionAt(int32 RegionX, int32 RegionY) {
	auto Key = FIntVector(RegionX,RegionY,0);
	if(HighestGeneratedRegion.Contains(Key)) {
		return  *HighestGeneratedRegion.Find(Key);
	} else {
		return -1;
	}
}

void APagedWorld::SetHighestGeneratedRegionAt(int32 RegionX, int32 RegionY, int32 RegionZ) {
	auto Key = FIntVector(RegionX,RegionY,0);
	if(HighestGeneratedRegion.Contains(Key)) {
		int32 CurrentHighest = *HighestGeneratedRegion.Find(Key);
		if(RegionZ > CurrentHighest) {
			HighestGeneratedRegion.Emplace(Key, RegionZ);
		}
	} else {
		HighestGeneratedRegion.Emplace(Key, RegionZ);
	}
}

void APagedWorld::QueueRegionRender(const FIntVector& pos) {
	NumRegionsPendingExtraction++;

	if (bRenderMarchingCubes) { (new FAutoDeleteAsyncTask<ExtractionThreads::MarchingCubesExtractionTask>(this, pos))->StartBackgroundTask(VoxelWorldThreadPool); }
	else { (new FAutoDeleteAsyncTask<ExtractionThreads::CubicExtractionTask>(this, pos))->StartBackgroundTask(VoxelWorldThreadPool); }
}

void APagedWorld::MarkRegionDirtyAndAdjacent(const FIntVector& pos) {
	dirtyRegions.Emplace(pos);
	dirtyRegions.Emplace(pos + FIntVector(REGION_SIZE, 0, 0));
	dirtyRegions.Emplace(pos + FIntVector(0, REGION_SIZE, 0));
	dirtyRegions.Emplace(pos + FIntVector(0, 0, REGION_SIZE));
	dirtyRegions.Emplace(pos + FIntVector(-REGION_SIZE, 0, 0));
	dirtyRegions.Emplace(pos + FIntVector(0, -REGION_SIZE, 0));
	dirtyRegions.Emplace(pos + FIntVector(0, 0, -REGION_SIZE));
}


void APagedWorld::PrefetchRegionsInRadius(const FIntVector& pos, int32 radius) const {
	auto reg = PolyVox::Region(pos.X, pos.Y, pos.Z, pos.X + REGION_SIZE, pos.Y + REGION_SIZE, pos.Z + REGION_SIZE);
	reg.grow(radius * REGION_SIZE);
	VoxelVolume.Get()->prefetch(reg);
}


bool APagedWorld::isRegionEmptyLocal(const FIntVector& pos) {
		if (regions.Contains(pos)) {
		auto region = *regions.Find(pos);
		return region->bEmptyLocally;
	}
	return false;
}

FIntVector APagedWorld::VoxelToRegionCoords(const FIntVector& VoxelCoords) {
	const FVector tmp = FVector(VoxelCoords) / (float)REGION_SIZE;
	return FIntVector(FMath::FloorToInt(tmp.X), FMath::FloorToInt(tmp.Y), FMath::FloorToInt(tmp.Z)) * 32;
}

FIntVector APagedWorld::WorldToVoxelCoords(const FVector& WorldCoords) { return FIntVector(WorldCoords / VOXEL_SIZE); }

FVector APagedWorld::VoxelToWorldCoords(const FIntVector& VoxelCoords) { return FVector(VoxelCoords * VOXEL_SIZE); }

/*bool APagedWorld::Server_ModifyVoxel_Validate(FIntVector VoxelLocation, uint8 Radius, uint8 Material, uint8 Density, AActor* cause, bool bIsSpherical, bool bShouldDrop) {
	//cause is valid and cause is less than x away , maybe make one without radius because thats usually server side 
	return true;
}

void APagedWorld::Server_ModifyVoxel_Implementation(FIntVector VoxelLocation, uint8 Radius, uint8 Material, uint8 Density, AActor* cause, bool bIsSpherical, bool bShouldDrop) {
	Multi_ModifyVoxel(VoxelLocation, Radius, Material, Density, cause, bIsSpherical, bShouldDrop);
}*/

void APagedWorld::Multi_ModifyVoxel_Implementation(const FIntVector& VoxelLocation, uint8 Radius, uint8 Material, uint8 Density, AActor* cause, bool bIsSpherical, bool bShouldDrop) {
	voxelUpdateQueue.Enqueue(FVoxelUpdate(VoxelLocation,Radius, Material, Density, bShouldDrop, bIsSpherical, true, cause));
}

void APagedWorld::BeginWorldGeneration(const FIntVector& RegionCoords) {
	if (bIsVoxelNetServer || bIsVoxelNetSingleplayer) {
		remainingRegionsToGenerate++;

		//UE_LOG(LogVoxelWorld, Warning, TEXT("Starting worldgen for region [%s]."), *RegionCoords.ToString());
		
		(new FAutoDeleteAsyncTask<WorldGenThreads::RegionGenerationTask>(this, RegionCoords))->StartBackgroundTask(VoxelWorldThreadPool);
	}
}

int32 APagedWorld::GetRegionSeed(const FIntVector& RegionCoords) { // inspired by minecraft
	return (RegionCoords.X * RegionSeedRandomX + RegionCoords.Y * RegionSeedRandomY + RegionCoords.Z * RegionSeedRandomZ) ^ WorldSeed;
}

float APagedWorld::GetHeightmapZ(int32 VoxelX, int32 VoxelY, uint8 HeightmapIndex) { return GetNoiseGeneratorArray()[HeightmapIndex]->GetNoise2D(VoxelX, VoxelY); }

int32 APagedWorld::getVolumeMemoryBytes() const { return VoxelVolume.IsValid() ? VoxelVolume.Get()->calculateSizeInBytes() : -1; }

void APagedWorld::Flush() const { VoxelVolume.Get()->flushAll(); }

void APagedWorld::LaunchServer() {
	bIsVoxelNetServer = true;
	ConnectToDatabase(WorldName);
	VoxelNetServer_StartServer();
}

void APagedWorld::LaunchSingleplayer() {
	bIsVoxelNetSingleplayer = true;
	ConnectToDatabase(WorldName);
}

void APagedWorld::LaunchClient(FString Host, int32 Port) { VoxelNetClient_ConnectToServer(Host, Port); }

void APagedWorld::ForceSaveWorld() {
	PreSaveWorld();
	VolumeMutex.Lock();
	Flush();
	VolumeMutex.Unlock();
	PostSaveWorld();
}

void APagedWorld::PreSaveWorld() {
	OnPreSaveWorld();
	if(PreSaveWorld_Event.IsBound()) PreSaveWorld_Event.Broadcast(false);

	//todo move somewhere else  // todo write loading
	TArray<uint8> data;
	FMemoryWriter writer(data,true);

	writer << NextPersistentActorID;

	writer.Flush();
	writer.Close();
	
	WorldStorageProvider->PutGlobalData("NextPersistentActorID", data);
}

void APagedWorld::PostSaveWorld() {
		OnPostSaveWorld();
	if(PostSaveWorld_Event.IsBound()) PostSaveWorld_Event.Broadcast(false);
}

void APagedWorld::SaveGlobalString(FString Key, FString Value) {
	if(WorldStorageProvider)
	WorldStorageProvider->PutGlobalString( std::string(TCHAR_TO_UTF8(*Key)), std::string(TCHAR_TO_UTF8(*Value)));
}

bool APagedWorld::LoadGlobalString(FString Key, FString& Value) {
	if(!WorldStorageProvider) return false;
	
	std::string Str;
	bool retval = WorldStorageProvider->GetGlobalString(std::string(TCHAR_TO_UTF8(*Key)), Str);
	if(retval)
		Value = UTF8_TO_TCHAR(Str.c_str());
	return retval;
}

void APagedWorld::SaveAllDataForRegions(TSet<FIntVector> Regions) {
	TArray<AActor*> outActors;
	UGameplayStatics::GetAllActorsWithInterface(this, UISavableWithRegion::StaticClass(), outActors);

	for (auto& unload : Regions) {
		TArray<AActor*> saved;
		TArray<FVoxelWorldActorRecord> regionActorRecords;

		int32 actorCount = 0;
		for (auto& elem : outActors) {
			const FTransform Transform = IISavableWithRegion::Execute_GetSaveTransform(elem);
			const FIntVector saveRegion = VoxelToRegionCoords(WorldToVoxelCoords(Transform.GetLocation()));
			if (saveRegion == unload) {
				IISavableWithRegion::Execute_OnPreSave(elem);
				FVoxelWorldActorRecord record;

				record.ActorClass = elem->GetClass()->GetPathName();
				record.ActorTransform = Transform;

				auto FindKey = LivePersistentActors.FindKey(elem);
				if(FindKey) {
					record.PersistentActorID = *FindKey;
				} else {
					record.PersistentActorID = 0;
				}
				
				//////////////////////////////////
				// save components

				auto Components = elem->GetComponentsByInterface(UISavableComponent::StaticClass());
				for (auto& comp : Components) {
					IISavableComponent::Execute_OnPreSave(comp);
					FVoxelWorldComponentRecord compRecord;

					compRecord.ComponentClass = comp->GetClass()->GetPathName();
					compRecord.bSpawnIfNotFound = IISavableComponent::Execute_GetSpawnIfNotFound(comp);

					USceneComponent* SceneComp = Cast<USceneComponent>(comp);
					if (SceneComp) { compRecord.ComponentTransform = SceneComp->GetRelativeTransform(); }

					FMemoryWriter writer(compRecord.ComponentData, true);
					FVoxelWorldSaveGameArchive proxy(writer);

					comp->Serialize(proxy);

					proxy.Flush();
					proxy.Close();
					writer.Flush();
					writer.Close();

					record.ActorComponents.Add(compRecord);

					IISavableComponent::Execute_OnSaved(comp);

					UE_LOG(LogVoxelDatabase, Verbose, TEXT("[db] Saved component %s, %d B data: %s."), *comp->GetReadableName(), compRecord.ComponentData.Num(),
					       *BytesToHex(compRecord.ComponentData.GetData(),compRecord.ComponentData.Num()));
				}

				//////////////////////////////////

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
				actorCount++;
				UE_LOG(LogVoxelDatabase, Verbose, TEXT("[db] Saved region %s actor %s."), *unload.ToString(), *elem->GetHumanReadableName());

				elem->Destroy();
			}
		}
		outActors.RemoveAll([saved](AActor* a) { return saved.Contains(a); });

		FBufferArchive region_actors(true);
		region_actors << regionActorRecords;

		WorldStorageProvider->PutRegionalData(unload, 0, region_actors);

		if (regionActorRecords.Num() > 0)
		UE_LOG(LogVoxelDatabase, Verbose, TEXT("[db] Saved region %s, %d actors, %d B data: %s."), *unload.ToString(), actorCount, region_actors.Num(),
		       *BytesToHex(region_actors.GetData(),region_actors.Num()));
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

				if (!recordClass || !IsValid(recordClass)) { UE_LOG(LogTemp, Warning, TEXT("[db] ERROR LOADING Class %s from region saved actors! Class is null!"), *record.ActorClass); }
				else {
					AActor* NewActor = GetWorld()->SpawnActorDeferred<AActor>(recordClass, record.ActorTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

					if (NewActor) {
						FMemoryReader MemoryReader(record.ActorData, true);
						FVoxelWorldSaveGameArchive Ar(MemoryReader);
						NewActor->Serialize(Ar);

						if(record.PersistentActorID != 0) {
							RegisterExistingPersistentActor(NewActor, record.PersistentActorID);
							//UE_LOG(LogTemp, Warning, TEXT("Loaded persistent actor id %d"), (int32)record.PersistentActorID);
						}
						
						UGameplayStatics::FinishSpawningActor(NewActor, record.ActorTransform);

						////
						//////////////////
						/// do components
						///

						TSet<UActorComponent*> processedComps;

						UE_LOG(LogVoxelDatabase, Verbose, TEXT("[db] There are %d component records on this actor %s"), record.ActorComponents.Num(), *NewActor->GetHumanReadableName());

						for (auto& compRecord : record.ActorComponents) {
							UClass* compClass = FindObject<UClass>(ANY_PACKAGE, *compRecord.ComponentClass);

							if (!compClass || !IsValid(compClass)) {
								UE_LOG(LogVoxelDatabase, Error, TEXT("[db] ERROR LOADING Component Class %s from region saved actors! Class is null!"), *compRecord.ComponentClass);
								continue;
							}

							TArray<UActorComponent*> outComps;
							NewActor->GetComponents(compClass, outComps); // get components of the class we are loading

							UE_LOG(LogVoxelDatabase, Verbose, TEXT("[db] Trying to resolve component record %s on actor %s"), *compRecord.ComponentClass , *NewActor->GetHumanReadableName());
							
							UE_LOG(LogVoxelDatabase, Verbose, TEXT("[db] There are %d  %s components on actor %s"), outComps.Num(),*compRecord.ComponentClass , *NewActor->GetHumanReadableName());

							UActorComponent* currentComp = nullptr;

							for (auto& elem : outComps) {
								// search for an unseen comp of the proper class
								if (!processedComps.Contains(elem)) {
									currentComp = elem;
									processedComps.Add(elem);
									UE_LOG(LogVoxelDatabase, Verbose, TEXT("[db] Found this comp %s"), *elem->GetReadableName());
									break;
								} else {
									UE_LOG(LogVoxelDatabase, Verbose, TEXT("[db] Already seen this comp %s"), *elem->GetReadableName());
								}
							}
							
							if (!currentComp) {
								UE_LOG(LogVoxelDatabase, Warning, TEXT("[db] Failed to find component %s on actor %s"), *compRecord.ComponentClass, *NewActor->GetHumanReadableName());
								if (compRecord.bSpawnIfNotFound) {
									// spawn it
									check(false);//not implemented
								}
								else { continue; }
							}

							if (currentComp) {
								UE_LOG(LogVoxelDatabase, Verbose, TEXT("[db] Loaded component %s  %d B data %s."), *compRecord.ComponentClass, compRecord.ComponentData.Num(),
								       *BytesToHex(compRecord.ComponentData.GetData(),compRecord.ComponentData.Num()));

								//USceneComponent* SceneComp = Cast<USceneComponent>(currentComp);
								//if (SceneComp) { SceneComp->SetRelativeTransform(compRecord.ComponentTransform); } // todo revisit

								FMemoryReader compMemoryReader(compRecord.ComponentData, true);
								FVoxelWorldSaveGameArchive compAr(compMemoryReader);
								currentComp->Serialize(compAr);
								UE_LOG(LogVoxelDatabase, Verbose, TEXT("--------------- Component Class %s loaded"), *compRecord.ComponentClass);
								IISavableComponent::Execute_OnLoaded(currentComp);
							}
						}

						/////////////////

						////
						IISavableWithRegion::Execute_OnLoaded(NewActor);
						UE_LOG(LogVoxelDatabase, Verbose, TEXT("[db] Loaded region %s  %d B data %s."), *load.ToString(), region_actors.Num(), *BytesToHex(region_actors.GetData(),region_actors.Num()));
					}
					else {
						UE_LOG(LogVoxelDatabase, Error, TEXT("[db] ERROR LOADING Class %s from region saved actors! Failed to spawn at %s!"), *record.ActorClass,
						       *record.ActorTransform.ToHumanReadableString());
					}
				}
			}
		}
	}
}

void APagedWorld::SavePlayerActor(FString Identifier, AActor* ActorToSerialize) {
	if (ActorToSerialize && IsValid(ActorToSerialize)) {
		///////////////////////////////////////////////////////////
			   ///
		const FTransform Transform = ActorToSerialize->GetActorTransform();
		//const FIntVector saveRegion = VoxelToRegionCoords(WorldToVoxelCoords(Transform.GetLocation()));

		FVoxelWorldPlayerActorRecord record;

		record.ActorClass = ActorToSerialize->GetClass()->GetPathName();
		record.ActorTransform = Transform;
		record.SavedAt = FDateTime::UtcNow();
		//////////////////////////////////
		// save components

		auto Components = ActorToSerialize->GetComponentsByInterface(UISavableComponent::StaticClass());
		for (auto& comp : Components) {
			IISavableComponent::Execute_OnPreSave(comp);
			FVoxelWorldComponentRecord compRecord;

			compRecord.ComponentClass = comp->GetClass()->GetPathName();
			compRecord.bSpawnIfNotFound = IISavableComponent::Execute_GetSpawnIfNotFound(comp);

			USceneComponent* SceneComp = Cast<USceneComponent>(comp);
			if (SceneComp) { compRecord.ComponentTransform = SceneComp->GetRelativeTransform(); }

			FMemoryWriter writer(compRecord.ComponentData, true);
			FVoxelWorldSaveGameArchive proxy(writer);

			comp->Serialize(proxy);

			proxy.Flush();
			proxy.Close();
			writer.Flush();
			writer.Close();

			record.ActorComponents.Add(compRecord);

			IISavableComponent::Execute_OnSaved(comp);

			UE_LOG(LogVoxelDatabase, Verbose, TEXT("[db] Saved component %s, %d B data: %s."), *comp->GetReadableName(), compRecord.ComponentData.Num(),
			       *BytesToHex(compRecord.ComponentData.GetData(),compRecord.ComponentData.Num()));
		}

		//////////////////////////////////

		FMemoryWriter writer(record.ActorData, true);
		FVoxelWorldSaveGameArchive proxy(writer);

		ActorToSerialize->Serialize(proxy);

		proxy.Flush();
		proxy.Close();
		writer.Flush();
		writer.Close();

		FBufferArchive playerSave(true);
		playerSave << record;

		playerSave.Flush();

		WorldStorageProvider->PutGlobalData("PLAYER_" + std::string(TCHAR_TO_UTF8(*Identifier)), playerSave);

		UE_LOG(LogVoxelDatabase, Verbose, TEXT("[db] Saved player actor %s."), *Identifier);
		UE_LOG(LogVoxelDatabase, Verbose, TEXT("[db] data: %s."), *BytesToHex(playerSave.GetData(),playerSave.Num()));
		/////////////////////////////////////////////////////////
	}
}

bool APagedWorld::LoadPlayerActor(FString Identifier, AActor* ExistingActor, bool bSetTransform, FTransform& OutTransform) {
	TArray<uint8> playerSave;

	if (WorldStorageProvider->GetGlobalData("PLAYER_" + std::string(TCHAR_TO_UTF8(*Identifier)), playerSave)) {
		FVoxelWorldPlayerActorRecord record;
		FMemoryReader reader(playerSave, true);
		reader << record;

		UClass* recordClass = FindObject<UClass>(ANY_PACKAGE, *record.ActorClass);

		if (!recordClass || !IsValid(recordClass) || ExistingActor->GetClass() != recordClass) {
			UE_LOG(LogVoxelDatabase, Error, TEXT("[db] ERROR LOADING Class %s from saved player actor %s! Class is null or not the same as the provided actor!"), *record.ActorClass, *Identifier);
			return false;
		}
		else {
			//AActor* NewActor = GetWorld()->SpawnActorDeferred<AActor>(recordClass, record.ActorTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

			if (ExistingActor) {
				FMemoryReader MemoryReader(record.ActorData, true);
				FVoxelWorldSaveGameArchive Ar(MemoryReader);
				ExistingActor->Serialize(Ar);

				OutTransform = record.ActorTransform;
				
				if(bSetTransform) {
					ExistingActor->SetActorTransform(record.ActorTransform);
				}
				//UGameplayStatics::FinishSpawningActor(NewActor, record.ActorTransform);
				////
				//////////////////
				/// do components
				///

				TSet<UActorComponent*> processedComps;

				UE_LOG(LogVoxelDatabase, Verbose, TEXT("[db] There are %d component records on this actor %s"), record.ActorComponents.Num(), *ExistingActor->GetHumanReadableName());

				for (auto& compRecord : record.ActorComponents) {
					UClass* compClass = FindObject<UClass>(ANY_PACKAGE, *compRecord.ComponentClass);

					if (!compClass || !IsValid(compClass)) {
						UE_LOG(LogVoxelDatabase, Error, TEXT("[db] ERROR LOADING Component Class %s from saved player actor %s! Class is null!"), *compRecord.ComponentClass, *Identifier);
						continue;
					}

					TArray<UActorComponent*> outComps;
					ExistingActor->GetComponents(compClass, outComps);

					UE_LOG(LogVoxelDatabase, Verbose, TEXT("[db] There are %d components on player actor %s"), outComps.Num(), *Identifier);

					UActorComponent* currentComp = nullptr;

					for (auto& elem : outComps) {
						// search for an unseen comp of the proper class
						if (!processedComps.Contains(elem)) {
							currentComp = elem;
							processedComps.Add(elem);
						}
					}

					if (!currentComp) {
						UE_LOG(LogVoxelDatabase, Verbose, TEXT("[db] Failed to find component %s on player actor %s"), *compRecord.ComponentClass, *Identifier);
						if (compRecord.bSpawnIfNotFound) {
							//todo spawn it
						}
						else { continue; }
					}

					if (currentComp) {
						UE_LOG(LogVoxelDatabase, Verbose, TEXT("[db] Loaded component %s  %d B data %s."), *compRecord.ComponentClass, compRecord.ComponentData.Num(),
						       *BytesToHex(compRecord.ComponentData.GetData(),compRecord.ComponentData.Num()));

						USceneComponent* SceneComp = Cast<USceneComponent>(currentComp);
						if (SceneComp) { SceneComp->SetRelativeTransform(compRecord.ComponentTransform); }

						FMemoryReader compMemoryReader(compRecord.ComponentData, true);
						FVoxelWorldSaveGameArchive compAr(compMemoryReader);
						currentComp->Serialize(compAr);
						UE_LOG(LogVoxelDatabase, Verbose, TEXT("--------------- Component Class %s loaded"), *compRecord.ComponentClass);
					}
				}

				/////////////////

				////
				UE_LOG(LogVoxelDatabase, Verbose, TEXT("[db] Loaded player actor %s  data %s."), *Identifier, *BytesToHex(playerSave.GetData(),playerSave.Num()));
				return true;
			}
			else {
				UE_LOG(LogVoxelDatabase, Error, TEXT("[db] ERROR LOADING Class %s from saved player actor %s! Failed to spawn at %s!"), *record.ActorClass, *Identifier,
				       *record.ActorTransform.ToHumanReadableString());
				return false;
			}
		}


		/////////////////////
	}
	else { return false; }
}

bool APagedWorld::LoadAndSpawnPlayerActor(FString Identifier, AActor*& OutSpawnedActor) {
	TArray<uint8> playerSave;

	if (WorldStorageProvider->GetGlobalData("PLAYER_" + std::string(TCHAR_TO_UTF8(*Identifier)), playerSave)) {
		FVoxelWorldPlayerActorRecord record;
		FMemoryReader reader(playerSave, true);
		reader << record;

		UClass* recordClass = FindObject<UClass>(ANY_PACKAGE, *record.ActorClass);

		if (!recordClass || !IsValid(recordClass)) {
			UE_LOG(LogVoxelDatabase, Error, TEXT("[db] ERROR LOADING Class %s from saved player actor %s! Class is null!"), *record.ActorClass, *Identifier);
			return false;
		}
		else {
			AActor* NewActor = GetWorld()->SpawnActorDeferred<AActor>(recordClass, record.ActorTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

			if (NewActor) {
				FMemoryReader MemoryReader(record.ActorData, true);
				FVoxelWorldSaveGameArchive Ar(MemoryReader);
				NewActor->Serialize(Ar);

				UGameplayStatics::FinishSpawningActor(NewActor, record.ActorTransform);
				////
				//////////////////
				/// do components
				///

				TSet<UActorComponent*> processedComps;

				UE_LOG(LogVoxelDatabase, Verbose, TEXT("[db] There are %d component records on this actor %s"), record.ActorComponents.Num(), *NewActor->GetHumanReadableName());

				for (auto& compRecord : record.ActorComponents) {
					UClass* compClass = FindObject<UClass>(ANY_PACKAGE, *compRecord.ComponentClass);

					if (!compClass || !IsValid(compClass)) {
						UE_LOG(LogVoxelDatabase, Error, TEXT("[db] ERROR LOADING Component Class %s from saved player actor %s! Class is null!"), *compRecord.ComponentClass, *Identifier);
						continue;
					}

					TArray<UActorComponent*> outComps;
					NewActor->GetComponents(compClass, outComps);

					UE_LOG(LogVoxelDatabase, Verbose, TEXT("[db] There are %d components on player actor %s"), outComps.Num(), *Identifier);

					UActorComponent* currentComp = nullptr;

					for (auto& elem : outComps) {
						// search for an unseen comp of the proper class
						if (!processedComps.Contains(elem)) {
							currentComp = elem;
							processedComps.Add(elem);
						}
					}

					if (!currentComp) {
						UE_LOG(LogVoxelDatabase, Verbose, TEXT("[db] Failed to find component %s on player actor %s"), *compRecord.ComponentClass, *Identifier);
						if (compRecord.bSpawnIfNotFound) {
							// spawn it
						}
						else { continue; }
					}

					if (currentComp) {
						UE_LOG(LogVoxelDatabase, Verbose, TEXT("[db] Loaded component %s  %d B data %s."), *compRecord.ComponentClass, compRecord.ComponentData.Num(),
						       *BytesToHex(compRecord.ComponentData.GetData(),compRecord.ComponentData.Num()));

						USceneComponent* SceneComp = Cast<USceneComponent>(currentComp);
						if (SceneComp) { SceneComp->SetRelativeTransform(compRecord.ComponentTransform); }

						FMemoryReader compMemoryReader(compRecord.ComponentData, true);
						FVoxelWorldSaveGameArchive compAr(compMemoryReader);
						currentComp->Serialize(compAr);
						UE_LOG(LogVoxelDatabase, Verbose, TEXT("--------------- Component Class %s loaded"), *compRecord.ComponentClass);
					}
				}

				/////////////////
				OutSpawnedActor = NewActor;
				////
				UE_LOG(LogVoxelDatabase, Verbose, TEXT("[db] Loaded player actor %s  data %s."), *Identifier, *BytesToHex(playerSave.GetData(),playerSave.Num()));
				return true;
			}
			else {
				UE_LOG(LogVoxelDatabase, Error, TEXT("[db] ERROR LOADING Class %s from saved player actor %s! Failed to spawn at %s!"), *record.ActorClass, *Identifier,
				       *record.ActorTransform.ToHumanReadableString());
				return false;
			}
		}


		/////////////////////
	}
	else { return false; }


}

AActor* APagedWorld::GetPersistentActor(int64 ID) {
	auto actor = LivePersistentActors.Find(ID);

	if(actor && IsValid(*actor)) {
		auto Actor = *actor;
		if(!Actor->IsPendingKill())
		return Actor;
	}
	return nullptr;
}

int64 APagedWorld::RegisterNewPersistentActor(AActor* Actor) {
	int64 ID = NextPersistentActorID++;
	RegisterExistingPersistentActor(Actor, ID);
	return ID;
}

int64 APagedWorld::LookupPersistentActorID(AActor* Actor) {
	if(LivePersistentActors.Num() <= 0) return 0;
	
	auto Key = LivePersistentActors.FindKey(Actor);
	if(Key) {
		return *Key;
	} else {
		return 0;
	}

}

void APagedWorld::RegisterExistingPersistentActor(AActor* Actor, int64 ID) {
	LivePersistentActors.Add(ID, Actor);
}

void APagedWorld::UnregisterPersistentActor(int64 ID) {
	LivePersistentActors.Remove(ID);
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
				//UE_LOG(LogVoxelNet, Warning, TEXT("Server Paging Component Tick: VoxelNetServer_PlayerVoxelServers does not contain this controller."));
				return false;
			}
		}
		else {
			UE_LOG(LogVoxelNet, Warning, TEXT("Server Paging Component Tick: Unable to find controller for pager."));
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
	}

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

			if (handshake.IsValid() && GetLocalRole() == ROLE_Authority) {
				VoxelNetServer_PlayerVoxelServers.Add(player, handshake);
				UE_LOG(LogVoxelNet, Warning, TEXT("Registered player controller with cookie %llu."), cookie);
			}
			else { UE_LOG(LogVoxelNet, Warning, TEXT("There was no valid server for the cookie %llu."), cookie); }
		}
		else { UE_LOG(LogVoxelNet, Warning, TEXT("There was no handshake sent with the cookie %llu."), cookie); }
	}
}

void APagedWorld::DisconnectPlayerFromVoxelNet(APlayerController* player) {
	if(VoxelNetServer_PlayerVoxelServers.Contains(player)) {
		auto ServerForClient = VoxelNetServer_PlayerVoxelServers.FindAndRemoveChecked(player);
		if(ServerForClient.IsValid()) ServerForClient.Get()->Stop();
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
			UE_LOG(LogVoxelNet, Warning, TEXT("Failed to generate random cookie for %s, retrying..."), *endpoint.ToString());
			FPlatformProcess::Sleep(1);
		}

		UE_LOG(LogVoxelNet, Warning, TEXT("Connection received from %s, starting thread..."), *endpoint.ToString());
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
			UE_LOG(LogVoxelNet, Warning, TEXT("Client: Connecting to server %s..."), *addr->ToString(true));
			VoxelNetClient_VoxelClient = MakeShareable(new VoxelNetThreads::VoxelNetClient(this, clientSocket));
			VoxelNetClient_ClientThread = FRunnableThread::Create(VoxelNetClient_VoxelClient.Get(), TEXT("VoxelNetClient"));
		}
		else { UE_LOG(LogTemp, Warning, TEXT("Client: Failed to connect to server.")); }
		return true;
	}
	return false;
}

bool APagedWorld::VoxelNetClient_DisconnectFromServer() {
	if(VoxelNetClient_VoxelClient.IsValid()) {
		VoxelNetClient_VoxelClient.Get()->Stop();
		return true;
	}
	return false;
}

int32 APagedWorld::VoxelNetClient_GetPendingRegionDownloads() const {
	// todo this is not thread safe
	if(VoxelNetClient_VoxelClient.IsValid()){
		return VoxelNetClient_VoxelClient.Get()->remainingRegionsToDownload;
	}
	return -1;
}

void APagedWorld::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(APagedWorld, WorldSeed);
}

