// Fill out your copyright notice in the Description page of Project Settings.
#include "StorageProviderBase.h"

#include "Config.h"
#include "Serialization/BufferArchive.h"

StorageProviderBase::StorageProviderBase()
	: KeyPrefix(""), KeySuffix("") {
}

StorageProviderBase::~StorageProviderBase() {
}

bool StorageProviderBase::PutBytes(std::string Key, TArray<uint8>& Bytes) { return Put(Key, ArchiveToString(Bytes)); }

bool StorageProviderBase::GetBytes(std::string Key, TArray<uint8>& Bytes) {
	std::string value;
	const auto bExists = Get(Key, value);
	if (!bExists)
		return false;
	ArchiveFromString(value, Bytes);
	return true;
}

bool StorageProviderBase::PutRegion(FIntVector Region, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk* RegionData) {
	// todo make batched write
	//bool success = true;
	if (bRegionsUseSingleKey) {
		char byteBuf[2 * REGION_SIZE * REGION_SIZE * REGION_SIZE]; // 65kb for 32^3, material and density 1 byte each
		int n = 0;
		//since we are saving big keys this order should optimize compression

		for (char x = 0; x < REGION_SIZE; x++) {
			for (char y = 0; y < REGION_SIZE; y++) {
				for (char w = 0; w < REGION_SIZE; w++) {
					auto uVoxel = RegionData->getVoxel(x, y, w);

					char mat = uVoxel.getMaterial(); // unsigned -> signed conversion
					char den = uVoxel.getDensity();

					byteBuf[n] = mat;
					byteBuf[n + (REGION_SIZE * REGION_SIZE * REGION_SIZE)] = den;
					n++;
				}
			}
		}

		return Put(SerializeLocationToString(Region.X, Region.Y, Region.Z, 0), std::string(byteBuf, 2 * REGION_SIZE * REGION_SIZE * REGION_SIZE));;
	}
	else {
		for (char w = 0; w < REGION_SIZE; w++) {
			// for each x,y layer
			char byteBuf[2 * REGION_SIZE * REGION_SIZE]; // 2kb for 32^2, material and density 1 byte each

			int n = 0; // this could be better if we were to calculate it from xy so it is independent of order?
			// x + (y*REGION_SIZE)
			//but compression may be better in order like this

			for (char x = 0; x < REGION_SIZE; x++) {
				for (char y = 0; y < REGION_SIZE; y++) {
					auto uVoxel = RegionData->getVoxel(x, y, w);

					char mat = uVoxel.getMaterial(); // unsigned -> signed conversion
					char den = uVoxel.getDensity();

					byteBuf[n++] = mat;
					byteBuf[n++] = den;
				}
			}

			auto b = Put(SerializeLocationToString(Region.X, Region.Y, Region.Z, w), std::string(byteBuf, 2 * REGION_SIZE * REGION_SIZE));
			if (!b)
				return false;
		}
		return true;
	}
}

