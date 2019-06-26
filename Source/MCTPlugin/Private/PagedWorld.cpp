#include "PagedWorld.h"
#include "WorldGenInterpreters.h"
#include "PagedRegion.h"

// Sets default values
APagedWorld::APagedWorld()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

APagedWorld::~APagedWorld()
{
	VoxelVolume.Reset();
	delete worldDB;
}

// Called when the game starts or when spawned
void APagedWorld::BeginPlay()
{
	Super::BeginPlay();

	leveldb::Options options;
	options.create_if_missing = true;

	FString dbname = FPaths::ProjectSavedDir() + DB_NAME;

	leveldb::Status status = leveldb::DB::Open(options, std::string(TCHAR_TO_UTF8(*dbname)), &worldDB);

	bool b = status.ok();
	assert(status.ok());

	UE_LOG(LogTemp, Warning, TEXT("Database connection to %s: %d"), *dbname,b);
}

// Called every frame
void APagedWorld::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// pop render queue
	// queue is multi input single consumer
	if (!worldGenerationQueue.IsEmpty()) { // doing one per tick reduces hitches by a good amount, but also may cause more redraws due to dirty regions

		FWorldGenerationTaskOutput gen;
		worldGenerationQueue.Dequeue(gen);
		remainingRegionsToGenerate--;

		for (int32 x = 0; x < REGION_SIZE; x++) { 
			for (int32 y = 0; y < REGION_SIZE; y++) {
				for (int32 z = 0; z < REGION_SIZE; z++) {
					VoxelVolume->setVoxel(x + gen.pos.X, y + gen.pos.Y, z + gen.pos.Z, gen.voxel[x][y][z]);
				}
			}
		}

		//dirtyRegions.Emplace(gen.pos);
		MarkRegionDirtyAndAdjacent(gen.pos);
	}


	// also voxelmodify queue
	while (!voxelUpdateQueue.IsEmpty()) {

		FVoxelUpdate update;
		voxelUpdateQueue.Dequeue(update);

		try {
			//lock
			for (int32 x = 0; x < update.radius; x++) { // todo evaluate performance
				for (int32 y = 0; y < update.radius; y++) {  
					for (int32 z = 0; z < update.radius; z++) {

						int32 n = update.radius / 2;
						int32 nx = x - n;
						int32 ny = y - n;
						int32 nz = z - n;

						//if (!update.bIsSpherical || FVector::DistSquared(FVector(update.origin.X + nx, update.origin.Y + ny, update.origin.Z + nz), FVector(update.origin)) <= update.radius*update.radius) { // not the optimal sphere algo 

							VoxelVolume->setVoxel(update.origin.X + nx, update.origin.Y + ny, update.origin.Z + nz, PolyVox::MaterialDensityPair88(update.material, update.density));
							MarkRegionDirtyAndAdjacent(VoxelToRegionCoords(FIntVector(update.origin.X + nx, update.origin.Y + ny, update.origin.Z + nz)));
							//dirtyRegions.Emplace(VoxelToRegionCoords(FIntVector(update.origin.X + nx, update.origin.Y + ny, update.origin.Z + nz))); //proper
							//dirtyRegions.Emplace(VoxelToRegionCoords(FIntVector(update.origin.X + (2*nx), update.origin.Y + (2*ny), update.origin.Z + (2*nz)))); //hack
						//}
					}
				}
			}
		}
		catch (...) {
			UE_LOG(LogTemp, Warning, TEXT("Caught exception in voxelUpdateQueue processing."));
			continue;
		}
	}

	auto dirtyClone = dirtyRegions;
	dirtyRegions.Reset(); // leave slack
	// do region updates
	for (auto& region : dirtyClone){ // if we render unloaded regions we get cascading world gen
		if (regions.Contains(region)) getRegionAt(region)->SlowRender();
		//if (regions.Contains(region)) QueueRegionRender(region);
	}
	//dirtyRegions.Reset(); // leave slack

	//while (!extractionQueue.IsEmpty()) { // doing one per tick reduces hitches by a good amount
	//	FExtractionTaskOutput gen;
	//	extractionQueue.Dequeue(gen);

	//	getRegionAt(gen.region)->RenderParsed(gen);
	//}

}

void APagedWorld::PostInitializeComponents() {

	VoxelVolume = MakeShareable(new PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>(new WorldPager(this)));//,256*1024*1024,REGION_SIZE));

	// Call the base class's function.
	Super::PostInitializeComponents();
}


void APagedWorld::RegisterPagingComponent(UTerrainPagingComponent * pagingComponent)
{
	pagingComponents.AddUnique(pagingComponent);
}

