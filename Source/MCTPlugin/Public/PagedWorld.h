#pragma once

#include "UnrealFastNoisePlugin.h"
#include "UnrealFastNoisePlugin/Public/UFNNoiseGenerator.h"
#include "PolyVox/PagedVolume.h"
#include "PolyVox/MaterialDensityPair.h"
#include "PolyVox/Vector.h"
#include "PolyVox/MarchingCubesSurfaceExtractor.h"
#include "GameFramework/Actor.h"
#include "RuntimeMeshComponent.h"
#include "LevelDatabase.h"
#include "CoreMinimal.h"
#include "VoxelNetThreads.h"
#include "Networking/Public/Common/TcpListener.h"
#include "Structs.h"
#include "StorageProviderBase.h"
#include "WorldGeneratorBase.h"
#include "PagedWorld.generated.h"

//cleaned up

class APagedRegion;
class UTerrainPagingComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FVoxelWorldUpdate, class AActor*, CauseActor, const FIntVector&, voxelLocation, const uint8, oldMaterial, const uint8, newMaterial, const bool, bShouldDrop);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVoxelNetHandshake,const int64, cookie);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPreSaveWorld,const bool, isQuitting);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPostSaveWorld,const bool, isQuitting);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRegionGenerated,const FIntVector&, Region);


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
	void VoxelUpdatesTick();
	void WorldNewRegionsTick();
	void VoxelNetClientTick();
	void VoxelNetServerTick();
	void PostInitializeComponents() override;
	void BeginDestroy() override;

	// PIE is not saving properly it just exits immediately without cleaning anything up...
	

	bool bHasStarted = false;
	bool bHasShutdown = false;
	


