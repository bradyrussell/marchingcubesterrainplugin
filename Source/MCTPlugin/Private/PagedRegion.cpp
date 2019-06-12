#include "PagedRegion.h"
#include "PolyVox/CubicSurfaceExtractor.h"
#include "PolyVox/MarchingCubesSurfaceExtractor.h"
#include "PolyVox/Mesh.h"

// Sets default values
APagedRegion::APagedRegion()
{
	PrimaryActorTick.bCanEverTick = false;
	Scene = CreateDefaultSubobject<USceneComponent>(FName(TEXT("Root")));
	SetRootComponent(Scene);
}

// Called every frame
void APagedRegion::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	//if (!extractionResultQueue.IsEmpty()) { 

	//	TArray<FExtractionTaskOutput> gen;
	//	extractionResultQueue.Dequeue(gen);

	//	int Material = 0;

	//	for (FExtractionTaskOutput r : gen) {
	//		if (r.Indices.Num() > 0) { // fixes dhd3d resource crash 

	//			FRuntimeMeshDataPtr Data = rMesh->GetOrCreateRuntimeMesh()->GetRuntimeMeshData();
	//			Data->EnterSerializedMode();

	//			if (!wasCreated[Material]) {
	//				Data->CreateMeshSection(Material, r.Vertices, r.Indices, r.Normals, r.UV0, r.Colors, r.Tangents, true, EUpdateFrequency::Average);
	//				wasCreated[Material] = true;
	//			}
	//			else {
	//				Data->UpdateMeshSection(Material, r.Vertices, r.Indices, r.Normals, r.UV0, r.Colors, r.Tangents);
	//			}

	//			auto Section = Data->BeginSectionUpdate(Material); // must be called every update
	//			rMesh->SetMaterial(Material, world->TerrainMaterials[Material]);
	//			Section->Commit();
	//		}
	//		Material++;
	//	}

	//	world->MarkRegionDirtyAndAdjacent(FIntVector(GetActorLocation()));
	//}
}

APagedRegion::~APagedRegion()
{
	//VoxelVolume.Reset();
}


void APagedRegion::PostInitializeComponents()
{
	Super::PostInitializeComponents();
}
 
// Called when the actor has begun playing in the level
void APagedRegion::BeginPlay()
{
	Super::BeginPlay();
	rMesh = NewObject<URuntimeMeshComponent>(this, URuntimeMeshComponent::StaticClass());//, *compName);
	rMesh->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
	rMesh->BodyInstance.SetResponseToAllChannels(ECR_Block);
	rMesh->RegisterComponent();
}

void APagedRegion::Render()
{
	//(new FAutoDeleteAsyncTask<ExtractionThread::SurfaceExtractionTask>(this, FIntVector(GetActorLocation())))->StartBackgroundTask(); horribly broken
	FVector OffsetLocation = GetActorLocation();

	PolyVox::Region ToExtract(PolyVox::Vector3DInt32(OffsetLocation.X, OffsetLocation.Y, OffsetLocation.Z), PolyVox::Vector3DInt32(OffsetLocation.X + REGION_SIZE, OffsetLocation.Y + REGION_SIZE, OffsetLocation.Z + REGION_SIZE));
	//ToExtract.shrink(1);
	auto ExtractedMesh = PolyVox::extractMarchingCubesMesh(world->VoxelVolume.Get(), ToExtract);

	//lock mutx
	//auto ExtractedMesh = PolyVox::extractCubicMesh(world->VoxelVolume.Get(), ToExtract);
	//unlock

	auto DecodedMesh = PolyVox::decodeMesh(ExtractedMesh);

	if (DecodedMesh.getNoOfIndices() == 0)
		return;

	//UE_LOG(LogTemp, Warning, TEXT("oof array len is %d."), DecodedMesh.getNoOfIndices());

	for (int32 Material = 0; Material < world->TerrainMaterials.Num(); Material++)
	{
		// Define variables to pass into the CreateMeshSection function
		auto Vertices = TArray<FVector>();
		auto Normals = TArray<FVector>();
		auto Tangents = TArray<FRuntimeMeshTangent>();
		auto Colors = TArray<FColor>();
		auto UV0 = TArray<FVector2D>();
		auto Indices = TArray<int32>();

		// Loop over all of the triangle vertex indices
		for (uint32 i = 0; i < DecodedMesh.getNoOfIndices() - 2; i += 3)
		{
			// We need to add the vertices of each triangle in reverse or the mesh will be upside down
			auto Index = DecodedMesh.getIndex(i + 2);
			auto Vertex2 = DecodedMesh.getVertex(Index);
			auto TriangleMaterial = Vertex2.data.getMaterial();

			// Before we continue, we need to be sure that the triangle is the right material; we don't want to use verticies from other materials
			if (TriangleMaterial == (Material + 1))
			{
				// If it is of the same material, then we need to add the correct indices now
				Indices.Add(Vertices.Add((FPolyVoxVector(Vertex2.position) + OffsetLocation) * VOXEL_SIZE));

				Index = DecodedMesh.getIndex(i + 1);
				auto Vertex1 = DecodedMesh.getVertex(Index);
				Indices.Add(Vertices.Add((FPolyVoxVector(Vertex1.position) + OffsetLocation) * VOXEL_SIZE));

				Index = DecodedMesh.getIndex(i);
				auto Vertex0 = DecodedMesh.getVertex(Index);
				Indices.Add(Vertices.Add((FPolyVoxVector(Vertex0.position) + OffsetLocation) * VOXEL_SIZE));

				// Calculate the tangents of our triangle
				const FVector Edge01 = FPolyVoxVector(Vertex1.position - Vertex0.position);
				const FVector Edge02 = FPolyVoxVector(Vertex2.position - Vertex0.position);

				const FVector TangentX = Edge01.GetSafeNormal();
				FVector TangentZ = (Edge01 ^ Edge02).GetSafeNormal();

				for (int32 i = 0; i < 3; i++)
				{
					Tangents.Add(FRuntimeMeshTangent(TangentX, false));
					Normals.Add(TangentZ);
				}
			}
		}

		// run below on gamethread

		if (Indices.Num() > 0) { // fixes dhd3d resource crash 

			FRuntimeMeshDataPtr Data = rMesh->GetOrCreateRuntimeMesh()->GetRuntimeMeshData();
			Data->EnterSerializedMode();

			if (!wasCreated[Material]) {
				Data->CreateMeshSection(Material, Vertices, Indices, Normals, UV0, Colors, Tangents, true, EUpdateFrequency::Average);
				wasCreated[Material] = true;
			}
			else {
				Data->UpdateMeshSection(Material, Vertices, Indices, Normals, UV0, Colors, Tangents);
			}

			auto Section = Data->BeginSectionUpdate(Material); // must be called every update
			rMesh->SetMaterial(Material, world->TerrainMaterials[Material]);
			Section->Commit();
		}
	}
	return;
}



