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
	void ConnectToDatabase();

	UPROPERTY(Category = "VoxelNet", BlueprintReadWrite, EditAnywhere) bool bIsVoxelNetServer = false;
	UPROPERTY(Category = "VoxelNet", BlueprintReadWrite, EditAnywhere) bool bIsVoxelNetSingleplayer = false;

	UPROPERTY(BlueprintAssignable, Category="Voxel Update Event")
	FVoxelWorldUpdate VoxelWorldUpdate_Event;

		UPROPERTY(BlueprintAssignable, Category="VoxelNet Handshake Event")
	FVoxelNetHandshake VoxelNetHandshake_Event;

	UPROPERTY(Category = "Voxel World", BlueprintReadOnly, VisibleAnywhere) TMap<FIntVector, APagedRegion*> regions;
	UPROPERTY(Category = "Voxel World", BlueprintReadOnly, VisibleAnywhere) TArray<UTerrainPagingComponent*> pagingComponents;

	UPROPERTY(Category = "Voxel World", BlueprintReadWrite, EditAnywhere) TArray<UMaterialInterface*> TerrainMaterials;
	UPROPERTY(Category = "Voxel World", BlueprintReadOnly, VisibleAnywhere) int32 remainingRegionsToGenerate = 0;

	// get region
	UFUNCTION(Category = "Voxel World", BlueprintCallable) APagedRegion* getRegionAt(FIntVector pos);

	UFUNCTION(Category = "Voxel World", BlueprintCallable) void RegisterPagingComponent(UTerrainPagingComponent* pagingComponent);

	//render
	UFUNCTION(Category = "Voxel World", BlueprintCallable) void QueueRegionRender(FIntVector pos);
	UFUNCTION(Category = "Voxel World", BlueprintCallable) void MarkRegionDirtyAndAdjacent(FIntVector pos);

	// terrain modification
	UFUNCTION(Category = "Voxel World", BlueprintCallable) bool ModifyVoxel(FIntVector pos, uint8 r, uint8 m, uint8 d, AActor* cause = nullptr, bool bIsSpherical = false);

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

	UFUNCTION(Category = "Voxel World - Saving", BlueprintCallable) void ForceSaveWorld() const;
	UFUNCTION(Category = "Voxel World - Saving", BlueprintImplementableEvent) void PreSaveWorld() const;
	UFUNCTION(Category = "Voxel World - Saving", BlueprintImplementableEvent) void PostSaveWorld() const;

	UFUNCTION(Category = "Voxel World", BlueprintCallable) void PagingComponentTick();
	UFUNCTION(Category = "Voxel World", BlueprintCallable) void UnloadRegionsExcept(TSet<FIntVector> loadedRegions);

	UFUNCTION(Category = "VoxelNet", BlueprintCallable) void RegisterPlayerWithCookie(APlayerController* player, int64 cookie);

	// save the actual voxel data to leveldb , stored under region coords X Y Z W where w 2kb is xy layers
	void SaveChunkToDatabase(leveldb::DB* db, FIntVector pos, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk* pChunk);
	bool ReadChunkFromDatabase(leveldb::DB* db, FIntVector pos, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk* pChunk);

	// save region-specific data to leveldb, for instance entities in the region or special local properties. 
	// these are separated by indicies based on categories , ideally with decreasing order of relevance as they are ordered by index
	void SaveRegionalDataToDatabase(leveldb::DB* db, FIntVector pos, uint8 index, TArray<uint8>& archive);
	bool LoadRegionalDataFromDatabase(leveldb::DB* db, FIntVector pos, uint8 index, TArray<uint8>& archive);

	//save map-wide data to leveldb. this is still map specific so universal properties should have their own db. because we prepend a 14 byte tag, the key can be anything including ""
	void SaveGlobalDataToDatabase(leveldb::DB* db, std::string key, TArray<uint8>& archive);
	bool LoadGlobalDataFromDatabase(leveldb::DB* db, std::string key, TArray<uint8>& archive);

	UFUNCTION(BlueprintCallable) void SaveStringToGlobal(FString s);
	UFUNCTION(BlueprintCallable) FString LoadStringFromGlobal();
