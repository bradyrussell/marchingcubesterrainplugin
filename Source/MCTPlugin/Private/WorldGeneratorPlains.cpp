// Fill out your copyright notice in the Description page of Project Settings.
#include "WorldGeneratorPlains.h"


int32 WorldGeneratorPlains::GetHeightmap(int32 X, int32 Y) {
	return NoiseGenerators[0]->GetNoise2D(X,Y);
}

PolyVox::MaterialDensityPair88 WorldGeneratorPlains::GetVoxel(FIntVector Location) {
	return PolyVox::MaterialDensityPair88(0,0);
}

const char* WorldGeneratorPlains::GetGeneratorName() {
	return "Plains";
}
