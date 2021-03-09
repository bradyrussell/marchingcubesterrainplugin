// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include <string>
#include "CoreMinimal.h"
#include "UFNNoiseGenerator.h"
#include "WorldGeneratorBase.h"


class MCTPLUGIN_API WorldGeneratorMars : public WorldGeneratorBase
{
public:
	const TArray<UUFNNoiseGenerator*> NoiseGenerators;
	
	explicit WorldGeneratorMars(const TArray<UUFNNoiseGenerator*>& NoiseGenerators)
		: NoiseGenerators(NoiseGenerators) {
	}

	/* WorldGenerator Interface */
	int32 GetHeightmap(int32 X, int32 Y) override;
	PolyVox::MaterialDensityPair88 GetVoxel(int32 X, int32 Y, int32 Z) override;
	
	const char* GetGeneratorName() override;
	/* End WorldGenerator Interface */
};
