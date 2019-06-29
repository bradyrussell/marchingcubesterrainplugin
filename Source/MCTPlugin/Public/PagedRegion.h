#pragma once
#include "UnrealFastNoisePlugin.h"
#include "UnrealFastNoisePlugin/Public/UFNNoiseGenerator.h"

#include "PolyVox/PagedVolume.h"
#include "PolyVox/MaterialDensityPair.h"
#include "PolyVox/Vector.h"
#include "PolyVox/MarchingCubesSurfaceExtractor.h"
#include "PolyVox/Mesh.h"

#include "RuntimeMeshComponent.h"
#include "RuntimeMeshSection.h"

//#include "LevelDatabase.h"

#include "AI/NavigationSystemBase.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PagedWorld.h"
#include "PagedRegion.generated.h"


UCLASS()
class MCTPLUGIN_API APagedRegion : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	APagedRegion();
	~APagedRegion();
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void PostInitializeComponents() override;
public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(Category = "Voxel Terrain", BlueprintReadWrite, VisibleAnywhere) class USceneComponent* Scene;
	UPROPERTY(Category = "Voxel Terrain", BlueprintReadOnly, VisibleAnywhere, Meta = (ExposeFunctionCategories = "Mesh,Rendering,Physics,Components|RuntimeMesh", AllowPrivateAccess = "true"))
	URuntimeMeshComponent* rMesh;

	//todo make better soln than this
	bool wasCreated[10] = { false,false,false,false,false,false,false,false,false,false };

	UPROPERTY(Category = "Voxel Terrain - World", BlueprintReadWrite, EditAnywhere, meta = (ExposeOnSpawn = "true")) APagedWorld* world;

	UFUNCTION(Category = "Voxel Terrain", BlueprintCallable) void SlowRender();
	void RenderDecoded(PolyVox::Mesh<PolyVox::Vertex<PolyVox::MaterialDensityPair88>, unsigned int> decoded);

	void RenderParsed(FExtractionTaskOutput output);

	void UpdateNavigation();

	// update the last seen to current time
	UFUNCTION(Category = "Voxel Terrain", BlueprintCallable)void Touch();

	// check whether the region was seen in the last x period of time
	UFUNCTION(Category = "Voxel Terrain", BlueprintCallable)bool wasSeenWithin(FTimespan timespan);

	FDateTime lastSeen;
private:

};