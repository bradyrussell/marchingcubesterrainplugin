#include "PreventMovementInUnloadedRegionsComponent.h"
#include "PagedWorld.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"


/*
void UPreventMovementInUnloadedRegionsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UPreventMovementInUnloadedRegionsComponent, DebugWaitingForAmount);

}
*/

UPreventMovementInUnloadedRegionsComponent::UPreventMovementInUnloadedRegionsComponent() {
}

void UPreventMovementInUnloadedRegionsComponent::BeginPlay() {
	Super::BeginPlay();
	TArray<AActor*> OutActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APagedWorld::StaticClass(), OutActors);
	if(OutActors.Num() < 1) return;
	WorldRef = Cast<APagedWorld>(OutActors[0]);
}

void UPreventMovementInUnloadedRegionsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {
	Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
	if(GetOwnerRole() != ENetRole::ROLE_Authority) return;
	if(WorldRef) {
		if(!PawnRef || !IsValid(PawnRef)) {
			PawnRef = Cast<APawn>(GetOwner());
			if(!PawnRef) {
				AController* Controller = Cast<AController>(GetOwner());
				if(Controller) {
					PawnRef = Controller->GetPawn();
				}
			}
		}

		if(PawnRef) { // prevent movement if in an unloaded region
			bShouldPreventMovement = !WorldRef->regions.Contains(APagedWorld::VoxelToRegionCoords(APagedWorld::WorldToVoxelCoords(PawnRef->GetActorLocation())));
		}
	}
}
