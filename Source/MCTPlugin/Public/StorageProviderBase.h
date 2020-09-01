// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include <string>
#include "CoreMinimal.h"
#include <PolyVox/MaterialDensityPair.h>
#include <PolyVox/PagedVolume.h>

/**	Small Map Test:
 *	Provider:	Save Time | Save Size | Files Created
 *			
	* LevelDB:	300ms	 | 12mb		| 18
	* Flatfile: 1280ms	 | 2mb		| 1,140
	* TMap:		825ms	 | 1.7mb	| 1
 *
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
	virtual bool Open(std::string Database, bool bCreateIfNotFound = true) = 0;
	virtual bool Close() = 0;
	
	virtual bool Put(std::string Key, std::string Value) = 0;
	virtual bool Get(std::string Key, std::string& Value) = 0;

	// return all keys in the database
	virtual bool Keys(TArray<FString>& OutKeys) = 0;

	// call a function for each key,value pair in the database
	// this is a read only operation 
	virtual bool ForEach(TFunction<void (std::string Key, std::string Value)> CalledForEach) = 0;
	
	virtual std::string GetDatabasePath(std::string Name) = 0;
	virtual const char* GetProviderName() = 0;
	/* End StorageProvider Interface */

	// Optionally allowed to override because some StorageProviders have character restrictions e.g. flatfile
	// if you override this you must also override Deserialize...() and IsRegionKey()
	virtual std::string SerializeLocationToString(int32_t X, int32_t Y, int32_t Z, uint8 W);
	virtual FIntVector4 DeserializeLocationFromString(std::string Key);
	// returns true if the key matches SerializeLocationToString
	virtual bool IsRegionKey(std::string Key);
	
	bool PutBytes(std::string Key, TArray<uint8>& Bytes);
	bool GetBytes(std::string Key, TArray<uint8>& Bytes);

	bool PutRegion(FIntVector Region, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk* RegionData);
	bool GetRegion(FIntVector Region, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk* RegionData);

	bool PutRegionBinary(FIntVector Region, TArray<uint8>& Bytes);
	bool GetRegionBinary(FIntVector Region, TArray<uint8>& Bytes);
	
	bool PutRegionalData(FIntVector Region, uint8 Index, TArray<uint8>& Bytes);
	bool GetRegionalData(FIntVector Region, uint8 Index, TArray<uint8>& Bytes);

	bool PutGlobalData(std::string Key, TArray<uint8>& Bytes);
	bool GetGlobalData(std::string Key, TArray<uint8>& Bytes);

	bool PutGlobalString(std::string Key, std::string String);
	bool GetGlobalString(std::string Key, std::string& String);
	
	int GetDatabaseFormat();
	bool SetDatabaseFormat(int Format);

	// Optionally allowed to override because some StorageProviders dont store a version , see StorageProviderNull
	virtual bool VerifyDatabaseFormat(int Format);

	static std::string ArchiveToString(TArray<uint8>& Archive);
	static void ArchiveFromString(std::string Input, TArray<uint8>& Archive);
	void SetKeyPrefix(std::string Prefix);
	void SetKeySuffix(std::string Suffix);

protected:
	std::string MakeKey(std::string Key) const;
	
};