public:
	TSharedPtr<PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>> VoxelVolume;
	// 
	TSet<FIntVector> dirtyRegions;
	// region keys which need redrawn & recooked; either because their voxels were modified or because they were just created
	TQueue<FVoxelUpdate, EQueueMode::Mpsc> voxelUpdateQueue;
	TQueue<FWorldGenerationTaskOutput, EQueueMode::Mpsc> worldGenerationQueue;
	//TQueue<FIntVector, EQueueMode::Mpsc> dirtyRegions;

	FCriticalSection VolumeMutex;
	TQueue<FExtractionTaskOutput, EQueueMode::Mpsc> extractionQueue;
	leveldb::DB* worldDB;
	////////// voxelnet stuff below ///////////////

	TMap<FIntVector, TArray<uint8>> VoxelNetServer_regionPackets;
	TQueue<FPacketTaskOutput, EQueueMode::Mpsc> VoxelNetServer_packetQueue;

	//voxelnet functions
	UFUNCTION(BlueprintCallable)bool VoxelNetServer_StartServer();
	UFUNCTION(BlueprintCallable)bool VoxelNetClient_ConnectToServer(FString ip_str);


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

namespace WorldGenThread {
	////////////////////////////////////////////////////////////////////////
	class RegionGenerationTask : public FNonAbandonableTask {
		friend class FAutoDeleteAsyncTask<RegionGenerationTask>;
		APagedWorld* world;
		FIntVector lower;
	public:
		RegionGenerationTask(APagedWorld* world,
		                     FIntVector lower/*, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk * pChunk*/):world(world),lower(lower) {

		}

		FORCEINLINE TStatId GetStatId() const {
			RETURN_QUICK_DECLARE_CYCLE_STAT(RegionGenerationTask, STATGROUP_ThreadPoolAsyncTasks);
		}

		void DoWork() {
			TArray<UUFNNoiseGenerator*> noise = world->GetNoiseGeneratorArray();

			FWorldGenerationTaskOutput output;
			output.pos = lower;

			// generate
			for (int32 x = 0; x < REGION_SIZE; x++) {
				for (int32 y = 0; y < REGION_SIZE; y++) {
					for (int32 z = 0; z < REGION_SIZE; z++) {
						if (noise.Num() == 0)
							return; // this happens if the game quits during worldgen
						// todo save function ptr to interp as param that way we can change them on the fly
						output.voxel[x][y][z] = WorldGen::Interpret_Mars(x + lower.X, y + lower.Y, z + lower.Z, noise);
					}
				}
			}

			// i can do multiple stages of generation here

			world->worldGenerationQueue.Enqueue(output);
		}
	};

	////////////////////////////////////////////////////////////////////////
};

namespace ExtractionThread {
	////////////////////////////////////////////////////////////////////////
	class ExtractionTask : public FNonAbandonableTask {
		friend class FAutoDeleteAsyncTask<ExtractionTask>;
		APagedWorld* world;
		FIntVector lower;
	public:
		ExtractionTask(APagedWorld* world, FIntVector lower) :world(world),lower(lower){

		}

		FORCEINLINE TStatId GetStatId() const {
			RETURN_QUICK_DECLARE_CYCLE_STAT(ExtractionTask, STATGROUP_ThreadPoolAsyncTasks);
		}

