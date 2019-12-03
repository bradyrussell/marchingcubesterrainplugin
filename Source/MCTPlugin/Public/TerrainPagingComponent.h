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

	//Radius in regions
	UPROPERTY(Category = "Voxel LOD Levels", BlueprintReadWrite, EditAnywhere) TArray<FVoxelLODLevel> LODLevels;
	UPROPERTY(Category = "Voxel World", BlueprintReadOnly, VisibleAnywhere) APagedWorld* world;
	TSet<FIntVector> subscribedRegions;
	TSet<FIntVector> waitingForPackets;
protected:
	virtual void BeginPlay() override;
public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
};
