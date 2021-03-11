#pragma once

#include "PolyVox/MaterialDensityPair.h"
#include "UnrealFastNoisePlugin.h"
#include "UnrealFastNoisePlugin/Public/UFNNoiseGenerator.h"

#define MATERIAL_AIR 0
#define MATERIAL_STONE 1
#define MATERIAL_DIRT 2
#define MATERIAL_COAL 3
#define MATERIAL_URANIUM 4
#define MATERIAL_GOLD 5
#define MATERIAL_IRON 6

enum EBiome { PLAINS, COAST, OCEAN, HILLS, MOUNTAINS };

namespace WorldGen {
	PolyVox::MaterialDensityPair88 Interpret_Woods(int32 x, int32 y, int32 z, UUFNNoiseGenerator* material, UUFNNoiseGenerator* heightmap, UUFNNoiseGenerator* biome);
	PolyVox::MaterialDensityPair88 Interpret_AlienSpires(int32 x, int32 y, int32 z, UUFNNoiseGenerator* material, UUFNNoiseGenerator* heightmap, UUFNNoiseGenerator* biome);
	PolyVox::MaterialDensityPair88 Interpret_Basic(int32 x, int32 y, int32 z, UUFNNoiseGenerator* material, UUFNNoiseGenerator* heightmap, UUFNNoiseGenerator* biome);

	PolyVox::MaterialDensityPair88 Interpret_Island(int32 x, int32 y, int32 z,const TArray<UUFNNoiseGenerator*>& noise);
	PolyVox::MaterialDensityPair88 Interpret_Island2(int32 x, int32 y, int32 z,const TArray<UUFNNoiseGenerator*>& noise);
	PolyVox::MaterialDensityPair88 Interpret_Mars(int32 x, int32 y, int32 z,const TArray<UUFNNoiseGenerator*>& noise);
	PolyVox::MaterialDensityPair88 Interpret_New(int32 x, int32 y, int32 z,const TArray<UUFNNoiseGenerator*>& noise);
	EBiome Interpret_Biome(float _height, float temperatue, float moisture);
};
