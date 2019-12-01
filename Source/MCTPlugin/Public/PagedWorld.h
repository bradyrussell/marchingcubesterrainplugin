#pragma once

#include "UnrealFastNoisePlugin.h"
#include "UnrealFastNoisePlugin/Public/UFNNoiseGenerator.h"
#include "PolyVox/PagedVolume.h"
#include "PolyVox/MaterialDensityPair.h"
#include "PolyVox/Vector.h"
#include "PolyVox/MarchingCubesSurfaceExtractor.h"
#include "PolyVox/Mesh.h"
#include "GameFramework/Actor.h"
#include "RuntimeMeshComponent.h"
#include "LevelDatabase.h"
#include "BufferArchive.h"
#include "CoreMinimal.h"
#include "WorldGenInterpreters.h"
#include "VoxelNetThreads.h"
#include "Networking/Public/Common/TcpListener.h"
#include "Structs.h"
#include "PagedWorld.generated.h"

//todo set tcp listener to use my own socket, then set it to auto delete it, with send buffer of 16. 2mb is too small
//todo figure out replicating the regions?

//end config

class APagedRegion;
class UTerrainPagingComponent;


DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FVoxelWorldUpdate, class AActor*, CauseActor, const FIntVector, voxelLocation, const uint8, oldMaterial, const uint8, newMaterial);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVoxelNetHandshake,const int64, cookie);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPreSaveWorld,const bool, isQuitting);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPostSaveWorld,const bool, isQuitting);

#ifdef WORLD_TICK_TRACKING
DECLARE_STATS_GROUP(TEXT("VoxelWorld"), STATGROUP_VoxelWorld, STATCAT_Advanced);
#endif