APagedRegion * APagedWorld::getRegionAt(FIntVector pos)
{
	if (regions.Contains(pos)) return regions.FindRef(pos);

	//FActorSpawnParameters param;
	FVector fpos = FVector(pos);

	UE_LOG(LogTemp, Warning, TEXT("Spawning a new region actor at %s."), *pos.ToString());


	try {
		APagedRegion * region = GetWorld()->SpawnActor<APagedRegion>(APagedRegion::StaticClass(), fpos, FRotator::ZeroRotator);
		region->world = this;
		regions.Add(pos, region);
		return region;
	}
	catch (...) {
		UE_LOG(LogTemp, Warning, TEXT("Exception spawning a new region actor at %s."), *pos.ToString());
		return nullptr;
	}
}

 void APagedWorld::QueueRegionRender(FIntVector pos)
{
	 (new FAutoDeleteAsyncTask<ExtractionThread::ExtractionTask>(this, pos))->StartBackgroundTask();
}

void APagedWorld::MarkRegionDirtyAndAdjacent(FIntVector pos) {
	dirtyRegions.Emplace(pos);

	dirtyRegions.Emplace(pos+FIntVector(REGION_SIZE,0,0));
	dirtyRegions.Emplace(pos + FIntVector(0, REGION_SIZE, 0));
	dirtyRegions.Emplace(pos + FIntVector(0, 0, REGION_SIZE));

	dirtyRegions.Emplace(pos + FIntVector(-REGION_SIZE, 0, 0));
	dirtyRegions.Emplace(pos + FIntVector(0, -REGION_SIZE, 0));
	dirtyRegions.Emplace(pos + FIntVector(0, 0, -REGION_SIZE));
}


void APagedWorld::GenerateWorldRadius(FIntVector pos, int32 radius)
{
	for (int z = -radius; z <= radius; z++) { // top down makes it feel faster
		for (int y = -radius; y <= radius; y++) {
			for (int x = -radius; x <= radius; x++) {
				FIntVector surrounding = pos + FIntVector(REGION_SIZE*x, REGION_SIZE*y, -REGION_SIZE*z); // -z means we gen higher regions first?
				if (regionsOnDisk.Contains(surrounding)) {
					getRegionAt(pos); // spawn actor
					MarkRegionDirtyAndAdjacent(pos);
				} else if (!regions.Contains(surrounding)) beginWorldGeneration(surrounding);
			}
		}
	}
}

void APagedWorld::LoadOrGenerateWorldRadius(FIntVector pos, int32 radius)
{
	auto reg = PolyVox::Region(pos.X, pos.Y, pos.Z, pos.X + REGION_SIZE, pos.Y + REGION_SIZE, pos.Z + REGION_SIZE);
	reg.grow(radius*REGION_SIZE);
	VoxelVolume.Get()->prefetch(reg);
	GenerateWorldRadius(pos,radius);
}

bool APagedWorld::ModifyVoxel(FIntVector pos, uint8 r, uint8 m, uint8 d, bool bIsSpherical)
{
	FVoxelUpdate update;

	update.origin = pos;
	update.material = m;
	update.density = d;
	update.radius = r;
	update.bIsSpherical = bIsSpherical;

	voxelUpdateQueue.Enqueue(update);

	//UE_LOG(LogTemp, Warning, TEXT("Modifying voxel at %s to (%d,%d)*%d."), *pos.ToString(),m,d,r);
	return true;
}

FIntVector APagedWorld::VoxelToRegionCoords(FIntVector voxel)
{
	FVector tmp = FVector(voxel) / (float)REGION_SIZE;
	return FIntVector(FMath::FloorToInt(tmp.X), FMath::FloorToInt(tmp.Y), FMath::FloorToInt(tmp.Z))*32;
}

FIntVector APagedWorld::WorldToVoxelCoords(FVector world)
{
	return FIntVector(world/VOXEL_SIZE);
}

void APagedWorld::beginWorldGeneration(FIntVector pos)
{
	UE_LOG(LogTemp, Warning, TEXT("Beginning world generation for region at %s."), *pos.ToString());

	getRegionAt(pos); // create the actor
	remainingRegionsToGenerate++;
	(new FAutoDeleteAsyncTask<WorldGenThread::RegionGenerationTask>(this, pos))->StartBackgroundTask();
}

int32 APagedWorld::getVolumeMemoryBytes()
{
	return VoxelVolume.Get()->calculateSizeInBytes();
}

void APagedWorld::Flush()
{
	VoxelVolume.Get()->flushAll();
}


WorldPager::WorldPager(APagedWorld *World) :world(World)
{
}

