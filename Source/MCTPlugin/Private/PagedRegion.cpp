#include "PagedRegion.h"
#include "PolyVox/CubicSurfaceExtractor.h"

#include "ComponentReregisterContext.h"
#include "Net/UnrealNetwork.h"

//cleaned up

APagedRegion::APagedRegion() {
	PrimaryActorTick.bCanEverTick = false;
	//Scene = CreateDefaultSubobject<USceneComponent>(FName(TEXT("Root")));
	//SetRootComponent(Scene);
	bReplicates = true;
	bAlwaysRelevant = true; // we manage our own culling
}

void APagedRegion::Tick(float DeltaTime) { Super::Tick(DeltaTime); }

APagedRegion::~APagedRegion() {}

void APagedRegion::PostInitializeComponents() { Super::PostInitializeComponents(); }

void APagedRegion::BeginPlay() {
	Super::BeginPlay();
	
	if(!StaticProvider)
	StaticProvider = NewObject<URuntimeMeshProviderStatic>(this, TEXT("RuntimeMeshProvider-Static"));

	GetRuntimeMeshComponent()->Initialize(StaticProvider);

	int index = 0;
	for(auto&elem: World->TerrainMaterials) {
		StaticProvider->SetupMaterialSlot(index++, FName(*FString::FromInt(index)), elem);
	}
	
	/*RuntimeMesh = NewObject<URuntimeMeshComponent>(this, URuntimeMeshComponent::StaticClass()); //, *compName);
	RuntimeMesh->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
	RuntimeMesh->BodyInstance.SetResponseToAllChannels(ECR_Block);

	/// DISABLING THIS STOPS THE NAN ERROR IN EDITOR. 
	//RuntimeMesh->SetCollisionUseAsyncCooking(World->bUseAsyncCollision); // this gave more performance gains than all the render queues as far as i can tell
	RuntimeMesh->RegisterComponent(); // NAN error happens on this call*/
	
	//this is meant to allow the regions map to be replicated
	if (World)
		if (!World->bIsVoxelNetServer) { World->regions.Add(FIntVector(GetRegionLocation()), this); }
}

void APagedRegion::EndPlay(const EEndPlayReason::Type EndPlayReason) {
	//this is meant to allow the regions map to be replicated
	if (World)
		if (!World->bIsVoxelNetServer) { World->regions.Remove(FIntVector(GetRegionLocation())); }
}


void APagedRegion::RenderParsed(FExtractionTaskOutput output) {
	if (!StaticProvider) { UE_LOG(LogTemp, Error, TEXT("There is no provider when trying to render parsed.")); }
	
	for (int32 Material = 0; Material < output.section.Num(); Material++) {
		if (output.section[Material].Indices.Num() > 0) {// fixes dhd3d resource crash 
			//FRuntimeMeshDataPtr Data = RuntimeMesh->GetOrCreateRuntimeMesh()->GetRuntimeMeshData();
			//Data->EnterSerializedMode();

			if (!bSectionExists[Material]) {
				//Material, output.section[Material].Vertices, output.section[Material].Indices, output.section[Material].Normals, output.section[Material].UV0,
				               //         output.section[Material].Colors, output.section[Material].Tangents, ERuntimeMeshUpdateFrequency::Frequent, true
				
				StaticProvider->CreateSectionFromComponents(0, Material, Material, output.section[Material].Vertices,output.section[Material].Indices, output.section[Material].Normals, output.section[Material].UV0,
				                        output.section[Material].Colors, output.section[Material].Tangents, ERuntimeMeshUpdateFrequency::Frequent, true);
				//RuntimeMesh->SetMaterial(Material, World->TerrainMaterials[Material]); // as far as i can tell not necessary on updates
				bSectionExists[Material] = true;
			}
			else {
				StaticProvider->UpdateSectionFromComponents(0, Material, output.section[Material].Vertices,output.section[Material].Indices, output.section[Material].Normals, output.section[Material].UV0,
				                        output.section[Material].Colors, output.section[Material].Tangents);
				
				// Data->UpdateMeshSection(Material, output.section[Material].Vertices, output.section[Material].Indices, output.section[Material].Normals, output.section[Material].UV0,
				//                         output.section[Material].Colors, output.section[Material].Tangents);
			}

			//auto Section = Data->BeginSectionUpdate(Material); // must be called every update
			//Section->Commit();
		}
		else {
			StaticProvider->ClearSection(0,Material);
			//RuntimeMesh->ClearMeshSection(Material); // fixes not being able to remove last polys of a material from a region
			bSectionExists[Material] = false;
		}
	}

	//StaticProvider


	
	/*// maybe the nan error is because this creates the mesh?
	if(RuntimeMesh->IsCollisionUsingAsyncCooking() != World->bUseAsyncCollision) {
		RuntimeMesh->SetCollisionUseAsyncCooking(World->bUseAsyncCollision);
	}*/
}

void APagedRegion::UpdateNavigation() const { FNavigationSystem::UpdateComponentData(*GetRuntimeMeshComponent()); }

FIntVector APagedRegion::GetRegionLocation() const { return FIntVector(GetActorLocation()) / VOXEL_SIZE/* / REGION_SIZE*/; }

void APagedRegion::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APagedRegion, World);
}
