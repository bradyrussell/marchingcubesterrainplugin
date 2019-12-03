#pragma once
#include "RuntimeMeshComponent.h"
#include "RuntimeMeshSection.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PagedWorld.h"
#include "PagedRegion.generated.h"

//cleaned up

UCLASS()
	class MCTPLUGIN_API APagedRegion : public AActor {
	GENERATED_BODY()

public:
	APagedRegion();
	~APagedRegion();
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PostInitializeComponents() override;
public:
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(Category = "Voxel Terrain", BlueprintReadWrite, VisibleAnywhere) class USceneComponent* Scene;
	UPROPERTY(Category = "Voxel Terrain", BlueprintReadOnly, VisibleAnywhere, Meta = (ExposeFunctionCategories = "Mesh,Rendering,Physics,Components|RuntimeMesh", AllowPrivateAccess = "true"))
	URuntimeMeshComponent* RuntimeMesh;

	UPROPERTY(Category = "Voxel Terrain - World", BlueprintReadWrite, Replicated, EditAnywhere, meta = (ExposeOnSpawn = "true")) APagedWorld* World;

	UFUNCTION(BlueprintCallable) void UpdateNavigation() const;
	UFUNCTION(BlueprintCallable, BlueprintPure) FIntVector GetRegionLocation() const;
	void RenderParsed(FExtractionTaskOutput output);

	bool bHasCollision;
private:
	bool bSectionExists[MAX_MATERIALS];
};
