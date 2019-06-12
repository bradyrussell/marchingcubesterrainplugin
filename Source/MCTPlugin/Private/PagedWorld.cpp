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
}

// Called when the game starts or when spawned
void APagedWorld::BeginPlay()
{
	Super::BeginPlay();
}

// Called every frame
void APagedWorld::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// pop render queue
	// queue is multi input single consumer
	if (!worldGenerationQueue.IsEmpty()) { // doing one per tick reduces hitches by a good amount

		FWorldGenerationTaskOutput gen;
		worldGenerationQueue.Dequeue(gen);

		for (int32 x = 0; x < 32; x++) {
			for (int32 y = 0; y < 32; y++) {
				for (int32 z = 0; z < 32; z++) {
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

						VoxelVolume->setVoxel(update.origin.X + nx, update.origin.Y + ny, update.origin.Z + nz, PolyVox::MaterialDensityPair88(update.material, update.density));
						MarkRegionDirtyAndAdjacent(VoxelToRegionCoords(FIntVector(update.origin.X + nx, update.origin.Y + ny, update.origin.Z + nz)));
						//dirtyRegions.Emplace(VoxelToRegionCoords(FIntVector(update.origin.X + nx, update.origin.Y + ny, update.origin.Z + nz))); //proper
						//dirtyRegions.Emplace(VoxelToRegionCoords(FIntVector(update.origin.X + (2*nx), update.origin.Y + (2*ny), update.origin.Z + (2*nz)))); //hack
					}
				}
			}
		}
		catch (...) {
			UE_LOG(LogTemp, Warning, TEXT("Caught exception in voxelUpdateQueue processing."));
			continue;
		}
	}

	// do region updates
	for (auto& region : dirtyRegions){ // if we render unloaded regions we get cascading world gen
		if(regions.Contains(region)) getRegionAt(region)->Render();
	}
	dirtyRegions.Empty();
}

void APagedWorld::PostInitializeComponents() {

	VoxelVolume = MakeShareable(new PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>(new WorldPager(this)));

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

	UE_LOG(LogTemp, Warning, TEXT("Spawning a new region actor at %s."),*pos.ToString());
	APagedRegion * region = GetWorld()->SpawnActor<APagedRegion>(APagedRegion::StaticClass(), FTransform(FRotator(0, 0, 0),FVector(pos),FVector(1,1,1)));
	region->world = this;
	regions.Add(pos,region);
	return region;
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
	for (int x = -radius; x <= radius; x++) {
		for (int y = -radius; y <= radius; y++) {
			for (int z = -radius; z <= radius; z++) {
				FIntVector surrounding = pos + FIntVector(REGION_SIZE*x, REGION_SIZE*y, REGION_SIZE*z);
				if (!regions.Contains(surrounding)) beginWorldGeneration(surrounding);
			}
		}
	}
}

bool APagedWorld::ModifyVoxel(FIntVector pos, uint8 r, uint8 m, uint8 d)
{
	FVoxelUpdate update;

	update.origin = pos;
	update.material = m;
	update.density = d;
	update.radius = r;

	voxelUpdateQueue.Enqueue(update);

	//UE_LOG(LogTemp, Warning, TEXT("Modifying voxel at %s to (%d,%d)*%d."), *pos.ToString(),m,d,r);
	return true;
}

FIntVector APagedWorld::VoxelToRegionCoords(FIntVector voxel)
{
	FVector tmp = FVector(voxel) / 32.0;
	return FIntVector(FMath::FloorToInt(tmp.X), FMath::FloorToInt(tmp.Y), FMath::FloorToInt(tmp.Z))*32;
}

FIntVector APagedWorld::WorldToVoxelCoords(FVector world)
{
	return FIntVector(world/100);
}

void APagedWorld::beginWorldGeneration(FIntVector pos)
{
	getRegionAt(pos); // create the actor
	(new FAutoDeleteAsyncTask<WorldGenThread::RegionGenerationTask>(this, pos))->StartBackgroundTask();
}


WorldPager::WorldPager(APagedWorld *World) :world(World)
{
}

void WorldPager::pageIn(const PolyVox::Region & region, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk * pChunk)
{
	FIntVector pos = FIntVector(region.getLowerX(), region.getLowerY(), region.getLowerZ());

	if (!world->regions.Contains(pos)) {
		//UE_LOG(LogTemp, Warning, TEXT("Paging in actorless region at %s."), *pos.ToString()); // no idea why this happens
	}
	else {
		//UE_LOG(LogTemp, Warning, TEXT("Paging in ok region at %s."), *pos.ToString());
	}


	//world->beginWorldGeneration(pos);

	return;
}

void WorldPager::pageOut(const PolyVox::Region & region, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk * pChunk)
{

//	PolyVox::saveVolume();
}

//void WorldPager::GenerateNewChunk(const PolyVox::Region & region, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk * pChunk) {
//	UUFNNoiseGenerator* material;
//	UUFNNoiseGenerator* heightmap;
//	UUFNNoiseGenerator* biome;
//
//	world->GetNoiseGenerators(material, heightmap, biome);
//
//	PolyVox::MaterialDensityPair88 Voxel;
//
//	if (!(IsValid(material) && IsValid(heightmap) && IsValid(biome))) {
//		
//		for (int32 x = region.getLowerX(); x <= region.getUpperX(); x++)
//		{
//			for (int32 y = region.getLowerY(); y <= region.getUpperY(); y++)
//			{
//				for (int32 z = region.getLowerZ(); z <= region.getUpperZ(); z++)
//				{
//					Voxel.setMaterial(((abs(x+y+z)) % 3) + 1);
//					Voxel.setDensity(250);
//
//					if (z > 30) {
//						Voxel.setMaterial(0);
//						Voxel.setDensity(0);
//					}
//
//					pChunk->setVoxel(x - region.getLowerX(), y - region.getLowerY(), z - region.getLowerZ(), Voxel);
//				}
//			}
//		}
//		return;
//	}
//
//	// generate
//	for (int32 x = region.getLowerX(); x <= region.getUpperX(); x++)
//	{
//		for (int32 y = region.getLowerY(); y <= region.getUpperY(); y++)
//		{
//			for (int32 z = region.getLowerZ(); z <= region.getUpperZ(); z++)
//			{
//				//auto v = WorldGen::Interpret_AlienSpires(x, y, z, material, heightmap, biome);
//				auto v = WorldGen::Interpret_Basic(x, y, z, material, heightmap, biome);
//
//				pChunk->setVoxel(x - region.getLowerX(), y - region.getLowerY(), z - region.getLowerZ(), v);
//			}
//		}
//	}
//
//	return;
//}
