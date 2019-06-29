#pragma once

#include "UnrealFastNoisePlugin.h"
#include "UnrealFastNoisePlugin/Public/UFNNoiseGenerator.h"
#include "PolyVox/PagedVolume.h"
#include "PolyVox/MaterialDensityPair.h"
#include "PolyVox/Vector.h"
#include "PolyVox/MarchingCubesSurfaceExtractor.h"
#include "PolyVox/Mesh.h"
#include "GameFramework/Actor.h"
#include "RuntimeMeshComponent.h"
#include "LevelDatabase.h"
#include "BufferArchive.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldGenInterpreters.h"
#include "PagedWorld.generated.h"

// voxel config
#define REGION_SIZE 32 //voxels
#define VOXEL_SIZE 100 // cm
#define MAX_MATERIALS 4
#define MARCHING_CUBES 1
#define ASYNC_COLLISION false//!WITH_EDITOR//false
//#define REGION_UNLOAD_DELAY 10 //seconds

// db config
#define DB_NAME "WorldDatabase"
#define DB_GLOBAL_TAG "MapGlobalData_" // 14 bytes or more so we dont conflict with region keys

// regional data offsets, max of 255 - REGION_SIZE 
#define REGIONAL_DATA_RESERVED 0
#define REGIONAL_DATA_ENTITY 1
#define REGIONAL_DATA_CONTAINER 2
#define REGIONAL_DATA_RESOURCES 3

#define REGIONAL_DATA_MAX 255-REGION_SIZE // 223 for VOXEL_SIZE of 32

//end config

class APagedRegion;
class UTerrainPagingComponent;

USTRUCT(BlueprintType)
struct FExtractionTaskSection // results of surface extraction and decoding, to be plugged into updatemesh
{
	GENERATED_BODY()
	TArray<FVector> Vertices = TArray<FVector>();
	TArray<FVector> Normals = TArray<FVector>();
	TArray<FRuntimeMeshTangent> Tangents = TArray<FRuntimeMeshTangent>();
	TArray<FColor> Colors = TArray<FColor>();
	TArray<FVector2D> UV0 = TArray<FVector2D>();
	TArray<int32> Indices = TArray<int32>();
};

USTRUCT(BlueprintType)
struct FExtractionTaskOutput // results of surface extraction and decoding, to be plugged into updatemesh
{
	GENERATED_BODY()
		FIntVector region;
	TArray<FExtractionTaskSection> section = TArray<FExtractionTaskSection>();
};

USTRUCT(BlueprintType)
struct FVoxelUpdate // a change of a group of voxels from any type to a single new type
{
	GENERATED_BODY()
		UPROPERTY(BlueprintReadWrite, Category = "Voxel Update")
		FIntVector origin;
		UPROPERTY(BlueprintReadWrite, Category = "Voxel Update")
		uint8 radius;
		UPROPERTY(BlueprintReadWrite, Category = "Voxel Update")
		uint8 material;
		UPROPERTY(BlueprintReadWrite, Category = "Voxel Update")
		uint8 density;
		//UPROPERTY(BlueprintReadWrite, Category = "Voxel Update")
		//bool bShouldDrop;
		UPROPERTY(BlueprintReadWrite, Category = "Voxel Update")
		bool bIsSpherical;
};

USTRUCT(BlueprintType)
struct FWorldGenerationTaskOutput // 
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "WorldGen Task")
	FIntVector pos;

	PolyVox::MaterialDensityPair88 voxel[REGION_SIZE][REGION_SIZE][REGION_SIZE];
};


UCLASS()
class MCTPLUGIN_API APagedWorld : public AActor
{
	GENERATED_BODY()

public:
	APagedWorld();
	~APagedWorld();

protected:
	virtual void BeginPlay() override;
	virtual void PostInitializeComponents() override;

public:
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(Category = "Voxel World", BlueprintReadOnly, VisibleAnywhere) TMap<FIntVector, APagedRegion*> regions;
	UPROPERTY(Category = "Voxel World", BlueprintReadOnly, VisibleAnywhere) TArray<UTerrainPagingComponent*> pagingComponents;
	UPROPERTY(Category = "Voxel World", BlueprintReadWrite, EditAnywhere) TArray<UMaterialInterface*> TerrainMaterials;
	UPROPERTY(Category = "Voxel World", BlueprintReadOnly, VisibleAnywhere) int32 remainingRegionsToGenerate = 0;

	// get region
	UFUNCTION(Category = "Voxel World", BlueprintCallable) APagedRegion* getRegionAt(FIntVector pos);

	UFUNCTION(Category = "Voxel World", BlueprintCallable) void RegisterPagingComponent(UTerrainPagingComponent* pagingComponent);

	//render
	//UFUNCTION(Category = "Voxel World", BlueprintCallable) void QueueRegionRender(FIntVector pos);
	UFUNCTION(Category = "Voxel World", BlueprintCallable) void MarkRegionDirtyAndAdjacent(FIntVector pos);