public:
	UFUNCTION(Category = "Voxel World", BlueprintCallable) virtual void SaveAndShutdown();
	void Tick(float DeltaTime) override;
	//debug
	UFUNCTION(Category = "Voxel World|Saving", BlueprintImplementableEvent) void OnRegionError(const FIntVector& Region);
	UPROPERTY(BlueprintReadWrite, EditAnywhere) TSet<FIntVector> PacketsToSendOrResendToSubscribersNextExtraction;
	UPROPERTY(BlueprintReadWrite, EditAnywhere) TSet<FIntVector> PacketsReadyToSendOrResend;
	UFUNCTION(Category = "Voxel World", BlueprintCallable) void ResendRegion(const FIntVector& region);
	// save as much data as possible to mitigate crash
	virtual void OnFatalError();

	// if true, creates CORE_COUNT threads per pool, if false CORE_COUNT / POOL_NUM per pool
	UPROPERTY(BlueprintReadWrite, EditAnywhere) bool bShareCores; // dont know which would be faster
	
	FQueuedThreadPool* VoxelWorldThreadPool;
	
	/* World */
	UFUNCTION(Category = "Voxel World", BlueprintCallable) APagedRegion* getRegionAt(const FIntVector& pos);
	UFUNCTION(Category = "Voxel World", BlueprintCallable) bool isRegionReadyServer(const FIntVector& pos);
	UFUNCTION(Category = "Voxel World", BlueprintCallable) bool isRegionReadyLocal(const FIntVector& pos);
	UFUNCTION(Category = "Voxel World", BlueprintCallable) bool isRegionEmptyServer(const FIntVector& pos);
	UFUNCTION(Category = "Voxel World", BlueprintCallable) bool isRegionEmptyLocal(const FIntVector& pos);
	UFUNCTION(Category = "Voxel World|Coordinates", BlueprintPure) static FIntVector VoxelToRegionCoords(const FIntVector& VoxelCoords);
	UFUNCTION(Category = "Voxel World|Coordinates", BlueprintPure) static FIntVector WorldToVoxelCoords(const FVector& WorldCoords);
	UFUNCTION(Category = "Voxel World|Coordinates", BlueprintPure) static FVector VoxelToWorldCoords(const FIntVector& VoxelCoords);
	UFUNCTION(Category = "Voxel World|Coordinates", BlueprintPure) static FIntVector VoxelToLocalVoxelCoords(const FIntVector& VoxelCoords);
	UFUNCTION(Category = "Voxel World|Coordinates", BlueprintPure) static FIntVector LocalVoxelToVoxelCoords(const FIntVector& LocalVoxelCoords, const FIntVector& RegionCoords);
	UFUNCTION(Category = "Voxel World", BlueprintCallable, NetMulticast, Reliable) void Multi_ModifyVoxel(const FIntVector& VoxelLocation, uint8 Radius, uint8 Material, uint8 Density, AActor* cause = nullptr, bool bIsSpherical = false, bool bShouldDrop = true);
	//UFUNCTION(Category = "Voxel World", BlueprintCallable, Server, Reliable, WithValidation) void Server_ModifyVoxel(const FIntVector& VoxelLocation, uint8 Radius, uint8 Material, uint8 Density, AActor* cause = nullptr, bool bIsSpherical = false, bool bShouldDrop = true);
	UPROPERTY(BlueprintAssignable, Category="Voxel Update Event") FVoxelWorldUpdate VoxelWorldUpdate_Event;
	UPROPERTY(BlueprintReadWrite, EditAnywhere) bool bUseAsyncCollision = true;
	UPROPERTY(Category = "Voxel World|Seed",BlueprintReadWrite, EditAnywhere, Replicated) int32 WorldSeed;
	UPROPERTY(Category = "Voxel World|Name",BlueprintReadWrite, EditAnywhere) FString WorldName = "WorldDatabase";
	TSharedPtr<PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>> VoxelVolume;
	// lock for VoxelVolume
	FCriticalSection VolumeMutex; 
	TQueue<FVoxelUpdate, EQueueMode::Mpsc> voxelUpdateQueue;
	//These regions will remain pinned 
	UPROPERTY(BlueprintReadWrite, EditAnywhere) TSet<FIntVector> ForceLoadedRegions; 
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly) void PinRegionsInRadius(const FIntVector& VoxelCoords, int32 Radius);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly) void UnpinRegionsInRadius(const FIntVector& VoxelCoords, int32 Radius);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly) void ClearPinnedRegions();

	// copy a region into an array we can access from bp
	UFUNCTION(Category = "Voxel World|Region Voxels", BlueprintCallable, BlueprintAuthorityOnly) FRegionVoxels GetRegionVoxels(const FIntVector& RegionCoords);
	UFUNCTION(Category = "Voxel World|Region Voxels", BlueprintCallable, BlueprintPure) static uint8 GetRegionVoxelMaterial(const FRegionVoxels& RegionVoxels, FIntVector LocalVoxelCoordinates);
	UFUNCTION(Category = "Voxel World|Region Voxels", BlueprintCallable, BlueprintPure) static uint8 GetRegionVoxelDensity(const FRegionVoxels& RegionVoxels, FIntVector LocalVoxelCoordinates);
	
	/* Database */
	UFUNCTION(Category = "Voxel World|Database", BlueprintCallable) void ConnectToDatabase(FString Name);
	StorageProviderBase* WorldStorageProvider;

	/* Memory */
	UFUNCTION(Category = "Voxel World|Volume Memory", BlueprintCallable) void CreateVolume(int32 MemoryTargetMB);
	UPROPERTY(Category = "Voxel World|Volume Memory",BlueprintReadWrite, EditDefaultsOnly) int32 VolumeTargetMemoryMB = 64;
	UPROPERTY(Category = "Voxel World|Volume Memory",BlueprintReadWrite, EditDefaultsOnly) int32 ClientVolumeTargetMemoryMB = 64;
	UFUNCTION(Category = "Voxel World|Volume Memory", BlueprintCallable) int32 getVolumeMemoryBytes() const;
	UFUNCTION(Category = "Voxel World|Volume Memory", BlueprintCallable) void Flush() const;

	/* Startup Helpers */
	UFUNCTION(Category = "Voxel World|Launch", BlueprintCallable) void LaunchServer();
	UFUNCTION(Category = "Voxel World|Launch", BlueprintCallable) void LaunchSingleplayer();
	UFUNCTION(Category = "Voxel World|Launch", BlueprintCallable) void LaunchClient(FString Host, int32 Port = 0);

	/* Saving */
	UFUNCTION(Category = "Voxel World|Saving", BlueprintCallable, BlueprintAuthorityOnly) void ForceSaveWorld() ;
	UFUNCTION(Category = "Voxel World|Saving", BlueprintCallable, BlueprintAuthorityOnly) void PreSaveWorld();
	UFUNCTION(Category = "Voxel World|Saving", BlueprintCallable, BlueprintAuthorityOnly) void PostSaveWorld();
	UFUNCTION(Category = "Voxel World|Saving", BlueprintImplementableEvent) void OnPreSaveWorld();
	UFUNCTION(Category = "Voxel World|Saving", BlueprintImplementableEvent) void OnPostSaveWorld();
	UFUNCTION(Category = "Voxel World|Saving", BlueprintCallable, BlueprintAuthorityOnly) void SaveGlobalString(FString Key, FString Value);
	UFUNCTION(Category = "Voxel World|Saving", BlueprintCallable, BlueprintAuthorityOnly) bool LoadGlobalString(FString Key, FString& Value);
	UFUNCTION(Category = "Voxel World|Saving", BlueprintCallable, BlueprintAuthorityOnly) void SaveAllDataForRegions(TSet<FIntVector> Regions);
	UFUNCTION(Category = "Voxel World|Saving", BlueprintCallable, BlueprintAuthorityOnly) void LoadAllDataForRegions(TSet<FIntVector> Regions);
	UFUNCTION(Category = "Voxel World|Saving", BlueprintCallable, BlueprintAuthorityOnly) void SavePlayerActor(FString Identifier, AActor* ActorToSerialize);
	UFUNCTION(Category = "Voxel World|Saving", BlueprintCallable, BlueprintAuthorityOnly) bool LoadPlayerActor(FString Identifier, AActor* ExistingActor, bool bSetTransform, FTransform& OutTransform);
	UFUNCTION(Category = "Voxel World|Saving", BlueprintCallable, BlueprintAuthorityOnly) bool LoadAndSpawnPlayerActor(FString Identifier, AActor*& OutSpawnedActor);
	UPROPERTY(BlueprintAssignable, Category="Voxel World|Saving") FPreSaveWorld PreSaveWorld_Event;
	UPROPERTY(BlueprintAssignable, Category="Voxel World|Saving") FPostSaveWorld PostSaveWorld_Event;

	/* Actor Ref Persistence */
	UPROPERTY(BlueprintReadWrite, VisibleAnywhere) TMap<int64, AActor*> LivePersistentActors;
	UPROPERTY(BlueprintReadWrite, VisibleAnywhere) int64 NextPersistentActorID = 1; // 0 is reserved for null

	
	//returns the actor or null if not loaded
	UFUNCTION(Category = "Voxel World|Persistent Actors", BlueprintCallable, BlueprintAuthorityOnly)  AActor* GetPersistentActor(int64 ID);

	//create a new ID and register the actor. returns ID
	UFUNCTION(Category = "Voxel World|Persistent Actors", BlueprintCallable, BlueprintAuthorityOnly)  int64 RegisterNewPersistentActor(AActor* Actor);

	//look up the ID for an actor or 0 if it doesnt exist
	UFUNCTION(Category = "Voxel World|Persistent Actors", BlueprintCallable, BlueprintAuthorityOnly)  int64 LookupPersistentActorID(AActor* Actor);

	//call when a persistent actor with an ID is loaded
	UFUNCTION(Category = "Voxel World|Persistent Actors", BlueprintCallable, BlueprintAuthorityOnly)  void RegisterExistingPersistentActor(AActor* Actor, int64 ID);

	//call when a persistent actor with an ID is unloaded
	UFUNCTION(Category = "Voxel World|Persistent Actors", BlueprintCallable, BlueprintAuthorityOnly)  void UnregisterPersistentActor(int64 ID);
	
	/* Generation */
	UFUNCTION(BlueprintImplementableEvent) const TArray<UUFNNoiseGenerator*> GetNoiseGeneratorArray(); 
	UFUNCTION(Category = "Voxel World|Generation", BlueprintCallable) void BeginWorldGeneration(const FIntVector& RegionCoords);
	UFUNCTION(Category = "Voxel World|Generation", BlueprintCallable) int32 GetRegionSeed(const FIntVector& RegionCoords);
	UFUNCTION(Category = "Voxel World|Generation", BlueprintCallable, BlueprintAuthorityOnly) float GetHeightmapZ(int32 VoxelX, int32 VoxelY, uint8 HeightmapIndex = 0);
	UFUNCTION(Category = "Voxel World|Generation", BlueprintCallable) void PrefetchRegionsInRadius(const FIntVector& pos, int32 radius) const;
	UFUNCTION(Category = "Voxel World|Generation", BlueprintCallable) void RegisterPagingComponent(UTerrainPagingComponent* pagingComponent);
	UFUNCTION(Category = "Voxel World|Generation", BlueprintCallable) void PagingComponentTick();
	UFUNCTION(Category = "Voxel World|Generation", BlueprintCallable) void UnloadRegionsExcept(TSet<FIntVector> loadedRegions);
	UPROPERTY(Category = "Voxel World|Generation", BlueprintReadOnly, VisibleAnywhere) TMap<FIntVector, APagedRegion*> regions;
	UPROPERTY(Category = "Voxel World|Generation", BlueprintReadOnly, VisibleAnywhere) TArray<UTerrainPagingComponent*> pagingComponents;
	UPROPERTY(Category = "Voxel World|Generation", BlueprintReadOnly, VisibleAnywhere) int32 remainingRegionsToGenerate = 0;// todo replace with below
	int32 RegionSeedRandomX;
	int32 RegionSeedRandomY;
	int32 RegionSeedRandomZ;

	UPROPERTY(BlueprintAssignable, Category="Voxel World|Generation") FRegionGenerated RegionGenerated_Event;
	//WorldGeneratorBase * WorldGenerationProvider;
	TQueue<FWorldGenerationTaskOutput, EQueueMode::Mpsc> worldGenerationQueue;
	float PagingComponentTickTimer = 0;
	UPROPERTY(Category = "Voxel World|Generation", BlueprintReadWrite, EditAnywhere) float PagingComponentTickRate = 1.f;

	// records the highest Z region of any given (X,Y,0) key
	UPROPERTY(Category = "Voxel World|Generation", BlueprintReadOnly, VisibleAnywhere) TMap<FIntVector, int32> HighestGeneratedRegion;

	UFUNCTION(Category = "Voxel World|Generation", BlueprintCallable, BlueprintAuthorityOnly) int32 GetHighestGeneratedRegionAt(int32 RegionX, int32 RegionY);
	void SetHighestGeneratedRegionAt(int32 RegionX, int32 RegionY, int32 RegionZ);
	
	/* Rendering */
	UPROPERTY(Category = "Voxel World|Rendering", BlueprintReadWrite, EditAnywhere) bool bRenderMarchingCubes = false;
	UPROPERTY(Category = "Voxel World|Rendering", BlueprintReadWrite, EditDefaultsOnly) TArray<UMaterialInterface*> TerrainMaterials;
	UFUNCTION(Category = "Voxel World|Rendering", BlueprintCallable) void QueueRegionRender(const FIntVector& pos);
	UFUNCTION(Category = "Voxel World|Rendering", BlueprintCallable) void MarkRegionDirtyAndAdjacent(const FIntVector& pos);
	TSet<FIntVector> dirtyRegions;// region keys which need redrawn & recooked; either because their voxels were modified or because they were just created
	TQueue<FExtractionTaskOutput, EQueueMode::Mpsc> extractionQueue;
	UPROPERTY(Category = "Voxel World|Generation", BlueprintReadOnly, VisibleAnywhere) int32 NumRegionsPendingExtraction = 0;

	/* Networking */
	UPROPERTY(Category = "Voxel World|Networking", BlueprintReadWrite, EditAnywhere) bool bIsVoxelNetServer = false;
	UPROPERTY(Category = "Voxel World|Networking", BlueprintReadWrite, EditAnywhere) bool bIsVoxelNetSingleplayer = false;
	UPROPERTY(BlueprintAssignable, Category="VoxelNet Handshake Event") FVoxelNetHandshake VoxelNetHandshake_Event;

	/* Voxelnet Server */
	UFUNCTION(Category = "Voxel World|Networking|Server", BlueprintCallable)bool VoxelNetServer_StartServer();
	UFUNCTION(Category = "Voxel World|Networking|Server", BlueprintCallable) void RegisterPlayerWithCookie(APlayerController* player, int64 cookie);
	UFUNCTION(Category = "Voxel World|Networking|Server", BlueprintCallable) void DisconnectPlayerFromVoxelNet(APlayerController* player);
	bool VoxelNetServer_SendPacketsToPagingComponent(UTerrainPagingComponent*& pager, TArray<TArray<uint8>> packets);
	TMap<FIntVector, TArray<uint8>> VoxelNetServer_regionPackets;
	TQueue<FPacketTaskOutput, EQueueMode::Mpsc> VoxelNetServer_packetQueue;
	bool VoxelNetServer_OnConnectionAccepted(FSocket* socket, const FIPv4Endpoint& endpoint);
	TSharedPtr<FTcpListener> VoxelNetServer_ServerListener;
	TArray<TSharedPtr<VoxelNetThreads::VoxelNetServer>> VoxelNetServer_VoxelServers;
	TArray<FRunnableThread*> VoxelNetServer_ServerThreads;
	// after handshake we associate playercontrollers with server threads, for uploading regions
	//	Server sends handshake
	//	Client receives and sends the cookie thru their playercontroller to the world Calling ServerRegisterPlayerWithCookie
	//	RegisterPlayerWithCookie takes a calling playercontroller and a cookie, looks up the cookie in map and associates Player:Socket
	TMap<int64, TSharedPtr<VoxelNetThreads::VoxelNetServer>> VoxelNetServer_SentHandshakes;
	TMap<APlayerController*, TSharedPtr<VoxelNetThreads::VoxelNetServer>> VoxelNetServer_PlayerVoxelServers;

	/* Voxelnet Client */
	UFUNCTION(Category = "Voxel World|Networking|Client", BlueprintCallable)bool VoxelNetClient_ConnectToServer(FString Host, int32 Port = 0);
	UFUNCTION(Category = "Voxel World|Networking|Client", BlueprintCallable)bool VoxelNetClient_DisconnectFromServer();
	UFUNCTION(Category = "Voxel World|Networking|Client", BlueprintCallable)int32 VoxelNetClient_GetPendingRegionDownloads() const;
	TSharedPtr<VoxelNetThreads::VoxelNetClient> VoxelNetClient_VoxelClient;
	FRunnableThread* VoxelNetClient_ClientThread;

	FCriticalSection DEBUG_pagedRegionsLock;
	TSet<FIntVector> DEBUG_pagedRegions;
	int32 DEBUG_pagedRegionsCounter; // curious as to whether this will be different
	
	UFUNCTION(Category = "Voxel World|DEBUG", BlueprintCallable)int32 DEBUG_GetPagedRegionCount();
	UFUNCTION(Category = "Voxel World|DEBUG", BlueprintCallable)int32 DEBUG_GetPagedRegionCounter();
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

class UEPolyvoxLogger : public  PolyVox::Logger
{
public:
       UEPolyvoxLogger() : Logger() {}
       virtual ~UEPolyvoxLogger() {}

       void logTraceMessage(const std::string& message) { UE_LOG(LogVoxelWorld, Verbose, TEXT("[PolyVox] %hs"), message.c_str()); }
       void logDebugMessage(const std::string& message) { UE_LOG(LogVoxelWorld, Verbose, TEXT("[PolyVox] %hs"), message.c_str()); }
       void logInfoMessage(const std::string& message) { UE_LOG(LogVoxelWorld, Display, TEXT("[PolyVox] %hs"), message.c_str()); }
       void logWarningMessage(const std::string& message) { UE_LOG(LogVoxelWorld, Warning, TEXT("[PolyVox] %hs"), message.c_str()); }
       void logErrorMessage(const std::string& message) { UE_LOG(LogVoxelWorld, Error, TEXT("[PolyVox] %hs"), message.c_str()); }
       void logFatalMessage(const std::string& message) { UE_LOG(LogVoxelWorld, Fatal, TEXT("[PolyVox] %hs"), message.c_str()); }
};