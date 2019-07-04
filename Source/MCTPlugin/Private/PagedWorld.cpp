#include "PagedWorld.h"
#include "WorldGenInterpreters.h"
#include "PagedRegion.h"
#include "TerrainPagingComponent.h"
#include "leveldb/filter_policy.h"
#include "leveldb/cache.h"

#ifdef WORLD_TICK_TRACKING
DECLARE_CYCLE_STAT(TEXT("World Process New Regions"), STAT_WorldNewRegions, STATGROUP_VoxelWorld);
DECLARE_CYCLE_STAT(TEXT("World Do Voxel Updates"), STAT_WorldVoxelUpdates, STATGROUP_VoxelWorld);
DECLARE_CYCLE_STAT(TEXT("World Process Dirty Regions"), STAT_WorldDirtyRegions, STATGROUP_VoxelWorld);
DECLARE_CYCLE_STAT(TEXT("World Clear Extraction Queue"), STAT_WorldClearExtractionQueue, STATGROUP_VoxelWorld);
#endif

// Sets default values
APagedWorld::APagedWorld() {
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

APagedWorld::~APagedWorld() {
	VoxelVolume.Reset();
	UE_LOG(LogTemp, Warning, TEXT("Saving world database..."));
	delete worldDB;
	UE_LOG(LogTemp, Warning, TEXT("World database saved."));
}

// Called when the game starts or when spawned
void APagedWorld::BeginPlay() {
	Super::BeginPlay();

	leveldb::Options options;
	options.create_if_missing = true;

#ifdef DATABASE_OPTIMIZATIONS
	//options.write_buffer_size = 120 * 1048576;
	options.block_cache = leveldb::NewLRUCache(8 * 1048576);
	options.filter_policy = leveldb::NewBloomFilterPolicy(10);
#endif

	FString dbname = FPaths::ProjectSavedDir() + DB_NAME;

	leveldb::Status status = leveldb::DB::Open(options, std::string(TCHAR_TO_UTF8(*dbname)), &worldDB);

	bool b = status.ok();
	assert(status.ok());

	UE_LOG(LogTemp, Warning, TEXT("Database connection to %s: %d"), *dbname, b);

	TArray<uint8> versionArchive;

	if(LoadGlobalDataFromDatabase(worldDB, DB_VERSION_TAG, versionArchive)) {
		FMemoryReader versionReader(versionArchive);
		int32 db_version;
		versionReader << db_version; // read version if one existed
		versionReader.FlushCache();
		versionReader.Close();
		UE_LOG(LogTemp, Warning, TEXT("Database version for %s: %d. Expected %d."), *dbname, db_version, DB_VERSION);
		assert(db_version == DB_VERSION);
	} else {
		FBufferArchive version;
		int32 dbVersion = DB_VERSION;
		version << dbVersion;
		SaveGlobalDataToDatabase(worldDB, DB_VERSION_TAG, version);
	}

}

// Called every frame
void APagedWorld::Tick(float DeltaTime) {
	Super::Tick(DeltaTime);

#ifdef WORLD_TICK_TRACKING
	{ SCOPE_CYCLE_COUNTER(STAT_WorldNewRegions);
#endif

	int NewRegions = 0;

	VolumeMutex.Lock();
	// pop render queue
	// queue is multi input single consumer
	while (!worldGenerationQueue.IsEmpty()) {
		// doing x per tick reduces hitches by a good amount, but causes slower loading times

		FWorldGenerationTaskOutput gen;
		worldGenerationQueue.Dequeue(gen);


		for (int32 x = 0; x < REGION_SIZE; x++) {
			for (int32 y = 0; y < REGION_SIZE; y++) { for (int32 z = 0; z < REGION_SIZE; z++) { VoxelVolume->setVoxel(x + gen.pos.X, y + gen.pos.Y, z + gen.pos.Z, gen.voxel[x][y][z]); } }
		}
		remainingRegionsToGenerate--;
	}
#ifdef WORLD_TICK_TRACKING
	}
	{ SCOPE_CYCLE_COUNTER(STAT_WorldVoxelUpdates);
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
						int32 n = update.radius / 2;
						int32 nx = x - n;
						int32 ny = y - n;
						int32 nz = z - n;

						auto oldMaterial = VoxelVolume->getVoxel(update.origin.X + nx, update.origin.Y + ny, update.origin.Z + nz).getMaterial();

						VoxelVolume->setVoxel(update.origin.X + nx, update.origin.Y + ny, update.origin.Z + nz, PolyVox::MaterialDensityPair88(update.material, update.density));
						MarkRegionDirtyAndAdjacent(VoxelToRegionCoords(FIntVector(update.origin.X + nx, update.origin.Y + ny, update.origin.Z + nz)));

						if(VoxelWorldUpdate_Event.IsBound()) {
							VoxelWorldUpdate_Event.Broadcast(update.causeActor, update.origin, oldMaterial, update.material);
						}

					}
				}
			}
		}
		catch (...) {
			UE_LOG(LogTemp, Warning, TEXT("Caught exception in voxelUpdateQueue processing."));
			continue;
		}
	}