bool StorageProviderBase::GetRegion(FIntVector Region, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk* RegionData) {
	bool containsNonZero = false;

	if (bRegionsUseSingleKey) {
		//for (char w = 0; w < REGION_SIZE; w++) {
			std::string chunkData;

			auto status = Get(SerializeLocationToString(Region.X, Region.Y, Region.Z, 0), chunkData);

			if (!status) {
				return false;
			}

			int n = 0;

		for (char x = 0; x < REGION_SIZE; x++) {
			for (char y = 0; y < REGION_SIZE; y++) {
				for (char w = 0; w < REGION_SIZE; w++) {
					unsigned char mat = chunkData[n]; // signed - > unsigned conversion
					unsigned char den = chunkData[n + (REGION_SIZE * REGION_SIZE * REGION_SIZE)];
					n++;

					if (mat != 0)
						containsNonZero = true;
					RegionData->setVoxel(x, y, w, PolyVox::MaterialDensityPair88(mat, den));
				}
			}
		}
#ifdef REGEN_NULL_REGIONS
	return containsNonZero;
#else REGEN_NULL_REGIONS
		return true;
#endif
	}
	else {
		for (char w = 0; w < REGION_SIZE; w++) {
			std::string chunkData;

			auto status = Get(SerializeLocationToString(Region.X, Region.Y, Region.Z, w), chunkData);
			//auto status = db->Get(leveldb::ReadOptions(), StorageProviderBase::SerializeLocationToString(pos.X, pos.Y, pos.Z, w), &chunkData);

			if (!status) {
				if (w > 0)
				UE_LOG(LogTemp, Warning, TEXT("Loading failed partway through %s region : failed at layer %d. Region data unrecoverable."), *Region.ToString(), w);
				return false;
			}

			int n = 0; // this could be better if we were to calculate it from xy so it is independent of order

			for (char x = 0; x < REGION_SIZE; x++) {
				for (char y = 0; y < REGION_SIZE; y++) {
					unsigned char mat = chunkData[n++]; // signed - > unsigned conversion
					unsigned char den = chunkData[n++];

					if (mat != 0)
						containsNonZero = true;
					RegionData->setVoxel(x, y, w, PolyVox::MaterialDensityPair88(mat, den));
				}
			}
		}
#ifdef REGEN_NULL_REGIONS
	return containsNonZero;
#else REGEN_NULL_REGIONS
		return true;
#endif
	}
}


bool StorageProviderBase::PutRegionalData(FIntVector Region, uint8 Index, TArray<uint8>& Bytes) {
	return PutBytes(SerializeLocationToString(Region.X, Region.Y, Region.Z, Index + REGION_SIZE), Bytes);
}

bool StorageProviderBase::GetRegionalData(FIntVector Region, uint8 Index, TArray<uint8>& Bytes) {
	return GetBytes(SerializeLocationToString(Region.X, Region.Y, Region.Z, Index + REGION_SIZE), Bytes);
}

bool StorageProviderBase::PutGlobalData(std::string Key, TArray<uint8>& Bytes) { return PutBytes(DB_GLOBAL_TAG + Key, Bytes); }

bool StorageProviderBase::GetGlobalData(std::string Key, TArray<uint8>& Bytes) { return GetBytes(DB_GLOBAL_TAG + Key, Bytes); }

int StorageProviderBase::GetDatabaseFormat() {
	std::string value;
	const auto bExists = Get(DB_VERSION_TAG, value);
	return bExists && value != "" ? std::stoi(value) : -1;
}

bool StorageProviderBase::SetDatabaseFormat(int Format) {
	char buf[16];
	_itoa(Format, buf, 10);
	return Put(DB_VERSION_TAG, buf);
}

bool StorageProviderBase::VerifyDatabaseFormat(int Format) {
		auto db_version = GetDatabaseFormat();
		if (db_version == -1) { 
			SetDatabaseFormat(DB_VERSION);
			db_version = GetDatabaseFormat();// this is intentional, basically a sort of check we can read/write from the db. override if you dont want, see StorageProviderNull 
		}
	return  db_version == Format;
}

std::string StorageProviderBase::ArchiveToString(TArray<uint8>& Archive) {
	const auto out = std::string((char*)Archive.GetData(), Archive.Num());
	return out;
}

void StorageProviderBase::ArchiveFromString(std::string Input, TArray<uint8>& Archive) {
	const int len = Input.length();
	if (len <= 0)
		return;

	if (Archive.Num() > 0)
		Archive.Empty(len);
	Archive.AddZeroed(len);

	for (int i = 0; i < len; i++) { Archive[i] = (unsigned char)Input[i]; }
}

void StorageProviderBase::SetKeyPrefix(std::string Prefix) { KeyPrefix = Prefix; }

void StorageProviderBase::SetKeySuffix(std::string Suffix) { KeySuffix = Suffix; }

std::string StorageProviderBase::MakeKey(std::string Key) const { return KeyPrefix + Key + KeySuffix; }

std::string StorageProviderBase::SerializeLocationToString(int32_t X, int32_t Y, int32_t Z, uint8 W) {
	FBufferArchive tempBuffer(true);
	tempBuffer << X;
	tempBuffer << Y;
	tempBuffer << Z;
	tempBuffer << W;

	return ArchiveToString(tempBuffer);
}
