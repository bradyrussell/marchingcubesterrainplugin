// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include <string>
#include "CoreMinimal.h"
#include "UFNNoiseGenerator.h"
#include "WorldGeneratorBase.h"


class MCTPLUGIN_API WorldGeneratorPlains: public WorldGeneratorBase
{
public:
	
	/* WorldGenerator Interface */
	int32 GetHeightmap(int32 X, int32 Y) override;
	PolyVox::MaterialDensityPair88 GetVoxel(FIntVector Location) override;
	
	const char* GetGeneratorName() override;
	/* End WorldGenerator Interface */

};
