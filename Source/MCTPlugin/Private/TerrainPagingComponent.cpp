#include "TerrainPagingComponent.h"
#include "PagedWorld.h"
#include "Kismet/GameplayStatics.h"

UTerrainPagingComponent::UTerrainPagingComponent() {
	PrimaryComponentTick.bCanEverTick = false;
}

bool UTerrainPagingComponent::ShouldFreezePawn() const {
	return !bIsConnectedToVoxelnet || bFreezePawn;
}

bool UTerrainPagingComponent::PrepareToTeleport(const FVector Destination) {
	bIsPreparingTeleport = true;
	bFreezePawn = true;
	OverrideLocation = Destination;
	bUseOverrideLocation = true;
	// somehow notify when received? gotta figure out what it means to receive all...
	// maybe viewdist*2 ^ 3 and wait til we receive that many packets?
	ExpectedRegions = FMath::Pow(viewDistance * 2, 3);
	return true;
	
}

void UTerrainPagingComponent::OnSentRegionPacket(int Num) {
	ExpectedRegions--;
	if(ExpectedRegions == 0 && bIsPreparingTeleport) {
		bIsPreparingTeleport = false;
		bFreezePawn = false;
		bUseOverrideLocation = false;
	}
}

FVector UTerrainPagingComponent::GetPagingLocation() const {
	return bUseOverrideLocation ? OverrideLocation : GetOwner()->GetActorLocation();
}

void UTerrainPagingComponent::BeginPlay() {
	Super::BeginPlay();
	TArray<AActor*> worldActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APagedWorld::StaticClass(), worldActors);

	if (worldActors.Num() < 1) {
		UE_LOG(LogTemp, Warning, TEXT("TerrainPagingComponent: Cannot find world actor."));
		return;
	}
	world = Cast<APagedWorld>(worldActors[0]);
	world->RegisterPagingComponent(this);
}

void UTerrainPagingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}
