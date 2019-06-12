#include "WorldGenInterpreters.h"

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
