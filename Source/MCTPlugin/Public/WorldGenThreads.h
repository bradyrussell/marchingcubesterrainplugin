#pragma once
#include "WorldGenInterpreters.h"
#include "Config.h"
#include "PagedWorld.h"

namespace WorldGenThreads {
	////////////////////////////////////////////////////////////////////////
	class RegionGenerationTask : public FNonAbandonableTask {
		friend class FAutoDeleteAsyncTask<RegionGenerationTask>;
		APagedWorld* world;
		FIntVector lower;
	public:
		RegionGenerationTask(APagedWorld* world,
		                     FIntVector lower)
			: world(world), lower(lower) {

		}

		FORCEINLINE TStatId GetStatId() const {
			RETURN_QUICK_DECLARE_CYCLE_STAT(RegionGenerationTask, STATGROUP_ThreadPoolAsyncTasks);
		}

		void DoWork() {
			const TArray<UUFNNoiseGenerator*> noise = world->GetNoiseGeneratorArray();

			FWorldGenerationTaskOutput output;
			output.pos = lower;

			// generate
			for (int32 x = 0; x < REGION_SIZE; x++) {
				for (int32 y = 0; y < REGION_SIZE; y++) {
					for (int32 z = 0; z < REGION_SIZE; z++) {
						if (noise.Num() == 0){
							//todo this happens during game
							UE_LOG(LogVoxelWorld, Error, TEXT("Failed to generate region [%s]: cannot access noise generators."),*lower.ToString());

							if(IsValid(world)){
								//todo fix
								//requeue the region as a workaround
								world->remainingRegionsToGenerate--;
								world->QueueRegionRender(lower);
							}
							return; // this happens if the game quits during worldgen
						}

						output.voxel[x][y][z] = WorldGen::Interpret_Mars(x + lower.X, y + lower.Y, z + lower.Z, noise);
//						output.voxel[x][y][z] = world->WorldGenerationProvider->GetVoxel(x + lower.X, y + lower.Y, z + lower.Z); //0xC0000005: Access violation executing location 0x0000023D00000008.
					}
				}
			}

			world->worldGenerationQueue.Enqueue(output);
			UE_LOG(LogVoxelWorld, Warning, TEXT("Generated voxels for region [%s]."),*lower.ToString());
		}
	};

	////////////////////////////////////////////////////////////////////////
};
