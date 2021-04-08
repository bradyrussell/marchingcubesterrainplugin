#pragma once

#include "PagedWorld.h"
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PreventMovementInUnloadedRegionsComponent.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
	class MCTPLUGIN_API UPreventMovementInUnloadedRegionsComponent : public UActorComponent {
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UPreventMovementInUnloadedRegionsComponent();

protected:
	virtual void BeginPlay() override;
public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(BlueprintReadWrite) APagedWorld* WorldRef;
	UPROPERTY(BlueprintReadWrite) APawn* PawnRef;

	UPROPERTY(BlueprintReadOnly, VisibleInstanceOnly) bool bShouldPreventMovement = false;
};
