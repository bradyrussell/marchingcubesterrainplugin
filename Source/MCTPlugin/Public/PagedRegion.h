#pragma once
#include "RuntimeMeshComponent.h"
//#include "RuntimeMeshSection.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PagedWorld.h"
#include "RuntimeMeshActor.h"
#include "RuntimeMeshComponent/Public/Providers/RuntimeMeshProviderStatic.h"
#include "PagedRegion.generated.h"

//cleaned up

UCLASS()
	class MCTPLUGIN_API APagedRegion : public ARuntimeMeshActor {
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

	//UPROPERTY(Category = "Voxel Terrain", BlueprintReadWrite, VisibleAnywhere) class USceneComponent* Scene;
	//UPROPERTY(Category = "Voxel Terrain", BlueprintReadOnly, VisibleAnywhere, Meta = (ExposeFunctionCategories = "Mesh,Rendering,Physics,Components|RuntimeMesh", AllowPrivateAccess = "true"))
	//URuntimeMeshComponent* RuntimeMesh;

	UPROPERTY(Category = "Voxel Terrain - World", BlueprintReadWrite, Replicated, EditAnywhere, meta = (ExposeOnSpawn = "true")) APagedWorld* World;
	
	UPROPERTY(BlueprintReadOnly, VisibleInstanceOnly) URuntimeMeshProviderStatic* StaticProvider;

	UFUNCTION(BlueprintCallable) void UpdateNavigation() const;
	UFUNCTION(BlueprintCallable, BlueprintPure) FIntVector GetRegionLocation() const;
	void RenderParsed(FExtractionTaskOutput output);

	// has the region been meshed on the local machine
	UPROPERTY(Category = "Voxel Terrain", BlueprintReadOnly, VisibleInstanceOnly) bool bReadyLocally = false;

	// whether the region has polys
	UPROPERTY(Category = "Voxel Terrain", BlueprintReadOnly, VisibleInstanceOnly) bool bEmptyLocally = true;

	// has the region been meshed on the server
	UPROPERTY(Category = "Voxel Terrain", BlueprintReadOnly, VisibleInstanceOnly, Replicated) bool bReadyServer = false;
	UPROPERTY(Category = "Voxel Terrain", BlueprintReadOnly, VisibleInstanceOnly, Replicated) bool bEmptyServer = true;
private:
	TArray<bool> bSectionExists;
};
