// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include <string>
#include "CoreMinimal.h"
#include "UFNNoiseGenerator.h"
#include <PolyVox/MaterialDensityPair.h>


class MCTPLUGIN_API WorldGeneratorBase
{
public:
	WorldGeneratorBase();
	virtual ~WorldGeneratorBase();

	/* WorldGenerator Interface */
	virtual int32 GetHeightmap(int32 X, int32 Y) = 0;
	virtual PolyVox::MaterialDensityPair88 GetVoxel(int32 X, int32 Y, int32 Z) = 0;
	
	virtual const char* GetGeneratorName() = 0;
	/* End WorldGenerator Interface */
};
