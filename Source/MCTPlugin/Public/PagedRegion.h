#pragma once
#include "UnrealFastNoisePlugin.h"
#include "UnrealFastNoisePlugin/Public/UFNNoiseGenerator.h"

#include "PolyVox/PagedVolume.h"
#include "PolyVox/MaterialDensityPair.h"
#include "PolyVox/Vector.h"
#include "PolyVox/MarchingCubesSurfaceExtractor.h"
#include "PolyVox/Mesh.h"

#include "RuntimeMeshComponent.h"
#include "RuntimeMeshSection.h"

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PagedWorld.h"
#include "PagedRegion.generated.h"


UCLASS()
class MCTPLUGIN_API APagedRegion : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	APagedRegion();
	~APagedRegion();
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void PostInitializeComponents() override;
public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	//UFUNCTION(Category = "Voxel Terrain", BlueprintCallable) bool RenderChunk();

	UPROPERTY(Category = "Voxel Terrain", BlueprintReadWrite, VisibleAnywhere) class USceneComponent* Scene;

	// The procedurally generated mesh that represents our voxels
	UPROPERTY(Category = "Voxel Terrain", BlueprintReadOnly, VisibleAnywhere, Meta = (ExposeFunctionCategories = "Mesh,Rendering,Physics,Components|RuntimeMesh", AllowPrivateAccess = "true"))
	URuntimeMeshComponent* rMesh;

	//todo make better soln than this
	bool wasCreated[10] = { false,false,false,false,false,false,false,false,false,false };

	//TQueue<TArray<FExtractionTaskOutput>, EQueueMode::Mpsc> extractionResultQueue;

	UPROPERTY(Category = "Voxel Terrain - World", BlueprintReadWrite, EditAnywhere, meta = (ExposeOnSpawn = "true")) APagedWorld* world;

	UFUNCTION(Category = "Voxel Terrain", BlueprintCallable)void SlowRender();
	void RenderDecoded(PolyVox::Mesh<PolyVox::Vertex<PolyVox::MaterialDensityPair88>, unsigned int> decoded);

	void RenderParsed(FExtractionTaskOutput output);
private:

};


//namespace ExtractionThread {
//	////////////////////////////////////////////////////////////////////////
//	class SurfaceExtractionTask : public FNonAbandonableTask {
//		friend class FAutoDeleteAsyncTask<SurfaceExtractionTask>;
//		APagedRegion* region;
//		FIntVector lower;
//	public:
//		SurfaceExtractionTask(APagedRegion* region, FIntVector lower) {
//			this->region = region;
//			this->lower = lower;
//		}
//
//		FORCEINLINE TStatId GetStatId() const
//		{
//			RETURN_QUICK_DECLARE_CYCLE_STAT(SurfaceExtractionTask, STATGROUP_ThreadPoolAsyncTasks);
//		}
//		//pChunk->setVoxel(x + lower.X, y + lower.Y, z + lower.Z, v);//world pos
//		void DoWork() {
//			TArray<FExtractionTaskOutput> output;
//			output.SetNum(region->world->TerrainMaterials.Num());
//
//			::PolyVox::Region ToExtract(::PolyVox::Vector3DInt32(lower.X, lower.Y, lower.Z), ::PolyVox::Vector3DInt32(lower.X + REGION_SIZE, lower.Y + REGION_SIZE, lower.Z + REGION_SIZE));
//
//			auto ExtractedMesh = ::PolyVox::extractMarchingCubesMesh(region->world->VoxelVolume.Get(), ToExtract);
//
//			//lock mutx
//			//auto ExtractedMesh = PolyVox::extractCubicMesh(world->VoxelVolume.Get(), ToExtract);
//			//unlock
//
//			auto DecodedMesh = ::PolyVox::decodeMesh(ExtractedMesh);
//
//			if (DecodedMesh.getNoOfIndices() == 0)
//				return;
//
//			//UE_LOG(LogTemp, Warning, TEXT("oof array len is %d."), DecodedMesh.getNoOfIndices());
//
//			for (int32 Material = 0; Material < region->world->TerrainMaterials.Num(); Material++)
//			{
//
//				// Loop over all of the triangle vertex indices
//				for (uint32 i = 0; i < DecodedMesh.getNoOfIndices() - 2; i += 3)
//				{
//					// We need to add the vertices of each triangle in reverse or the mesh will be upside down
//					auto Index = DecodedMesh.getIndex(i + 2);
//					auto Vertex2 = DecodedMesh.getVertex(Index);
//					auto TriangleMaterial = Vertex2.data.getMaterial();
//
//					// Before we continue, we need to be sure that the triangle is the right material; we don't want to use verticies from other materials
//					if (TriangleMaterial == (Material + 1))
//					{
//						// If it is of the same material, then we need to add the correct indices now
//						output[Material].Indices.Add(output[Material].Vertices.Add((FPolyVoxVector(Vertex2.position) + FPolyVoxVector(FVector(lower))) * VOXEL_SIZE));
//
//						Index = DecodedMesh.getIndex(i + 1);
//						auto Vertex1 = DecodedMesh.getVertex(Index);
//						output[Material].Indices.Add(output[Material].Vertices.Add((FPolyVoxVector(Vertex1.position) + FPolyVoxVector(FVector(lower))) * VOXEL_SIZE));
//
//						Index = DecodedMesh.getIndex(i);
//						auto Vertex0 = DecodedMesh.getVertex(Index);
//						output[Material].Indices.Add(output[Material].Vertices.Add((FPolyVoxVector(Vertex0.position) + FPolyVoxVector(FVector(lower))) * VOXEL_SIZE));
//
//						// Calculate the tangents of our triangle
//						const FVector Edge01 = FPolyVoxVector(Vertex1.position - Vertex0.position);
//						const FVector Edge02 = FPolyVoxVector(Vertex2.position - Vertex0.position);
//
//						const FVector TangentX = Edge01.GetSafeNormal();
//						FVector TangentZ = (Edge01 ^ Edge02).GetSafeNormal();
//
//						for (int32 i = 0; i < 3; i++)
//						{
//							output[Material].Tangents.Add(FRuntimeMeshTangent(TangentX, false));
//							output[Material].Normals.Add(TangentZ);
//						}
//					}
//				}
//
//
//			}
//			region->extractionResultQueue.Enqueue(output);
//		};
//		////////////////////////////////////////////////////////////////////////
//	};
//}