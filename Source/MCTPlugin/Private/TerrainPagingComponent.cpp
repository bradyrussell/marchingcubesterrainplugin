// Copyright (c) 2016 Brandon Garvin


#include "TerrainPagingComponent.h"
#include "PagedWorld.h"
#include "Kismet/GameplayStatics.h"

// Sets default values for this component's properties
UTerrainPagingComponent::UTerrainPagingComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
	//PrimaryComponentTick.bRunOnAnyThread = true;
	//PrimaryComponentTick.TickInterval = .5;
	// ...
}


// Called when the game starts
void UTerrainPagingComponent::BeginPlay()
{
	Super::BeginPlay();
	TArray<AActor*> worldActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APagedWorld::StaticClass(), worldActors);

	if (worldActors.Num() < 1) {
		UE_LOG(LogTemp, Warning, TEXT("no world exists"));
		return;
	}

	world = Cast<APagedWorld>(worldActors[0]);

	world->RegisterPagingComponent(this);
	// ...
	
}


// Called every frame
void UTerrainPagingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	world->LoadOrGenerateWorldRadius(world->VoxelToRegionCoords(world->WorldToVoxelCoords(GetOwner()->GetActorLocation())), viewDistance);
	//world->MarkRegionDirtyAndSurrounding(world->VoxelToRegionCoords(world->WorldToVoxelCoords(GetOwner()->GetActorLocation())),viewDistance);
	// ...
}

