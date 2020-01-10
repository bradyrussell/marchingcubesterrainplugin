// Fill out your copyright notice in the Description page of Project Settings.
#include "WorldGeneratorPlains.h"


int32 WorldGeneratorPlains::GetHeightmap(int32 X, int32 Y) {
	return NoiseGenerators[0]->GetNoise2D(X,Y);
}

PolyVox::MaterialDensityPair88 WorldGeneratorPlains::GetVoxel(int32 X, int32 Y, int32 Z) {
	int32 _height = GetHeightmap(X, Y);
	int32 randomness = NoiseGenerators[0]->GetNoise3D(X, Y,_height);
	//float ore = NoiseGenerators[1]->GetNoise3D(x,y,z);
	//
	// float cave = NoiseGenerators[4]->GetNoise3D(x,y,z);
	// if(cave > 1)  return PolyVox::MaterialDensityPair88(Block_Air,0);
	
	if(Z < _height - (10+randomness)) {
		//if(ore >= .1 && ore <= .12) return PolyVox::MaterialDensityPair88(Block_Iron, 255);
		//if(ore >= .12 && ore <= .15) return PolyVox::MaterialDensityPair88(Block_Gold, 255);
		//if(ore >= .15 && ore <= .2) return PolyVox::MaterialDensityPair88(Block_Gunpowder, 255);
		/*else*/ return PolyVox::MaterialDensityPair88(Block_Stone, 255);
	}
	if(Z < _height - (2+randomness)) return PolyVox::MaterialDensityPair88(Block_Dirt, 255);
	if(Z < _height - randomness/4) return PolyVox::MaterialDensityPair88(Block_Grass, 255);
	return PolyVox::MaterialDensityPair88(Block_Air,0);
}


const char* WorldGeneratorPlains::GetGeneratorName() {
	return "Plains";
}
