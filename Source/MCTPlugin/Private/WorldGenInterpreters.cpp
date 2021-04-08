#include "WorldGenInterpreters.h"
#include "PagedWorld.h"

PolyVox::MaterialDensityPair88 WorldGen::Interpret_Woods(int32 x, int32 y, int32 z, UUFNNoiseGenerator* material, UUFNNoiseGenerator* heightmap, UUFNNoiseGenerator* biome) {
	PolyVox::MaterialDensityPair88 Voxel;
	auto _height = heightmap->GetNoise2D(x, y);
	auto _material = material->GetNoise3D(x, y, z);
	auto _biome = biome->GetNoise3D(x, y, z);

	if (z <= 0 + (_biome)) {
		Voxel.setMaterial(1);
		Voxel.setDensity(Voxel.getMaxDensity());
	}
	else if (z > _height * 2) {
		Voxel.setMaterial(0);
		Voxel.setDensity(0);
	}
	else {
		uint16 r = _material;
		uint16 g = .8 * Voxel.getMaxDensity();

		Voxel.setMaterial(2);
		Voxel.setDensity(g);
	}
	return Voxel;
}

PolyVox::MaterialDensityPair88 WorldGen::Interpret_AlienSpires(int32 x, int32 y, int32 z, UUFNNoiseGenerator* material, UUFNNoiseGenerator* heightmap, UUFNNoiseGenerator* biome) {
	PolyVox::MaterialDensityPair88 Voxel;
	auto _height = heightmap->GetNoise2D(x, y);
	auto _material = material->GetNoise3D(x, y, z);
	auto _biome = (biome->GetNoise3D(x, y, z) + 1);

	if (z <= 1) {
		Voxel.setMaterial(1);
		Voxel.setDensity(Voxel.getMaxDensity());
	}
	else if (z > _height) {
		Voxel.setMaterial(0);
		Voxel.setDensity(0);
	}
	else if (z + 5 > _height) {
		uint16 r = _material;
		uint16 g = _biome * Voxel.getMaxDensity();
		Voxel.setMaterial((r % 2)); // only grass or air 5 above heightmap
		Voxel.setDensity((r % 2 == 1) ? g : 0);
	}
	else {
		uint16 r = _material;
		uint16 g = _biome * Voxel.getMaxDensity();

		Voxel.setMaterial((r % 3) + 1);
		Voxel.setDensity(g);
	}
	return Voxel;
}

PolyVox::MaterialDensityPair88 WorldGen::Interpret_Basic(int32 x, int32 y, int32 z, UUFNNoiseGenerator* material, UUFNNoiseGenerator* heightmap, UUFNNoiseGenerator* biome) {
	PolyVox::MaterialDensityPair88 Voxel;
	auto _height = heightmap->GetNoise2D(x, y);
	auto _material = material->GetNoise3D(x, y, z);
	auto _biome = biome->GetNoise3D(x, y, z);

	if (z <= 0) {
		Voxel.setMaterial(2);
		Voxel.setDensity(Voxel.getMaxDensity());
	}
	else if (z > _height) {
		Voxel.setMaterial(0);
		Voxel.setDensity(0);
	}
	else {
		uint16 r = _material;
		uint16 g = .8 * Voxel.getMaxDensity();

		Voxel.setMaterial(r);
		Voxel.setDensity(g);
	}
	return Voxel;
}

PolyVox::MaterialDensityPair88 Interpret_Biome_Mountains(int32 z, float _height, float _caves, float _ore, int32 density) {
	PolyVox::MaterialDensityPair88 Voxel;
	
	if (z < (_height + 4) && (_caves > 1)) {
		return PolyVox::MaterialDensityPair88(MATERIAL_AIR, 0);
	}
	//if (z < (_height - 64) && _caves > .5) {
	//	return PolyVox::MaterialDensityPair88(MATERIAL_ORE, 140);
	//}

	if (z + 8 > _height) {
		return PolyVox::MaterialDensityPair88(MATERIAL_DIRT, density);
	}

	if(_ore > .7) {
		float randomness = (_ore - .7)*100; // blends veins near borders

		if(_height - 1024+randomness > z) return PolyVox::MaterialDensityPair88(MATERIAL_URANIUM, 200+randomness);
		if(_height - 256+randomness > z) return PolyVox::MaterialDensityPair88(MATERIAL_GOLD, 200+randomness);
		if(_height - 128+randomness > z) return PolyVox::MaterialDensityPair88(MATERIAL_IRON, 200+randomness);
		if(_height - -8+randomness > z) return PolyVox::MaterialDensityPair88(MATERIAL_COAL, 200+randomness);
		
	}

	if (z + 560 > _height) {
		return PolyVox::MaterialDensityPair88(MATERIAL_STONE, 255);
	}

	if (z + 880 > _height) {
		return PolyVox::MaterialDensityPair88(MATERIAL_STONE, 255);
	}

	return PolyVox::MaterialDensityPair88(MATERIAL_STONE, 255);
	
}

