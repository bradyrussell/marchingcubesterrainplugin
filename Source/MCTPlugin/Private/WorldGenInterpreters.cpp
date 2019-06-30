#include "WorldGenInterpreters.h"
#include "PagedWorld.h"

PolyVox::MaterialDensityPair88 WorldGen::Interpret_Woods(int32 x, int32 y, int32 z, UUFNNoiseGenerator * material, UUFNNoiseGenerator * heightmap, UUFNNoiseGenerator * biome)
{
	PolyVox::MaterialDensityPair88 Voxel;
	auto _height = heightmap->GetNoise2D(x, y);
	auto _material = material->GetNoise3D(x, y, z);
	auto _biome = biome->GetNoise3D(x, y, z);

	if (z <= 0+(_biome)) {
		Voxel.setMaterial(1);
		Voxel.setDensity(Voxel.getMaxDensity());
	}
	else if (z > _height*2) {
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

PolyVox::MaterialDensityPair88 WorldGen::Interpret_AlienSpires(int32 x, int32 y, int32 z, UUFNNoiseGenerator * material, UUFNNoiseGenerator * heightmap, UUFNNoiseGenerator * biome)
{
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

PolyVox::MaterialDensityPair88 WorldGen::Interpret_Basic(int32 x, int32 y, int32 z, UUFNNoiseGenerator * material, UUFNNoiseGenerator * heightmap, UUFNNoiseGenerator * biome)
{
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

PolyVox::MaterialDensityPair88 WorldGen::Interpret_Mars(int32 x, int32 y, int32 z, TArray<UUFNNoiseGenerator*> noise)
{
	if (noise.Num() == 0) return PolyVox::MaterialDensityPair88();

	PolyVox::MaterialDensityPair88 Voxel;
	auto _height = noise[0]->GetNoise2D(x, y);
	//only evaluate noise when you will need it; its expensive

	if (z > _height) { // Above maximum height
		Voxel.setMaterial(MATERIAL_AIR);
		Voxel.setDensity(0);
		return Voxel;
	}
	else {
		//auto _material = noise[1]->GetNoise3D(x, y, z);
		//auto _temperature = noise[2]->GetNoise2D(x, y);
		//auto _moisture = noise[3]->GetNoise2D(x, y);
		auto _caves = noise[4]->GetNoise3D(x, y, z);

		auto _moisture = 1;

		if (z < (_height - 24) && _caves > .6) { // caves 24m+ deep
			Voxel.setMaterial(MATERIAL_AIR); // old cave
			Voxel.setDensity(0);
			return Voxel;
		}
		if (z < (_height - 24) && _caves > .5) { // cave walls?
			Voxel.setMaterial(MATERIAL_ORE); 
			Voxel.setDensity(140);
			return Voxel;
		}

		if (z + 4 > _height) { // grass layer
			Voxel.setMaterial(MATERIAL_DIRT);
			Voxel.setDensity(_moisture*Voxel.getMaxDensity());
		} else if(z + 280 > _height) { //stone layer
			Voxel.setMaterial(MATERIAL_STONE);
			Voxel.setDensity(_moisture*Voxel.getMaxDensity());
		} else if (z + 400 > _height) { // gold layer
			Voxel.setMaterial(MATERIAL_ORE);
			Voxel.setDensity(_moisture*Voxel.getMaxDensity());
		} else { // bedrock
			Voxel.setMaterial(MATERIAL_WOOD);
			Voxel.setDensity(_moisture*Voxel.getMaxDensity());
		}

	}

	return Voxel;
}
