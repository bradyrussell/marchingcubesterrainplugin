#include "TerrainPagingComponent.h"
#include "PagedWorld.h"
#include "Kismet/GameplayStatics.h"

UTerrainPagingComponent::UTerrainPagingComponent() {
	PrimaryComponentTick.bCanEverTick = false;
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