#ifdef WORLD_TICK_TRACKING
	}
	{ SCOPE_CYCLE_COUNTER(STAT_WorldDirtyRegions);
#endif
	VolumeMutex.Unlock();

	auto dirtyClone = dirtyRegions;
	dirtyRegions.Reset(); // leave slack
	for (auto& region : dirtyClone) {
		// if we render unloaded regions we get cascading world gen
		if (regions.Contains(region))
			QueueRegionRender(region);
	}

#ifdef WORLD_TICK_TRACKING
	}
	{ SCOPE_CYCLE_COUNTER(STAT_WorldClearExtractionQueue);
#endif
	while (!extractionQueue.IsEmpty()) {
		FExtractionTaskOutput gen;
		extractionQueue.Dequeue(gen);
		auto reg = getRegionAt(gen.region);
		reg->RenderParsed(gen);
		reg->UpdateNavigation();
	}

#ifdef WORLD_TICK_TRACKING
	}
#endif
}

void APagedWorld::PostInitializeComponents() {

	VoxelVolume = MakeShareable(new PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>(new WorldPager(this))); //,256*1024*1024,REGION_SIZE));

	// Call the base class's function.
	Super::PostInitializeComponents();
}


void APagedWorld::RegisterPagingComponent(UTerrainPagingComponent* pagingComponent) { pagingComponents.AddUnique(pagingComponent); }

APagedRegion* APagedWorld::getRegionAt(FIntVector pos) {
	if (regions.Contains(pos))
		return regions.FindRef(pos);

	//FActorSpawnParameters param;
	FVector fpos = FVector(pos);

	//UE_LOG(LogTemp, Warning, TEXT("Spawning a new region actor at %s."), *pos.ToString());


	try {
		APagedRegion* region = GetWorld()->SpawnActor<APagedRegion>(APagedRegion::StaticClass(), fpos, FRotator::ZeroRotator);
		region->world = this;
		regions.Add(pos, region);

		MarkRegionDirtyAndAdjacent(pos);
		return region;
	}
	catch (...) {
		//UE_LOG(LogTemp, Warning, TEXT("Exception spawning a new region actor at %s."), *pos.ToString());
		return nullptr;
	}
}

void APagedWorld::QueueRegionRender(FIntVector pos) { (new FAutoDeleteAsyncTask<ExtractionThread::ExtractionTask>(this, pos))->StartBackgroundTask(); }

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

bool APagedWorld::ModifyVoxel(FIntVector pos, uint8 r, uint8 m, uint8 d, bool bIsSpherical) {
	voxelUpdateQueue.Enqueue(FVoxelUpdate(pos,r,m,d,nullptr,bIsSpherical));
	return true;
}

FIntVector APagedWorld::VoxelToRegionCoords(FIntVector voxel) {
	FVector tmp = FVector(voxel) / (float)REGION_SIZE;
	return FIntVector(FMath::FloorToInt(tmp.X), FMath::FloorToInt(tmp.Y), FMath::FloorToInt(tmp.Z)) * 32;
}

FIntVector APagedWorld::WorldToVoxelCoords(FVector world) { return FIntVector(world / VOXEL_SIZE); }