	// terrain modification
	UFUNCTION(Category = "Voxel World", BlueprintCallable) bool ModifyVoxel(FIntVector pos, uint8 r, uint8 m, uint8 d, bool bIsSpherical);

	// coordinates
	UFUNCTION(Category = "Voxel Coordinates", BlueprintCallable, BlueprintPure) static FIntVector VoxelToRegionCoords(FIntVector voxel);
	UFUNCTION(Category = "Voxel Coordinates", BlueprintCallable, BlueprintPure) static FIntVector WorldToVoxelCoords(FVector world);
	
	// world gen
	UFUNCTION(BlueprintImplementableEvent) TArray<UUFNNoiseGenerator*>  GetNoiseGeneratorArray();
	UFUNCTION(Category = "Voxel World", BlueprintCallable) void beginWorldGeneration(FIntVector pos);
	UFUNCTION(Category = "Voxel World", BlueprintCallable) void GenerateWorldRadius(FIntVector pos, int32 radius);
	UFUNCTION(Category = "Voxel World", BlueprintCallable) void TouchOrSpawnRadius(FIntVector pos, int32 radius);
	UFUNCTION(Category = "Voxel World", BlueprintCallable) void LoadOrGenerateWorldRadius(FIntVector pos, int32 radius);

	// memory
	UFUNCTION(Category = "Voxel World - Volume Memory", BlueprintCallable) int32 getVolumeMemoryBytes();
	UFUNCTION(Category = "Voxel World - Volume Memory", BlueprintCallable) void Flush();

	UFUNCTION(Category = "Voxel World", BlueprintCallable) void UnloadOldRegions();

	UFUNCTION(Category = "Voxel World", BlueprintCallable) void PagingComponentTick();
	UFUNCTION(Category = "Voxel World", BlueprintCallable) void UnloadRegionsExcept(TSet<FIntVector> loadedRegions);

	// save the actual voxel data to leveldb , stored under region coords X Y Z W where w 2kb is xy layers
	void SaveChunkToDatabase(leveldb::DB * db, FIntVector pos, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk * pChunk);
	bool ReadChunkFromDatabase(leveldb::DB * db, FIntVector pos, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk * pChunk);

	// save region-specific data to leveldb, for instance entities in the region or special local properties. 
	// these are separated by indicies based on categories , ideally with decreasing order of relevance as they are ordered by index
	void SaveRegionalDataToDatabase(leveldb::DB * db, FIntVector pos, uint8 index, TArray<uint8> &archive);
	bool LoadRegionalDataFromDatabase(leveldb::DB * db, FIntVector pos, uint8 index, TArray<uint8> &archive);

	//save map-wide data to leveldb. this is still map specific so universal properties should have their own db. because we prepend a 14 byte tag, the key can be anything including ""
	void SaveGlobalDataToDatabase(leveldb::DB * db, std::string key, TArray<uint8> &archive);
	bool LoadGlobalDataFromDatabase(leveldb::DB * db, std::string key, TArray<uint8> &archive);

public:
	TSharedPtr<PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>> VoxelVolume;
	// todo make queue
	TSet<FIntVector> dirtyRegions; // region keys which need redrawn; either because their voxels were modified or because they were just created
	TQueue<FVoxelUpdate, EQueueMode::Mpsc> voxelUpdateQueue;
	TQueue<FWorldGenerationTaskOutput, EQueueMode::Mpsc> worldGenerationQueue;
	//TQueue<FIntVector, EQueueMode::Mpsc> dirtyRegions;

	FCriticalSection VolumeMutex;
	TQueue<FExtractionTaskOutput, EQueueMode::Mpsc> extractionQueue;

	//TSet<FIntVector> regionsOnDisk;

	leveldb::DB *worldDB;
};

class WorldPager : public PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Pager
{
public:
	WorldPager(APagedWorld* World);
	virtual ~WorldPager() {};

	virtual void pageIn(const PolyVox::Region& region, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk* pChunk);
	virtual void pageOut(const PolyVox::Region& region, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk* pChunk);

	APagedWorld* world;
};

// Bridge between PolyVox Vector3DFloat and Unreal Engine 4 FVector
struct FPolyVoxVector : public FVector
{
	FORCEINLINE FPolyVoxVector()
	{}

	explicit FORCEINLINE FPolyVoxVector(EForceInit E)
		: FVector(E)
	{}

	FORCEINLINE FPolyVoxVector(float InX, float InY, float InZ)
		: FVector(InX, InY, InX)
	{}

	FORCEINLINE FPolyVoxVector(const FVector &InVec)
	{
		FVector::operator=(InVec);
	}

	FORCEINLINE FPolyVoxVector(const PolyVox::Vector3DFloat &InVec)
	{
		FPolyVoxVector::operator=(InVec);
	}

	FORCEINLINE FVector& operator=(const PolyVox::Vector3DFloat& Other)
	{
		this->X = Other.getX();
		this->Y = Other.getY();
		this->Z = Other.getZ();

		DiagnosticCheckNaN();

		return *this;
	}
};

