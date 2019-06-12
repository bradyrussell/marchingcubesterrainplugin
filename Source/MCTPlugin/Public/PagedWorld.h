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
//#include "PagedRegion.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldGenInterpreters.h"
#include "PagedWorld.generated.h"

class APagedRegion;
class UTerrainPagingComponent;

#define REGION_SIZE 32
#define VOXEL_SIZE 100


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
};

USTRUCT(BlueprintType)
struct FWorldGenerationTaskOutput // 
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "WorldGen Task")
	FIntVector pos;

	PolyVox::MaterialDensityPair88 voxel[32][32][32]; // ~ 500 kb
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

	UFUNCTION(Category = "Voxel World", BlueprintCallable) void MarkRegionDirtyAndAdjacent(FIntVector pos);

	UFUNCTION(Category = "Voxel World", BlueprintCallable)void GenerateWorldRadius(FIntVector pos, int32 radius);

	//debug mod terrain
	UFUNCTION(Category = "Voxel Terrain", BlueprintCallable) bool ModifyVoxel(FIntVector pos, uint8 r, uint8 m, uint8 d);


	UFUNCTION(Category = "Voxel Coordinates", BlueprintCallable, BlueprintPure)
	static FIntVector VoxelToRegionCoords(FIntVector voxel);

	UFUNCTION(Category = "Voxel Coordinates", BlueprintCallable, BlueprintPure)
	static FIntVector WorldToVoxelCoords(FVector world);

	UFUNCTION(Category = "Voxel World", BlueprintCallable) void beginWorldGeneration(FIntVector pos);


public:
	TSharedPtr<PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>> VoxelVolume;

	TSet<FIntVector> dirtyRegions; // region keys which need redrawn; either because their voxels were modified or because they were just created
	TQueue<FVoxelUpdate, EQueueMode::Mpsc> voxelUpdateQueue;
	TQueue<FWorldGenerationTaskOutput, EQueueMode::Mpsc> worldGenerationQueue;
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

	void GenerateNewChunk(const PolyVox::Region & region, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk * pChunk);

	FCriticalSection VolumeMutex;

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
			for (int32 x = 0; x < 32; x++){
				for (int32 y = 0; y < 32; y++){
					for (int32 z = 0; z < 32; z++){
						output.voxel[x][y][z] = WorldGen::Interpret_AlienSpires(x + lower.X, y + lower.Y, z + lower.Z, material, heightmap, biome);
					}
				}
			}

			world->worldGenerationQueue.Enqueue(output);
		}
	};
	////////////////////////////////////////////////////////////////////////
};