PolyVox::MaterialDensityPair88 WorldGen::Interpret_Mars(int32 x, int32 y, int32 z,const TArray<UUFNNoiseGenerator*>& noise) {
	if (noise.Num() == 0) {
		UE_LOG(LogTemp, Warning, TEXT("Aborting region generation, cannot access noise."))
		return PolyVox::MaterialDensityPair88();
	}

	float totalHeight = noise[0]->GetNoise2D(x, y);
	int32 _height = totalHeight;
	int32 _density = FMath::FloorToInt(FMath::Abs(totalHeight - _height) * 128.f)+128; // converts the fractional part of the height to a range 128-255
	//only evaluate noise when you will need it; its expensive. aka return as soon as possible

	if (z > _height) return PolyVox::MaterialDensityPair88(MATERIAL_AIR, 0);

	else {
		//auto _material = noise[1]->GetNoise3D(x, y, z);

		auto _temperature = noise[2]->GetNoise2D(x, y); // assumed to be -1 thru 1
		//auto _moisture = noise[3]->GetNoise2D(x, y); // assumed to be -1 thru 1

		auto _biome = Interpret_Biome(_height,_temperature,1);

		auto _caves = noise[4]->GetNoise3D(x, y, z);
		auto _ore = noise[5]->GetNoise3D(x, y, z);

		switch (_biome) { 

			case MOUNTAINS: return Interpret_Biome_Mountains(z, _height, _caves, _ore, _density);
			default: return PolyVox::MaterialDensityPair88(0,0);
		}
	}

}


/*
 *	Index	|	Function
 *	0		|	Island Height 2d
 *	1		|	Island Depth 2d
 *	2		|	Material 3d
 *	3		|	Caves 3d
 */
PolyVox::MaterialDensityPair88 WorldGen::Interpret_Island2(int32 x, int32 y, int32 z,const TArray<UUFNNoiseGenerator*>& noise) {
	if (noise.Num() < 2) {
		UE_LOG(LogTemp, Warning, TEXT("Aborting region generation, cannot access noise."))
		return PolyVox::MaterialDensityPair88();
	}
	const float totalHeight = noise[0]->GetNoise2D(x, y);
	//const float totalHeight = FMath::Max(-1.f,noise[0]->GetNoise2D(x, y));
	const int32 Height = totalHeight;

	if(z > Height) return PolyVox::MaterialDensityPair88(MATERIAL_AIR, 0);

	const auto Depth = /*FMath::Max(0.f,noise[1]->GetNoise2D(x,y))*/0.f;
	if(z < -Depth) return PolyVox::MaterialDensityPair88(MATERIAL_AIR, 0);

	const auto Caves = noise[3]->GetNoise3D(x,y,z);
	if(Caves > 0.1f) return PolyVox::MaterialDensityPair88(MATERIAL_AIR, 0);
	
	// todo maybe depth density?  
	const int32 Density = FMath::FloorToInt(FMath::Abs(totalHeight - Height) * 128.f)+128; // converts the fractional part of the height to a range 128-255

	const auto Material = FMath::Clamp(FMath::RoundToInt(noise[2]->GetNoise3D(x,y,z)),1,1);

	//todo this will make wall interior voxels less dense
	//if(Caves > 0.f && Caves <= 0.1f) return PolyVox::MaterialDensityPair88(Material, FMath::FloorToInt((1.f-(Caves * 10.f)) * 128.f)+128); // should make smooth density gradient on cave walls?
	
	if(z == Height || z == -Depth) return PolyVox::MaterialDensityPair88(MATERIAL_DIRT, Density);
	return PolyVox::MaterialDensityPair88(Material, 255);
}

