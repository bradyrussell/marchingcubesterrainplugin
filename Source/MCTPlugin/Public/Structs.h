#pragma once
#include "UnrealFastNoisePlugin.h"
#include "PolyVox/MaterialDensityPair.h"
#include "PolyVox/Vector.h"
#include "GameFramework/Actor.h"
#include "RuntimeMeshComponent.h"
#include "CoreMinimal.h"
#include "VoxelNetThreads.h"

#include "Config.h"
#include "Structs.generated.h"

USTRUCT(BlueprintType)
	struct FExtractionTaskSection // results of surface extraction and decoding, to be plugged into updatemesh
{
	GENERATED_BODY()
	TArray<FVector> Vertices = TArray<FVector>();
	TArray<FVector> Normals = TArray<FVector>();
	TArray<FRuntimeMeshTangent> Tangents = TArray<FRuntimeMeshTangent>();
	TArray<FColor> Colors = TArray<FColor>();
	TArray<FVector2D> UV0 = TArray<FVector2D>();
	TArray<int32> Indices = TArray<int32>();
};

USTRUCT(BlueprintType)
	struct FExtractionTaskOutput // results of surface extraction and decoding, to be plugged into updatemesh
{
	GENERATED_BODY()
	FIntVector region;
	TArray<FExtractionTaskSection> section = TArray<FExtractionTaskSection>();
};

USTRUCT(BlueprintType)
	struct FPacketTaskOutput // compressed packet information for a region
{
	GENERATED_BODY()
	FIntVector region;
	TArray<uint8> packet;
};

USTRUCT(BlueprintType)
	struct FVoxelUpdate // a change of a group of voxels from any type to a single new type
{
	GENERATED_BODY()
	FVoxelUpdate() {
	}

	FVoxelUpdate(FIntVector Origin, uint8 Radius, uint8 Material, uint8 Density, AActor* causeActor = nullptr, bool IsSpherical = false, bool ShouldDrop = true, bool ShouldCallEvent = true)
		: origin(Origin), radius(Radius), material(Material), density(Density), bShouldDrop(ShouldDrop), bIsSpherical(IsSpherical), bShouldCallEvent(ShouldCallEvent), causeActor(causeActor) {
	}

	UPROPERTY(BlueprintReadWrite, Category = "Voxel Update")
	FIntVector origin;
	UPROPERTY(BlueprintReadWrite, Category = "Voxel Update")
	uint8 radius;
	UPROPERTY(BlueprintReadWrite, Category = "Voxel Update")
	uint8 material;
	UPROPERTY(BlueprintReadWrite, Category = "Voxel Update")
	uint8 density;
	UPROPERTY(BlueprintReadWrite, Category = "Voxel Update")
	bool bShouldDrop;
	UPROPERTY(BlueprintReadWrite, Category = "Voxel Update")
	bool bIsSpherical;
	UPROPERTY(BlueprintReadWrite, Category = "Voxel Update")
	bool bShouldCallEvent;
	UPROPERTY(BlueprintReadWrite, Category = "Voxel Update")
	AActor* causeActor = nullptr;
};



USTRUCT(BlueprintType)
	struct FWorldGenerationTaskOutput // 
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "WorldGen Task")
	FIntVector pos;

	PolyVox::MaterialDensityPair88 voxel[REGION_SIZE][REGION_SIZE][REGION_SIZE];
};

USTRUCT(BlueprintType)
	struct FPendingHandshake // 
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Handshake")
	int64 cookie = 0;
	TSharedPtr<VoxelNetThreads::VoxelNetServer> server;
};