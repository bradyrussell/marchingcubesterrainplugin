// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include <string>
#include "CoreMinimal.h"
#include <PolyVox/MaterialDensityPair.h>
#include <PolyVox/PagedVolume.h>

/*enum StorageOptimization {
	None,
	Speed,
	Memory,
	Disk
};*/ // todo remove in favor of Subclass specific ctors

/**
 * 
 */
class MCTPLUGIN_API StorageProviderBase
{
public:
	StorageProviderBase();
	virtual ~StorageProviderBase();

	std::string KeyPrefix, KeySuffix;
	bool bRegionsUseSingleKey = false;
	
	/* StorageProvider Interface */
	virtual bool Open(std::string Database, bool bCreateIfNotFound = true/*, StorageOptimization Optimization = StorageOptimization::None*/) = 0;
	virtual bool Close() = 0;
	
	virtual bool Put(std::string Key, std::string Value) = 0;
	virtual bool Get(std::string Key, std::string& Value) = 0;

	virtual std::string GetDatabasePath(std::string Name) = 0;
	virtual const char* GetProviderName() = 0;
	/* End StorageProvider Interface */

	// Optionally allowed to override because some StorageProviders have character restrictions e.g. flatfile
	virtual std::string SerializeLocationToString(int32_t X, int32_t Y, int32_t Z, uint8 W);
	
	bool PutBytes(std::string Key, TArray<uint8>& Bytes);
	bool GetBytes(std::string Key, TArray<uint8>& Bytes);

	bool PutRegion(FIntVector Region, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk* RegionData);
	bool GetRegion(FIntVector Region, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk* RegionData);

	bool PutRegionalData(FIntVector Region, uint8 Index, TArray<uint8>& Bytes);
	bool GetRegionalData(FIntVector Region, uint8 Index, TArray<uint8>& Bytes);

	bool PutGlobalData(std::string Key, TArray<uint8>& Bytes);
	bool GetGlobalData(std::string Key, TArray<uint8>& Bytes);
	
	int GetDatabaseFormat();
	bool SetDatabaseFormat(int Format);

	static std::string ArchiveToString(TArray<uint8>& Archive);
	static void ArchiveFromString(std::string Input, TArray<uint8>& Archive);
	void SetKeyPrefix(std::string Prefix);
	void SetKeySuffix(std::string Suffix);

protected:
	std::string MakeKey(std::string Key) const;
	
};
