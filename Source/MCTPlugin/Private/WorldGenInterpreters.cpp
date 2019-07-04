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

PolyVox::MaterialDensityPair88 Interpret_Biome_Mountains(int32 z, float _height, float _caves, float _ore) {
	PolyVox::MaterialDensityPair88 Voxel;


	if (z < (_height + 4) && (_caves > 1)) {
		return PolyVox::MaterialDensityPair88(MATERIAL_AIR, 0);
	}
	//if (z < (_height - 64) && _caves > .5) {
	//	return PolyVox::MaterialDensityPair88(MATERIAL_ORE, 140);
	//}

	if (z + 8 > _height) {
		return PolyVox::MaterialDensityPair88(MATERIAL_DIRT, 255);
	}

	if(_ore > .7) {
		float randomness = (_ore - .7)*100; // blends veins near borders

		if(_height - 1024+randomness > z) return PolyVox::MaterialDensityPair88(MATERIAL_URANIUM, 200+randomness);
		if(_height - 256+randomness > z) return PolyVox::MaterialDensityPair88(MATERIAL_GOLD, 200+randomness);
		if(_height - 128+randomness > z) return PolyVox::MaterialDensityPair88(MATERIAL_IRON, 200+randomness);
		if(_height - 1+randomness > z) return PolyVox::MaterialDensityPair88(MATERIAL_COAL, 200+randomness);
		
	}

	if (z + 560 > _height) {
		return PolyVox::MaterialDensityPair88(MATERIAL_STONE, 255);
	}

	if (z + 880 > _height) {
		return PolyVox::MaterialDensityPair88(MATERIAL_STONE, 255);
	}

	return PolyVox::MaterialDensityPair88(MATERIAL_STONE, 255);
	
}

PolyVox::MaterialDensityPair88 WorldGen::Interpret_Mars(int32 x, int32 y, int32 z, TArray<UUFNNoiseGenerator*> noise) {
	if (noise.Num() == 0) return PolyVox::MaterialDensityPair88();

	auto _height = noise[0]->GetNoise2D(x, y);
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

			case MOUNTAINS: return Interpret_Biome_Mountains(z, _height, _caves, _ore);
			default: return PolyVox::MaterialDensityPair88(0,0);
		}
	}

}

EBiome WorldGen::Interpret_Biome(float _height, float temperatue, float moisture) {
	//if(_height > 64) return MOUNTAINS;
	//if(_height > 0) return PLAINS;
	return MOUNTAINS;
}