		void DoWork() {
			FExtractionTaskOutput output;
			output.section.AddDefaulted(MAX_MATERIALS);
			output.region = lower;

			PolyVox::Region ToExtract(PolyVox::Vector3DInt32(lower.X, lower.Y, lower.Z),
			                          PolyVox::Vector3DInt32(lower.X + REGION_SIZE, lower.Y + REGION_SIZE,
			                                                 lower.Z + REGION_SIZE));

			world->VolumeMutex.Lock();
			if (world->bIsVoxelNetServer) {
				/*
										   * We generate the packet in the extraction thread because: 
										   *  that means it has just changed
										   *  it will likely need to be sent anyways in the near future
										   *  and it is already on another thread with a lock, 
										   *	where locks are our current biggest slowdown
										   *	this has be advantage of not being dos-vulnerable and allowing packet updates to be quick
										   *	so far it has not been a significant issue for performance
										   */
				// begin packet generation
				FPacketTaskOutput packetOutput;
				Packet::RegionData packet;
				packet.x = lower.X;
				packet.y = lower.Y;
				packet.z = lower.Z;

				for (int32 x = 0; x < REGION_SIZE; x++) {
					for (int32 y = 0; y < REGION_SIZE; y++) {
						for (int32 z = 0; z < REGION_SIZE; z++) {
							auto voxel = world->VoxelVolume.Get()->getVoxel(lower.X + x, lower.Y + y, lower.Z + z);
							packet.data[0][x][y][z] = voxel.getMaterial();
							packet.data[1][x][y][z] = voxel.getDensity();
						}
					}
				}

				FBufferArchive packetArchive(true);
				Packet::MakeRegionContents(packetArchive, packet);

				packetOutput.region = lower;
				packetOutput.packet = packetArchive; // this is intentional, is there a better way to value initialize it?

				world->VoxelNetServer_packetQueue.Enqueue(packetOutput);
			}
			// end packet generation

			auto ExtractedMesh = extractMarchingCubesMesh(world->VoxelVolume.Get(), ToExtract);
			world->VolumeMutex.Unlock();

			auto decoded = decodeMesh(ExtractedMesh);

			//output.decoded = DecodedMesh;

			FVector OffsetLocation = FVector(lower);

			if (decoded.getNoOfIndices() == 0)
				return;


			for (int32 Material = 0; Material < world->TerrainMaterials.Num(); Material++) {
				// Loop over all of the triangle vertex indices
				for (uint32 i = 0; i < decoded.getNoOfIndices() - 2; i += 3) {
					// We need to add the vertices of each triangle in reverse or the mesh will be upside down
					auto Index = decoded.getIndex(i + 2);
					auto Vertex2 = decoded.getVertex(Index);
					auto TriangleMaterial = Vertex2.data.getMaterial();

					// Before we continue, we need to be sure that the triangle is the right material; we don't want to use verticies from other materials
					if (TriangleMaterial == (Material + 1)) {
						// If it is of the same material, then we need to add the correct indices now
						output.section[Material].Indices.Add(
							output.section[Material].Vertices.Add(
								(FPolyVoxVector(Vertex2.position) +
									OffsetLocation) * VOXEL_SIZE));

						Index = decoded.getIndex(i + 1);
						auto Vertex1 = decoded.getVertex(Index);
						output.section[Material].Indices.Add(
							output.section[Material].Vertices.Add(
								(FPolyVoxVector(Vertex1.position) +
									OffsetLocation) * VOXEL_SIZE));

						Index = decoded.getIndex(i);
						auto Vertex0 = decoded.getVertex(Index);
						output.section[Material].Indices.Add(
							output.section[Material].Vertices.Add(
								(FPolyVoxVector(Vertex0.position) +
									OffsetLocation) * VOXEL_SIZE));

						// Calculate the tangents of our triangle
						const FVector Edge01 = FPolyVoxVector(Vertex1.position - Vertex0.position);
						const FVector Edge02 = FPolyVoxVector(Vertex2.position - Vertex0.position);

						const FVector TangentX = Edge01.GetSafeNormal();
						FVector TangentZ = (Edge01 ^ Edge02).GetSafeNormal();

						for (int32 n = 0; n < 3; n++) {
							output.section[Material].Tangents.Add(FRuntimeMeshTangent(TangentX, false));
							output.section[Material].Normals.Add(TangentZ);
						}
					}
				}
			}
			//////////////////////////

			world->extractionQueue.Enqueue(output);
		}
	};

	////////////////////////////////////////////////////////////////////////
};
