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
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldGenInterpreters.h"
#include "PagedWorld.generated.h"

class APagedRegion;
class UTerrainPagingComponent;

#define REGION_SIZE 32
#define VOXEL_SIZE 100
#define MAX_MATERIALS 4
#define MARCHING_CUBES 1
#define ASYNC_COLLISION true

#define DB_NAME "WorldDatabase"


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

	PolyVox::MaterialDensityPair88 voxel[32][32][32]; 
};


UCLASS()
class MCTPLUGIN_API APagedWorld : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	APagedWorld();
	~APagedWorld();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void PostInitializeComponents() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	UFUNCTION(BlueprintImplementableEvent) void GetNoiseGenerators(UUFNNoiseGenerator* &material,UUFNNoiseGenerator* &heightmap,UUFNNoiseGenerator* &biome);

	// The procedurally generated mesh that represents our voxels
	UPROPERTY(Category = "Voxel World", BlueprintReadOnly, VisibleAnywhere)
	TMap<FIntVector, APagedRegion*> regions;

	UPROPERTY(Category = "Voxel World", BlueprintReadOnly, VisibleAnywhere)
		TArray<UTerrainPagingComponent*> pagingComponents;

	UFUNCTION(Category = "Voxel World", BlueprintCallable) void RegisterPagingComponent(UTerrainPagingComponent* pagingComponent);

	UPROPERTY(Category = "Voxel Terrain - Terrain Settings", BlueprintReadWrite, EditAnywhere) TArray<UMaterialInterface*> TerrainMaterials;

	UFUNCTION(Category = "Voxel World", BlueprintCallable) APagedRegion* getRegionAt(FIntVector pos);

	UFUNCTION(Category = "Voxel World", BlueprintCallable) void QueueRegionRender(FIntVector pos);

	UFUNCTION(Category = "Voxel World", BlueprintCallable) void MarkRegionDirtyAndAdjacent(FIntVector pos);

	UFUNCTION(Category = "Voxel World", BlueprintCallable)void GenerateWorldRadius(FIntVector pos, int32 radius);

	UFUNCTION(Category = "Voxel World", BlueprintCallable)void LoadOrGenerateWorldRadius(FIntVector pos, int32 radius);

	//debug mod terrain
	UFUNCTION(Category = "Voxel Terrain", BlueprintCallable) bool ModifyVoxel(FIntVector pos, uint8 r, uint8 m, uint8 d, bool bIsSpherical);


	UFUNCTION(Category = "Voxel Coordinates", BlueprintCallable, BlueprintPure)
	static FIntVector VoxelToRegionCoords(FIntVector voxel);

	UFUNCTION(Category = "Voxel Coordinates", BlueprintCallable, BlueprintPure)
	static FIntVector WorldToVoxelCoords(FVector world);

	UFUNCTION(Category = "Voxel World", BlueprintCallable) void beginWorldGeneration(FIntVector pos);

	UPROPERTY(Category = "Voxel World", BlueprintReadOnly, VisibleAnywhere) int32 remainingRegionsToGenerate = 0;

	UFUNCTION(Category = "Voxel Terrain - Volume Memory", BlueprintCallable) int32 getVolumeMemoryBytes();

	UFUNCTION(Category = "Voxel Terrain - Volume Memory", BlueprintCallable) void Flush();

	void SaveChunkToDatabase(leveldb::DB * db, FIntVector pos, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk * pChunk);
	bool ReadChunkFromDatabase(leveldb::DB * db, FIntVector pos, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk * pChunk);

public:
	TSharedPtr<PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>> VoxelVolume;

	TSet<FIntVector> dirtyRegions; // region keys which need redrawn; either because their voxels were modified or because they were just created
	TQueue<FVoxelUpdate, EQueueMode::Mpsc> voxelUpdateQueue;
	TQueue<FWorldGenerationTaskOutput, EQueueMode::Mpsc> worldGenerationQueue;

	FCriticalSection VolumeMutex;
	TQueue<FExtractionTaskOutput, EQueueMode::Mpsc> extractionQueue;

	leveldb::DB *worldDB;
};

class WorldPager : public PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Pager
{
public:
	// Constructor
	WorldPager(APagedWorld* World);

	// Destructor
	virtual ~WorldPager() {};

	// PagedVolume::Pager functions
	virtual void pageIn(const PolyVox::Region& region, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk* pChunk);
	virtual void pageOut(const PolyVox::Region& region, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk* pChunk);

	//FCriticalSection VolumeMutex;

	APagedWorld* world;
public:
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
		//PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk * pChunk;
	public:
		RegionGenerationTask(APagedWorld* world, FIntVector lower/*, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk * pChunk*/) {
			this->world = world;
			this->lower = lower;
			//this->pChunk = pChunk;
		}

		FORCEINLINE TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(RegionGenerationTask, STATGROUP_ThreadPoolAsyncTasks);
		}
		//pChunk->setVoxel(x + lower.X, y + lower.Y, z + lower.Z, v);//world pos
		void DoWork() {
			UUFNNoiseGenerator* material;
			UUFNNoiseGenerator* heightmap;
			UUFNNoiseGenerator* biome;
			
			world->GetNoiseGenerators(material, heightmap, biome);

			FWorldGenerationTaskOutput output;
			output.pos = lower;

			// generate
			for (int32 x = 0; x < REGION_SIZE; x++){
				for (int32 y = 0; y < REGION_SIZE; y++){
					for (int32 z = 0; z < REGION_SIZE; z++){ // todo save function ptr to interp as param that way we can change them on the fly
						output.voxel[x][y][z] = WorldGen::Interpret_Woods(x + lower.X, y + lower.Y, z + lower.Z, material, heightmap, biome);
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
