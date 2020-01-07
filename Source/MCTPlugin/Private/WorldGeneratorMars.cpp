// Fill out your copyright notice in the Description page of Project Settings.
#include "WorldGeneratorMars.h"


int32 WorldGeneratorMars::GetHeightmap(int32 X, int32 Y) {
	return NoiseGenerators[0]->GetNoise2D(X,Y);
}

PolyVox::MaterialDensityPair88 WorldGeneratorMars::GetVoxel(FIntVector Location) {
	return PolyVox::MaterialDensityPair88(0,0);
}

const char* WorldGeneratorMars::GetGeneratorName() {
	return "Mars";
}
