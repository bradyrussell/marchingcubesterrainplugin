// Copyright (c) 2016 Brandon Garvin

#pragma once

#include "PagedWorld.h"
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TerrainPagingComponent.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
	class MCTPLUGIN_API UTerrainPagingComponent : public UActorComponent {
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UTerrainPagingComponent();

	UPROPERTY(Category = "Voxel World", BlueprintReadWrite, EditAnywhere)
	int32 viewDistance = 2;

	UPROPERTY(Category = "Voxel World", BlueprintReadOnly, VisibleAnywhere)
	APagedWorld* world;

	//getCurrentRegion


protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;


};
