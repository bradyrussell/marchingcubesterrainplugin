#pragma once

#include "PolyVox/MaterialDensityPair.h"
#include "UnrealFastNoisePlugin.h"
#include "UnrealFastNoisePlugin/Public/UFNNoiseGenerator.h"

namespace WorldGen {

	PolyVox::MaterialDensityPair88 Interpret_AlienSpires(int32 x, int32 y, int32 z, UUFNNoiseGenerator* material,UUFNNoiseGenerator* heightmap,UUFNNoiseGenerator* biome);
	PolyVox::MaterialDensityPair88 Interpret_Basic(int32 x, int32 y, int32 z, UUFNNoiseGenerator* material, UUFNNoiseGenerator* heightmap, UUFNNoiseGenerator* biome);
};