/*
 *	Index	|	Function
 *	0		|	Voxel Density
 *	1		|	Voxel Material
 *	2		|	Island Presence
 */

// island presence noise forms a gradient of where islands are allowed, and there is a hard border at IslandPresenceRequirement 
// could make this an in game setting
// .4 and .3 are good settings at least with the current Island Presence noise generator
const float IslandPresenceRequirement = .27f; // -1 to 1, higher values mean less frequent islands. this also changes the formation of islands and too low a value makes them too 'noisy'
const float X_Scale = 2.f, Y_Scale = 2.f, Z_Scale = 1.f; // scale the noise by dividing the input coordinates
PolyVox::MaterialDensityPair88 WorldGen::Interpret_Island(int32 x, int32 y, int32 z,const TArray<UUFNNoiseGenerator*>& noise) {
	if (noise.Num() < 2) {
		UE_LOG(LogTemp, Warning, TEXT("Aborting region generation, cannot access noise."))
		return PolyVox::MaterialDensityPair88();
	}

	auto x_scaled = x / X_Scale;
	auto y_scaled = y / Y_Scale;
	auto z_scaled = z / Z_Scale;

	if(noise[2]->GetNoise3D(x_scaled,y_scaled,z_scaled) < IslandPresenceRequirement) return PolyVox::MaterialDensityPair88(MATERIAL_AIR, 0);

	int32 density = ((noise[0]->GetNoise3D(x_scaled,y_scaled,z_scaled) + 1) * 128) - 1;
	int32 material = FMath::RoundToInt(noise[1]->GetNoise3D(x_scaled,y_scaled,z_scaled));

	/*if (z > _height)*/ return PolyVox::MaterialDensityPair88(material, density);

	/*else {
		//auto _material = noise[1]->GetNoise3D(x, y, z);

		auto _temperature = noise[2]->GetNoise2D(x, y); // assumed to be -1 thru 1
		//auto _moisture = noise[3]->GetNoise2D(x, y); // assumed to be -1 thru 1

		auto _biome = Interpret_Biome(_height,_temperature,1);

		auto _caves = noise[4]->GetNoise3D(x, y, z);
		auto _ore = noise[5]->GetNoise3D(x, y, z);

		switch (_biome) { 

			case MOUNTAINS: return Interpret_Biome_Mountains(z, _height, _caves, _ore, _density);
			default: return PolyVox::MaterialDensityPair88(0,0);
		}
	}*/

}

EBiome WorldGen::Interpret_Biome(float _height, float temperatue, float moisture) {
	//if(_height > 64) return MOUNTAINS;
	//if(_height > 0) return PLAINS;
	return MOUNTAINS;
}

enum {
	Block_Air,
	Block_Grass,
	Block_Dirt,
	Block_Stone,
	Block_Iron,
	Block_Gold,
	Block_Gunpowder,
};

PolyVox::MaterialDensityPair88 WorldGen::Interpret_New(int32 x, int32 y, int32 z,const TArray<UUFNNoiseGenerator*>& noise) {
	float totalHeight = noise[0]->GetNoise2D(x, y);
	int32 _height = totalHeight;
	int32 _density = FMath::FloorToInt(FMath::Abs(totalHeight - _height) * 128.f)+128; // converts the fractional part of the height to a range 128-255
	int32 randomness = noise[0]->GetNoise3D(x,y,_height);

	//float ore = noise[1]->GetNoise3D(x,y,z);
	//
	// float cave = noise[4]->GetNoise3D(x,y,z);
	// if(cave > 1)  return PolyVox::MaterialDensityPair88(Block_Air,0);
	
	if(z < _height - (10+randomness)) {
		//if(ore >= .1 && ore <= .12) return PolyVox::MaterialDensityPair88(Block_Iron, 255);
		//if(ore >= .12 && ore <= .15) return PolyVox::MaterialDensityPair88(Block_Gold, 255);
		//if(ore >= .15 && ore <= .2) return PolyVox::MaterialDensityPair88(Block_Gunpowder, 255);
		/*else*/ return PolyVox::MaterialDensityPair88(Block_Stone, 255);
	}
	if(z < _height - (2+randomness)) return PolyVox::MaterialDensityPair88(Block_Dirt, 255);
	if(z < _height - randomness/4) return PolyVox::MaterialDensityPair88(Block_Grass, _density);
	return PolyVox::MaterialDensityPair88(Block_Air,0);

}
