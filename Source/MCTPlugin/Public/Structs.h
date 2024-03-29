#pragma once
#include "UnrealFastNoisePlugin.h"
#include "PolyVox/MaterialDensityPair.h"
#include "PolyVox/Vector.h"
#include "GameFramework/Actor.h"
#include "RuntimeMeshComponent.h"
#include "CoreMinimal.h"
#include "VoxelNetThreads.h"

#include "Config.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Structs.generated.h"

// https://github.com/LtBrandon/UE4VoxelTerrain
// Bridge between PolyVox Vector3DFloat and Unreal Engine 4 FVector
struct FPolyVoxVector : public FVector {
	FORCEINLINE FPolyVoxVector() {
	}

	explicit FORCEINLINE FPolyVoxVector(EForceInit E)
		: FVector(E) {
	}

	FORCEINLINE FPolyVoxVector(float InX, float InY, float InZ)
		: FVector(InX, InY, InX) {
	}

	FORCEINLINE FPolyVoxVector(const FVector& InVec) { FVector::operator=(InVec); }

	FORCEINLINE FPolyVoxVector(const PolyVox::Vector3DFloat& InVec) { operator=(InVec); }

	FORCEINLINE FVector& operator=(const PolyVox::Vector3DFloat& Other) {
		this->X = Other.getX();
		this->Y = Other.getY();
		this->Z = Other.getZ();

		DiagnosticCheckNaN();

		return *this;
	}
};

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
	bool bIsEmpty = false;
	FIntVector region;
	TArray<FExtractionTaskSection> section = TArray<FExtractionTaskSection>();
};

USTRUCT(BlueprintType)
struct FPacketTaskOutput // compressed packet information for a region
{
	GENERATED_BODY()
	FIntVector region;
	TArray<uint8> packet;
	bool bIsEmpty;
};

USTRUCT(BlueprintType)
struct FRegionVoxels // a region of voxels copied in a game-readable format
{
	GENERATED_BODY()
	int32 Material[REGION_SIZE][REGION_SIZE][REGION_SIZE];
	int32 Density[REGION_SIZE][REGION_SIZE][REGION_SIZE];

	/*int32 GetIndex(FIntVector LocalRegionCoord) {
		return LocalRegionCoord.X | (LocalRegionCoord.Y << 8) | (LocalRegionCoord.Z << 16);
	}*/
};

USTRUCT(BlueprintType)
struct FVoxelUpdate // a change of a group of voxels from any type to a single new type
{
	GENERATED_BODY()
	FVoxelUpdate() {
	}


	FVoxelUpdate(const FIntVector& Origin, uint8 Radius, uint8 Material, uint8 Density, bool bShouldDrop, bool bIsSpherical, bool bShouldCallEvent, AActor* CauseActor)
		: origin(Origin),
		  radius(Radius),
		  material(Material),
		  density(Density),
		  bShouldDrop(bShouldDrop),
		  bIsSpherical(bIsSpherical),
		  bShouldCallEvent(bShouldCallEvent),
		  causeActor(CauseActor) {
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

//FVoxelExplosion(TMap<uint8,float> MaterialExplosionResistances ??)


USTRUCT(BlueprintType)
struct FWorldGenerationTaskOutput // 
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "WorldGen Task")
	FIntVector pos;
	PolyVox::MaterialDensityPair88 voxel[REGION_SIZE][REGION_SIZE][REGION_SIZE];

	// is it entirely zeroed?
	bool bIsEmpty;
};

USTRUCT(BlueprintType)
struct FPendingHandshake // 
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Handshake")
	int64 cookie = 0;
	TSharedPtr<VoxelNetThreads::VoxelNetServer> server;
};


struct FVoxelWorldSaveGameArchive : public FObjectAndNameAsStringProxyArchive {
	FVoxelWorldSaveGameArchive(FArchive& InInnerArchive)
		: FObjectAndNameAsStringProxyArchive(InInnerArchive, false) {
		ArIsSaveGame = true;
		ArNoDelta = true;
	}
};


USTRUCT()
struct FVoxelWorldComponentRecord {
	GENERATED_USTRUCT_BODY()

	FString ComponentClass;
	FTransform ComponentTransform;
	TArray<uint8> ComponentData;
	bool bSpawnIfNotFound;

	friend FArchive& operator<<(FArchive& Ar, FVoxelWorldComponentRecord& Record) {
		Ar << Record.ComponentClass;
		Ar << Record.ComponentTransform;
		Ar << Record.ComponentData;
		Ar << Record.bSpawnIfNotFound;
		return Ar;
	}

};

USTRUCT()
struct FVoxelWorldActorRecord {
	GENERATED_USTRUCT_BODY()

	FString ActorClass;
	FTransform ActorTransform;
	TArray<FVoxelWorldComponentRecord> ActorComponents;
	TArray<uint8> ActorData;
	int64 PersistentActorID;

	friend FArchive& operator<<(FArchive& Ar, FVoxelWorldActorRecord& Record) {
		Ar << Record.ActorClass;
		Ar << Record.ActorTransform;
		Ar << Record.ActorComponents;
		Ar << Record.ActorData;
		Ar << Record.PersistentActorID;
		return Ar;
	}

};

USTRUCT()
struct FVoxelWorldPlayerActorRecord {
	GENERATED_USTRUCT_BODY()

	FString ActorClass;
	FDateTime SavedAt;
	FTransform ActorTransform;
	TArray<FVoxelWorldComponentRecord> ActorComponents;
	TArray<uint8> ActorData;


	friend FArchive& operator<<(FArchive& Ar, FVoxelWorldPlayerActorRecord& Record) {
		Ar << Record.ActorClass;
		Ar << Record.SavedAt;
		Ar << Record.ActorTransform;
		Ar << Record.ActorComponents;
		Ar << Record.ActorData;
		return Ar;
	}

};
