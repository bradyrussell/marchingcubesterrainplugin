#pragma once
#include "Blueprint/AIAsyncTaskBlueprintProxy.h"
#include "PagedWorld.h"
#include <PolyVox/CubicSurfaceExtractor.h>
#include <PolyVox/VolumeResampler.h>
#include <PolyVox/RawVolume.h>

namespace ExtractionThreads {
	////////////////////////////////////////////////////////////////////////
	class MarchingCubesExtractionTask : public FNonAbandonableTask {
		friend class FAutoDeleteAsyncTask<MarchingCubesExtractionTask>;
		APagedWorld* world;
		FIntVector lower;
	public:
		MarchingCubesExtractionTask(APagedWorld* world, FIntVector lower) :world(world),lower(lower){

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

			if(!world->VoxelVolume.IsValid()) return;
			
			if (world->bIsVoxelNetServer) {
				/*
										   *    We generate the packet in the extraction thread because: 
										   *    that means it has just changed
										   *    it will likely need to be sent anyways in the near future
										   *    and it is already on another thread with a lock, 
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

			// LOD goes here

			// volume resampling LOD here
			
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
	///
	///

	////////////////////////////////////////////////////////////////////////
	class CubicExtractionTask : public FNonAbandonableTask {
		friend class FAutoDeleteAsyncTask<CubicExtractionTask>;
		APagedWorld* world;
		FIntVector lower;
	public:
		CubicExtractionTask(APagedWorld* world, FIntVector lower) :world(world),lower(lower){

		}

		FORCEINLINE TStatId GetStatId() const {
			RETURN_QUICK_DECLARE_CYCLE_STAT(ExtractionTask, STATGROUP_ThreadPoolAsyncTasks);
		}

		void DoWork() {
			FExtractionTaskOutput output;
			output.section.AddDefaulted(MAX_MATERIALS);
			output.region = lower;

			PolyVox::Region ToExtract(PolyVox::Vector3DInt32(lower.X, lower.Y, lower.Z),
			                          PolyVox::Vector3DInt32(lower.X + REGION_SIZE-1, lower.Y + REGION_SIZE-1,
			                                                 lower.Z + REGION_SIZE-1)); // not sure why cubic needs -1, or perhaps its MC that needs +1 to cover gaps?

			world->VolumeMutex.Lock();

			if(!world->VoxelVolume.IsValid()) return;
			
			if (world->bIsVoxelNetServer) {
				/*
										   *    We generate the packet in the extraction thread because: 
										   *    that means it has just changed
										   *    it will likely need to be sent anyways in the near future
										   *    and it is already on another thread with a lock, 
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

			// LOD goes here
			//if lod  divison factor > 0
			int32 DivisionFactor = 8;

			DivisionFactor = FMath::Max(DivisionFactor, 1);
			
			PolyVox::Mesh<PolyVox::CubicVertex<PolyVox::MaterialDensityPair88>> ExtractedMesh;
			
			if(DivisionFactor > 1){
				PolyVox::RawVolume<PolyVox::MaterialDensityPair88> LodVolume(PolyVox::Region(0,0,0,(REGION_SIZE / DivisionFactor)-1,(REGION_SIZE / DivisionFactor)-1,(REGION_SIZE / DivisionFactor)-1));
				
				PolyVox::VolumeResampler<PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>,PolyVox::RawVolume<PolyVox::MaterialDensityPair88>> volumeResampler(world->VoxelVolume.Get(), ToExtract, &LodVolume, LodVolume.getEnclosingRegion());
				volumeResampler.execute();

				// i really need to not render external faces
				
				//todo can i instead multiply the verts by the lod divisor??
				//PolyVox::RawVolume<PolyVox::MaterialDensityPair88> ResizedVolume(PolyVox::Region(0,0,0,REGION_SIZE,REGION_SIZE,REGION_SIZE));

				//PolyVox::VolumeResampler<PolyVox::RawVolume<PolyVox::MaterialDensityPair88>,PolyVox::RawVolume<PolyVox::MaterialDensityPair88>> volumeResampler2(&LodVolume, LodVolume.getEnclosingRegion(), &ResizedVolume, ResizedVolume.getEnclosingRegion());
				//volumeResampler2.execute();
				
				// volume resampling LOD here
				//ResizedVolume.setBorderValue(PolyVox::MaterialDensityPair88(1,255)); // hopefully reduces unneeded faces
				//ExtractedMesh = PolyVox::extractCubicMesh(&ResizedVolume, ResizedVolume.getEnclosingRegion());
				ExtractedMesh = PolyVox::extractCubicMesh(&LodVolume, LodVolume.getEnclosingRegion());
			} else {
				ExtractedMesh = PolyVox::extractCubicMesh(world->VoxelVolume.Get(), ToExtract);
			}
			
			world->VolumeMutex.Unlock();

			auto decoded = PolyVox::decodeMesh(ExtractedMesh);

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
							output.section[Material].Vertices.Add((FPolyVoxVector(Vertex2.position) + OffsetLocation/DivisionFactor) * VOXEL_SIZE *DivisionFactor));

						Index = decoded.getIndex(i + 1);
						auto Vertex1 = decoded.getVertex(Index);
						output.section[Material].Indices.Add(
							output.section[Material].Vertices.Add(
								(FPolyVoxVector(Vertex1.position) +
									OffsetLocation/DivisionFactor) * VOXEL_SIZE *DivisionFactor));

						Index = decoded.getIndex(i);
						auto Vertex0 = decoded.getVertex(Index);
						output.section[Material].Indices.Add(
							output.section[Material].Vertices.Add(
								(FPolyVoxVector(Vertex0.position) +
									OffsetLocation/DivisionFactor) * VOXEL_SIZE *DivisionFactor));

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