void WorldPager::pageIn(const PolyVox::Region & region, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk * pChunk)
{
	FIntVector pos = FIntVector(region.getLowerX(), region.getLowerY(), region.getLowerZ());

	bool regionExists = world->ReadChunkFromDatabase(world->worldDB, pos, pChunk);

	if (regionExists) { // todo fix 
		UE_LOG(LogTemp, Warning, TEXT("[db] Paging in EXISTING region %s."), *pos.ToString());
		world->regionsOnDisk.Emplace(pos);
		//auto reg = world->getRegionAt(pos); // create the actor
		//world->MarkRegionDirtyAndAdjacent(pos); //concurrent access
	}
	//else {
	//	//UE_LOG(LogTemp, Warning, TEXT("[db] Paging in NON-EXISTANT region %s."), *pos.ToString());
	//}

	//if (!world->regions.Contains(pos)) {
	//	//UE_LOG(LogTemp, Warning, TEXT("Paging in actorless region at %s."), *pos.ToString()); // no idea why this happens // must be peeking, they dont save
	//}
	//else {
	////	UE_LOG(LogTemp, Warning, TEXT("Paging in ok region at %s."), *pos.ToString());
	//}

	//world->beginWorldGeneration(pos);

	return;
}


void WorldPager::pageOut(const PolyVox::Region & region, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk * pChunk)
{
	FIntVector pos = FIntVector(region.getLowerX(), region.getLowerY(), region.getLowerZ());
	world->SaveChunkToDatabase(world->worldDB, pos, pChunk);
	UE_LOG(LogTemp, Warning, TEXT("[db] Saved region to db:  %s."), *pos.ToString());
}


std::string SerializeLocationString(int32_t X, int32_t Y, int32_t Z, char W) {
	char byteBuf[13];
	uint32_t uX = X;
	uint32_t uY = Y;
	uint32_t uZ = Z;

	byteBuf[0] = uX;
	byteBuf[1] = uX >> 8;
	byteBuf[2] = uX >> 16;
	byteBuf[3] = uX >> 24;

	byteBuf[4] = uY;
	byteBuf[5] = uY >> 8;
	byteBuf[6] = uY >> 16;
	byteBuf[7] = uY >> 24;

	byteBuf[8] = uZ;
	byteBuf[9] = uZ >> 8;
	byteBuf[10] = uZ >> 16;
	byteBuf[11] = uZ >> 24;

	byteBuf[12] = W;
	return std::string(byteBuf, 13);
}


void APagedWorld::SaveChunkToDatabase(leveldb::DB * db, FIntVector pos, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk * pChunk)
{ // todo make batched write
	for (char w = 0; w < REGION_SIZE; w++) {
		char byteBuf[2*REGION_SIZE*REGION_SIZE]; // 2kb for 32^2

		int n = 0; // this could be better if we were to calculate it from xy so it is independent of order

		for (char x = 0; x < REGION_SIZE; x++) {
			for (char y = 0; y < REGION_SIZE; y++) {

				auto uVoxel = pChunk->getVoxel(x,y,w);

				char mat = uVoxel.getMaterial(); // unsigned -> signed conversion
				char den = uVoxel.getDensity();

				byteBuf[n++] = mat;
				byteBuf[n++] = den;
				//UE_LOG(LogTemp, Warning, TEXT("[debug] m%d d%d  n%d"), mat,den,n);
			}
		}

		db->Put(leveldb::WriteOptions(),SerializeLocationString(pos.X, pos.Y, pos.Z, w), std::string(byteBuf, 2*REGION_SIZE*REGION_SIZE));
	}
}

bool APagedWorld::ReadChunkFromDatabase(leveldb::DB* db, FIntVector pos, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk * pChunk)
{
	for (char w = 0; w < REGION_SIZE; w++) {
		std::string chunkData;
		auto status = db->Get(leveldb::ReadOptions(), SerializeLocationString(pos.X, pos.Y, pos.Z, w), &chunkData);

		if (status.IsNotFound()) {
			if(w > 0) UE_LOG(LogTemp, Warning, TEXT("Loading failed partway through %s region : failed at layer %d. Region data unrecoverable."), *pos.ToString(), w);
			return false;
		}

		int n = 0; // this could be better if we were to calculate it from xy so it is independent of order

		for (char x = 0; x < REGION_SIZE; x++) {
			for (char y = 0; y < REGION_SIZE; y++) {
				
				unsigned char mat = chunkData[n++]; // signed - > unsigned conversion
				unsigned char den = chunkData[n++];

				//auto voxel = PolyVox::MaterialDensityPair88(mat,den);

				pChunk->setVoxel(x, y, w, PolyVox::MaterialDensityPair88(mat, den));
			}
		}

	}

	return true;
}