void APagedWorld::beginWorldGeneration(FIntVector pos) {
	remainingRegionsToGenerate++;
	(new FAutoDeleteAsyncTask<WorldGenThread::RegionGenerationTask>(this, pos))->StartBackgroundTask();
}

int32 APagedWorld::getVolumeMemoryBytes() const { return VoxelVolume.Get()->calculateSizeInBytes(); }

void APagedWorld::Flush() const { VoxelVolume.Get()->flushAll(); }

void APagedWorld::PagingComponentTick() {
	TSet<FIntVector> regionsToLoad;

	for (auto& pager : pagingComponents) {
		int radius = pager->viewDistance;
		FIntVector pos = VoxelToRegionCoords(WorldToVoxelCoords(pager->GetOwner()->GetActorLocation()));

		for (int z = -radius; z <= radius; z++) {
			// top down makes it feel faster
			for (int y = -radius; y <= radius; y++) {
				for (int x = -radius; x <= radius; x++) {
					FIntVector surrounding = pos + FIntVector(REGION_SIZE * x, REGION_SIZE * y, -REGION_SIZE * z); // -z means we gen higher regions first?

					regionsToLoad.Emplace(surrounding);
				}
			}
		}
	}

	UnloadRegionsExcept(regionsToLoad);
}

void APagedWorld::UnloadRegionsExcept(TSet<FIntVector> regionsToLoad) {
	TArray<FIntVector> currentRegionsArr;
	int num = regions.GetKeys(currentRegionsArr);
	TSet<FIntVector> currentRegions(currentRegionsArr);

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


WorldPager::WorldPager(APagedWorld* World)
	: world(World) {
}

void WorldPager::pageIn(const PolyVox::Region& region, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk* pChunk) {
	FIntVector pos = FIntVector(region.getLowerX(), region.getLowerY(), region.getLowerZ());

	bool regionExists = world->ReadChunkFromDatabase(world->worldDB, pos, pChunk);

	if (!regionExists) {
		world->beginWorldGeneration(pos);
	}

	return;
}


void WorldPager::pageOut(const PolyVox::Region& region, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk* pChunk) {
	FIntVector pos = FIntVector(region.getLowerX(), region.getLowerY(), region.getLowerZ());

#ifndef DONT_SAVE
	world->SaveChunkToDatabase(world->worldDB, pos, pChunk);
#endif
	//UE_LOG(LogTemp, Warning, TEXT("[db] Saved region to db:  %s."), *pos.ToString());
}

std::string ArchiveToString(TArray<uint8>& archive) { return std::string((char*)archive.GetData(), archive.Num()); }


void ArchiveFromString(std::string input, TArray<uint8>& archive) {
	int len = input.length();

	if (archive.Num() > 0)
		archive.Empty(len);
	archive.AddZeroed(len);

	for (int i = 0; i < len; i++) { archive[i] = (unsigned char)input[i]; }
	//memcpy((void*)s, archive.GetData(), len); // both leave the zeroes , need to investigate
	//FGenericPlatformMemory::Memcpy((void*)input.c_str(),archive.GetData(), len);
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

		int n = 0; // this could be better if we were to calculate it from xy so it is independent of order

		for (char x = 0; x < REGION_SIZE; x++) {
			for (char y = 0; y < REGION_SIZE; y++) {
				auto uVoxel = pChunk->getVoxel(x, y, w);

				char mat = uVoxel.getMaterial(); // unsigned -> signed conversion
				char den = uVoxel.getDensity();

				byteBuf[n++] = mat;
				byteBuf[n++] = den;
				//UE_LOG(LogTemp, Warning, TEXT("[debug] m%d d%d  n%d"), mat,den,n);
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

				//auto voxel = PolyVox::MaterialDensityPair88(mat,den);
				if(mat != 0) containsNonZero = true;
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


/// spiral loading 
//
//
//for (int i = -rad; i < rad; i++) {
//	doSpawn(i, -rad);
//	doSpawn(i, rad-1);
//	doSpawn(-rad, i);
//	doSpawn(rad-1, i);
//}
