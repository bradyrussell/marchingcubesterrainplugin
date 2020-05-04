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

	for (int i = 0; i < World->TerrainMaterials.Num(); i++) { StaticProvider->SetupMaterialSlot(i, FName(*FString::FromInt(i)), World->TerrainMaterials[i]); }

	FRuntimeMeshCollisionSettings CollisionSettings;
	CollisionSettings.bUseAsyncCooking = World->bUseAsyncCollision;
	CollisionSettings.bUseComplexAsSimple = true;

	StaticProvider->SetCollisionSettings(CollisionSettings);

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
	if (!StaticProvider) { UE_LOG(LogVoxelWorld, Error, TEXT("There is no provider when trying to render parsed.")); }

	// for each LOD level in output

	for (int32 Material = 0; Material < output.section.Num(); Material++) {
		if (output.section[Material].Indices.Num() > 0) {
			if (!bSectionExists[Material]) {
				StaticProvider->CreateSectionFromComponents(0, Material, Material, output.section[Material].Vertices, output.section[Material].Indices, output.section[Material].Normals,
				                                            output.section[Material].UV0,
				                                            output.section[Material].Colors, output.section[Material].Tangents, ERuntimeMeshUpdateFrequency::Frequent, true);

				bSectionExists[Material] = true;
			}
			else {
				StaticProvider->UpdateSectionFromComponents(0, Material, output.section[Material].Vertices, output.section[Material].Indices, output.section[Material].Normals,
				                                            output.section[Material].UV0,
				                                            output.section[Material].Colors, output.section[Material].Tangents);

				StaticProvider->MarkCollisionDirty();
			}
		}
		else {
			StaticProvider->ClearSection(0, Material);

			bSectionExists[Material] = false;
		}
	}

}

void APagedRegion::UpdateNavigation() const { FNavigationSystem::UpdateComponentData(*GetRuntimeMeshComponent()); }

FIntVector APagedRegion::GetRegionLocation() const { return FIntVector(GetActorLocation()) / VOXEL_SIZE/* / REGION_SIZE*/; }

void APagedRegion::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APagedRegion, World);
}
