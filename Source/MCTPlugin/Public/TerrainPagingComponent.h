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
	UPROPERTY(Category = "Paging Component", BlueprintReadWrite, EditAnywhere) int32 viewDistance = 4;
	UPROPERTY(Category = "Paging Component", BlueprintReadWrite, EditAnywhere) bool bUseOverrideLocation;
	UPROPERTY(Category = "Paging Component", BlueprintReadWrite, EditAnywhere) FVector OverrideLocation;
	UPROPERTY(Category = "Paging Component", BlueprintReadWrite, EditAnywhere) bool bIsConnectedToVoxelnet = false;
	UPROPERTY(Category = "Paging Component", BlueprintReadWrite, EditAnywhere) bool bFreezePawn = false;
	
	UPROPERTY(Category = "Paging Component", BlueprintReadOnly, VisibleAnywhere) APagedWorld* world;

	UFUNCTION(Category = "Paging Component", BlueprintPure) bool ShouldFreezePawn() const;
	UFUNCTION(Category = "Paging Component", BlueprintCallable) bool PrepareToTeleport(const FVector Destination);

	void OnSentRegionPacket(int Num);
	int32 ExpectedRegions = 0;
	bool bIsPreparingTeleport = false;
	
	TSet<FIntVector> subscribedRegions;
	TSet<FIntVector> waitingForPackets;

	FVector GetPagingLocation() const;
	
protected:
	virtual void BeginPlay() override;
public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
};