UCLASS()
	class MCTPLUGIN_API APagedWorld : public AActor {
	GENERATED_BODY()

public:
	APagedWorld();
	~APagedWorld();

protected:
	void BeginPlay() override;
	void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	void PostInitializeComponents() override;

public:
	void Tick(float DeltaTime) override;

	UFUNCTION(Category = "Voxel World", BlueprintCallable)
	void ConnectToDatabase(FString Name);

	UPROPERTY(Category = "VoxelNet", BlueprintReadWrite, EditAnywhere) bool bIsVoxelNetServer = false;
	UPROPERTY(Category = "VoxelNet", BlueprintReadWrite, EditAnywhere) bool bIsVoxelNetSingleplayer = false;

	UPROPERTY(Category = "Voxel World", BlueprintReadWrite, EditAnywhere) bool bRenderMarchingCubes = false;

	UPROPERTY(BlueprintAssignable, Category="Voxel Update Event")
	FVoxelWorldUpdate VoxelWorldUpdate_Event;

	UPROPERTY(BlueprintAssignable, Category="VoxelNet Handshake Event")
	FVoxelNetHandshake VoxelNetHandshake_Event;

	UPROPERTY(BlueprintAssignable, Category="Voxel World - Saving")
	FPreSaveWorld PreSaveWorld_Event;

	UPROPERTY(BlueprintAssignable, Category="Voxel World - Saving")
	FPostSaveWorld PostSaveWorld_Event;

	UPROPERTY(Category = "Voxel World", BlueprintReadOnly, VisibleAnywhere) TMap<FIntVector, APagedRegion*> regions;
	UPROPERTY(Category = "Voxel World", BlueprintReadOnly, VisibleAnywhere) TArray<UTerrainPagingComponent*> pagingComponents;

	UPROPERTY(Category = "Voxel World", BlueprintReadWrite, EditAnywhere) TArray<UMaterialInterface*> TerrainMaterials;
	UPROPERTY(Category = "Voxel World", BlueprintReadOnly, VisibleAnywhere) int32 remainingRegionsToGenerate = 0;

	UPROPERTY(Category = "Voxel World - Database", BlueprintReadOnly, VisibleAnywhere) FString DatabaseName;
	
	// get region
	UFUNCTION(Category = "Voxel World", BlueprintCallable) APagedRegion* getRegionAt(FIntVector pos);

	UFUNCTION(Category = "Voxel World", BlueprintCallable) void RegisterPagingComponent(UTerrainPagingComponent* pagingComponent);

	//render
	UFUNCTION(Category = "Voxel World", BlueprintCallable) void QueueRegionRender(FIntVector pos);
	UFUNCTION(Category = "Voxel World", BlueprintCallable) void MarkRegionDirtyAndAdjacent(FIntVector pos);

	// terrain modification
	UFUNCTION(Category = "Voxel World", BlueprintCallable) bool ModifyVoxel(FIntVector VoxelLocation, uint8 Radius, uint8 Material, uint8 Density, AActor* cause = nullptr, bool bIsSpherical = false);

	// coordinates
	UFUNCTION(Category = "Voxel Coordinates", BlueprintCallable, BlueprintPure) static FIntVector VoxelToRegionCoords(FIntVector voxel);
	UFUNCTION(Category = "Voxel Coordinates", BlueprintCallable, BlueprintPure) static FIntVector
	WorldToVoxelCoords(FVector world);

	// world gen
	UFUNCTION(BlueprintImplementableEvent) const TArray<UUFNNoiseGenerator*> GetNoiseGeneratorArray(); // can this be const?
	UFUNCTION(Category = "Voxel World", BlueprintCallable) void beginWorldGeneration(FIntVector pos);
	UFUNCTION(Category = "Voxel World", BlueprintCallable) void PrefetchRegionsInRadius(FIntVector pos, int32 radius) const;

	// memory
	UFUNCTION(Category = "Voxel World - Volume Memory", BlueprintCallable) int32 getVolumeMemoryBytes() const;
	UFUNCTION(Category = "Voxel World - Volume Memory", BlueprintCallable) void Flush() const;

	UFUNCTION(Category = "Voxel World - Saving", BlueprintCallable) void ForceSaveWorld() ;
	UFUNCTION(Category = "Voxel World - Saving", BlueprintImplementableEvent) void PreSaveWorld();
	UFUNCTION(Category = "Voxel World - Saving", BlueprintImplementableEvent) void PostSaveWorld();

	UFUNCTION(Category = "Voxel World", BlueprintCallable) void PagingComponentTick();
	UFUNCTION(Category = "Voxel World", BlueprintCallable) void UnloadRegionsExcept(TSet<FIntVector> loadedRegions);

	UFUNCTION(Category = "VoxelNet", BlueprintCallable) void RegisterPlayerWithCookie(APlayerController* player, int64 cookie);

	// save the actual voxel data to leveldb , stored under region coords X Y Z W where w 2kb is xy layers
	static void SaveChunkToDatabase(leveldb::DB* db, FIntVector pos, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk* pChunk);
	static bool ReadChunkFromDatabase(leveldb::DB* db, FIntVector pos, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk* pChunk);

	// save region-specific data to leveldb, for instance entities in the region or special local properties. 
	// these are separated by indicies based on categories , ideally with decreasing order of relevance as they are ordered by index
	static void SaveRegionalDataToDatabase(leveldb::DB* db, FIntVector pos, uint8 index, TArray<uint8>& archive);
	static bool LoadRegionalDataFromDatabase(leveldb::DB* db, FIntVector pos, uint8 index, TArray<uint8>& archive);

	//save map-wide data to leveldb. this is still map specific so universal properties should have their own db. because we prepend a 14 byte tag, the key can be anything including ""
	static void SaveGlobalDataToDatabase(leveldb::DB* db, std::string key, TArray<uint8>& archive);
	static bool LoadGlobalDataFromDatabase(leveldb::DB* db, std::string key, TArray<uint8>& archive);

	UFUNCTION(BlueprintCallable) void TempSaveTransformToGlobal(FString key, FTransform value);
	UFUNCTION(BlueprintCallable) FTransform TempLoadTransformToGlobal(FString key);
	
	UFUNCTION(BlueprintCallable) void SaveStringToGlobal(FString s);
	UFUNCTION(BlueprintCallable) FString LoadStringFromGlobal();
public:
	TSharedPtr<PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>> VoxelVolume;
	// 
	TSet<FIntVector> dirtyRegions;
	
	// region keys which need redrawn & recooked; either because their voxels were modified or because they were just created
	TQueue<FVoxelUpdate, EQueueMode::Mpsc> voxelUpdateQueue;
	TQueue<FWorldGenerationTaskOutput, EQueueMode::Mpsc> worldGenerationQueue;

	FCriticalSection VolumeMutex;
	TQueue<FExtractionTaskOutput, EQueueMode::Mpsc> extractionQueue;
	leveldb::DB* worldDB;

	UPROPERTY(BlueprintReadWrite, EditAnywhere) bool bUseAsyncCollision = true;
	
	////////// voxelnet stuff below ///////////////

	TMap<FIntVector, TArray<uint8>> VoxelNetServer_regionPackets;
	TQueue<FPacketTaskOutput, EQueueMode::Mpsc> VoxelNetServer_packetQueue;

	//voxelnet functions
	UFUNCTION(BlueprintCallable)bool VoxelNetServer_StartServer();
	UFUNCTION(BlueprintCallable)bool VoxelNetClient_ConnectToServer(FString ip_str);
	UFUNCTION(BlueprintCallable)int32 VoxelNetClient_GetPendingRegionDownloads() const;

	bool VoxelNetServer_OnConnectionAccepted(FSocket* socket, const FIPv4Endpoint& endpoint);

	TSharedPtr<FTcpListener> VoxelNetServer_ServerListener;
	TArray<TSharedPtr<VoxelNetThreads::VoxelNetServer>> VoxelNetServer_VoxelServers;
	TArray<FRunnableThread*> VoxelNetServer_ServerThreads;

	// after handshake we associate playercontrollers with server threads, for uploading regions
	/*
	 * Server sends handshake
	 * Client receives and sends the cookie thru their playercontroller to the world Calling ServerRegisterPlayerWithCookie
	 * RegisterPlayerWithCookie takes a calling playercontroller and a cookie, looks up the cookie in map and associates Player:Socket
	 *
	 */
	TMap<int64, TSharedPtr<VoxelNetThreads::VoxelNetServer>> VoxelNetServer_SentHandshakes;
	TMap<APlayerController*, TSharedPtr<VoxelNetThreads::VoxelNetServer>> VoxelNetServer_PlayerVoxelServers;

	TSharedPtr<VoxelNetThreads::VoxelNetClient> VoxelNetClient_VoxelClient;
	FRunnableThread* VoxelNetClient_ClientThread;
};

class WorldPager : public PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Pager {
public:
	WorldPager(APagedWorld* World);

	virtual ~WorldPager() {
	};

	void pageIn(const PolyVox::Region& region,
	            PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk* pChunk) override;
	void pageOut(const PolyVox::Region& region,
	             PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk* pChunk) override;

	APagedWorld* world;
};
//https://garvinized.com/posts/2016/voxel-terrain-in-unreal-engine-4-part-3/
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