namespace WorldGenThread { 
	////////////////////////////////////////////////////////////////////////
	class RegionGenerationTask : public FNonAbandonableTask {
		friend class FAutoDeleteAsyncTask<RegionGenerationTask>;
		APagedWorld* world;
		FIntVector lower;
	public:
		RegionGenerationTask(APagedWorld* world, FIntVector lower/*, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk * pChunk*/) {
			this->world = world;
			this->lower = lower;
		}

		FORCEINLINE TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(RegionGenerationTask, STATGROUP_ThreadPoolAsyncTasks);
		}

		void DoWork() {
			TArray<UUFNNoiseGenerator*> noise = world->GetNoiseGeneratorArray();
			
			FWorldGenerationTaskOutput output;
			output.pos = lower;

			// generate
			for (int32 x = 0; x < REGION_SIZE; x++){
				for (int32 y = 0; y < REGION_SIZE; y++){
					for (int32 z = 0; z < REGION_SIZE; z++){ // todo save function ptr to interp as param that way we can change them on the fly
						output.voxel[x][y][z] = WorldGen::Interpret_Mars(x + lower.X, y + lower.Y, z + lower.Z, noise);
					}
				}
			}

			world->worldGenerationQueue.Enqueue(output);
		}
	};
	////////////////////////////////////////////////////////////////////////
};

namespace ExtractionThread {
	////////////////////////////////////////////////////////////////////////
	class ExtractionTask : public FNonAbandonableTask {
		friend class FAutoDeleteAsyncTask<ExtractionTask>;
		APagedWorld* world;
		FIntVector lower;
	public:
		ExtractionTask(APagedWorld* world, FIntVector lower/*, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk * pChunk*/) {
			this->world = world;
			this->lower = lower;
		}

		FORCEINLINE TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(ExtractionTask, STATGROUP_ThreadPoolAsyncTasks);
		}

		void DoWork() {
			FExtractionTaskOutput output;
			output.section.AddDefaulted(MAX_MATERIALS);
			output.region = lower;

			PolyVox::Region ToExtract(PolyVox::Vector3DInt32(lower.X, lower.Y, lower.Z), PolyVox::Vector3DInt32(lower.X + REGION_SIZE, lower.Y + REGION_SIZE, lower.Z + REGION_SIZE));
			
			world->VolumeMutex.Lock();
			auto ExtractedMesh = PolyVox::extractMarchingCubesMesh(world->VoxelVolume.Get(), ToExtract);
			world->VolumeMutex.Unlock();

			auto decoded = PolyVox::decodeMesh(ExtractedMesh);

			//output.decoded = DecodedMesh;

			FVector OffsetLocation = FVector(lower);

			if (decoded.getNoOfIndices() == 0)
				return;


			for (int32 Material = 0; Material < world->TerrainMaterials.Num(); Material++)
			{
				// Loop over all of the triangle vertex indices
				for (uint32 i = 0; i < decoded.getNoOfIndices() - 2; i += 3)
				{
					// We need to add the vertices of each triangle in reverse or the mesh will be upside down
					auto Index = decoded.getIndex(i + 2);
					auto Vertex2 = decoded.getVertex(Index);
					auto TriangleMaterial = Vertex2.data.getMaterial();

					// Before we continue, we need to be sure that the triangle is the right material; we don't want to use verticies from other materials
					if (TriangleMaterial == (Material + 1))
					{
						// If it is of the same material, then we need to add the correct indices now
						output.section[Material].Indices.Add(output.section[Material].Vertices.Add((FPolyVoxVector(Vertex2.position) + OffsetLocation) * VOXEL_SIZE));

						Index = decoded.getIndex(i + 1);
						auto Vertex1 = decoded.getVertex(Index);
						output.section[Material].Indices.Add(output.section[Material].Vertices.Add((FPolyVoxVector(Vertex1.position) + OffsetLocation) * VOXEL_SIZE));

						Index = decoded.getIndex(i);
						auto Vertex0 = decoded.getVertex(Index);
						output.section[Material].Indices.Add(output.section[Material].Vertices.Add((FPolyVoxVector(Vertex0.position) + OffsetLocation) * VOXEL_SIZE));

						// Calculate the tangents of our triangle
						const FVector Edge01 = FPolyVoxVector(Vertex1.position - Vertex0.position);
						const FVector Edge02 = FPolyVoxVector(Vertex2.position - Vertex0.position);

						const FVector TangentX = Edge01.GetSafeNormal();
						FVector TangentZ = (Edge01 ^ Edge02).GetSafeNormal();

						for (int32 i = 0; i < 3; i++)
						{
							output.section[Material].Tangents.Add(FRuntimeMeshTangent(TangentX, false));
							output.section[Material].Normals.Add(TangentZ);
						}
					}
				}
			}
			//////////////////////////

			world->extractionQueue.Enqueue(output);
		}
	};
	////////////////////////////////////////////////////////////////////////
};
