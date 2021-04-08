#include "PagedRegion.h"
#include "PolyVox/CubicSurfaceExtractor.h"
#include "MCTPlugin.h"
#include "ComponentReregisterContext.h"
#include "Net/UnrealNetwork.h"

//cleaned up

APagedRegion::APagedRegion() {
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	bAlwaysRelevant = true; // we manage our own culling
	GetRuntimeMeshComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECollisionResponse::ECR_Block);
}

void APagedRegion::Tick(float DeltaTime) { Super::Tick(DeltaTime); }

APagedRegion::~APagedRegion() {
}

void APagedRegion::PostInitializeComponents() { Super::PostInitializeComponents(); }

void APagedRegion::BeginPlay() {
	Super::BeginPlay();

	if (!StaticProvider)
		StaticProvider = NewObject<URuntimeMeshProviderStatic>(this, TEXT("RuntimeMeshProvider-Static"));

	GetRuntimeMeshComponent()->Initialize(StaticProvider);

	if (World){
		bSectionExists.AddDefaulted(World->TerrainMaterials.Num());
		
		for (int i = 0; i < World->TerrainMaterials.Num(); i++) { StaticProvider->SetupMaterialSlot(i, FName(*FString::FromInt(i)), World->TerrainMaterials[i]); }

		FRuntimeMeshCollisionSettings CollisionSettings;
		CollisionSettings.bUseAsyncCooking = World->bUseAsyncCollision;
		CollisionSettings.bUseComplexAsSimple = true;
	
		StaticProvider->SetCollisionSettings(CollisionSettings);

		//this is meant to allow the regions map to be replicated
	
		if (!World->bIsVoxelNetServer && !World->bIsVoxelNetSingleplayer) {
			World->regions.Add(FIntVector(GetRegionLocation()), this);
			World->dirtyRegions.Emplace(this->GetRegionLocation());
		}
	} else {
		UE_LOG(LogVoxelWorld, Error, TEXT("Region [%s] spawned with an invalid world reference."), *this->GetRegionLocation().ToString());
		Destroy();
	}
}

void APagedRegion::EndPlay(const EEndPlayReason::Type EndPlayReason) {
	//this is meant to allow the regions map to be replicated
	if (World)
		if (!World->bIsVoxelNetServer) { World->regions.Remove(FIntVector(GetRegionLocation())); }
}


void APagedRegion::RenderParsed(const FExtractionTaskOutput& output) {
	if (!StaticProvider) { UE_LOG(LogVoxelWorld, Error, TEXT("There is no provider when trying to render parsed.")); }

	bool bHasMesh = false;
	
	// for each LOD level in output
	//if(!output.bIsEmpty){   // this is making it so the last mesh of a region cant be cleared. i dont see any harm in iterating on air regions
		for (int32 Material = 0; Material < output.section.Num(); Material++) {
			if (output.section[Material].Indices.Num() > 0) {
				if (!bSectionExists[Material]) {
					StaticProvider->CreateSectionFromComponents(0, Material, Material, output.section[Material].Vertices, output.section[Material].Indices, output.section[Material].Normals,
					                                            output.section[Material].UV0,
					                                            output.section[Material].Colors, output.section[Material].Tangents, ERuntimeMeshUpdateFrequency::Frequent, true);

					bSectionExists[Material] = true;
					bHasMesh = true;
				}
				else {
					StaticProvider->UpdateSectionFromComponents(0, Material, output.section[Material].Vertices, output.section[Material].Indices, output.section[Material].Normals,
					                                            output.section[Material].UV0,
					                                            output.section[Material].Colors, output.section[Material].Tangents);
					bHasMesh = true;
				//	StaticProvider->MarkCollisionDirty(); // does it matter that i call this numerous time? i assume not because they are all in succession
						// if it does I can change it to a function scope bNeedsMarkedDirty since it applies for the whole provider not just the section
				}
			}
			else {
				StaticProvider->ClearSection(0, Material);
				
				//StaticProvider->MarkCollisionDirty(); 
				bSectionExists[Material] = false;
			}
		}
	//}
	/*if(bHasMesh) */StaticProvider->MarkCollisionDirty(); 

	bEmptyLocally = output.bIsEmpty;
	bReadyLocally = true;
	if(GetLocalRole() == ENetRole::ROLE_Authority) {
		bReadyServer = true;
		bEmptyServer = output.bIsEmpty;
	}
}

void APagedRegion::UpdateNavigation() const { FNavigationSystem::UpdateComponentData(*GetRuntimeMeshComponent()); }

FIntVector APagedRegion::GetRegionLocation() const { return FIntVector(GetActorLocation()) / VOXEL_SIZE/* / REGION_SIZE*/; }

void APagedRegion::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APagedRegion, World);
	DOREPLIFETIME(APagedRegion, bReadyServer);
	DOREPLIFETIME(APagedRegion, bEmptyServer);
}
