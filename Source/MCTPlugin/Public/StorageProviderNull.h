// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include <string>
#include "CoreMinimal.h"
#include "StorageProviderBase.h"

/**
 * 
 */
class MCTPLUGIN_API StorageProviderNull: public StorageProviderBase
{
public:
	StorageProviderNull();
	virtual ~StorageProviderNull();

	// override all of these to return immediately
	bool PutBytes(std::string Key, TArray<uint8>& Bytes) override;
	bool GetBytes(std::string Key, TArray<uint8>& Bytes) override;
	bool PutRegion(FIntVector Region, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk* RegionData) override;
	bool GetRegion(FIntVector Region, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk* RegionData) override;
	bool PutRegionBinary(FIntVector Region, TArray<uint8>& Bytes) override;
	bool GetRegionBinary(FIntVector Region, TArray<uint8>& Bytes) override;
	bool PutRegionalData(FIntVector Region, uint8 Index, TArray<uint8>& Bytes) override;
	bool GetRegionalData(FIntVector Region, uint8 Index, TArray<uint8>& Bytes) override;
	bool PutGlobalData(std::string Key, TArray<uint8>& Bytes) override;
	bool GetGlobalData(std::string Key, TArray<uint8>& Bytes) override;
	bool PutGlobalString(std::string Key, std::string String) override;
	bool GetGlobalString(std::string Key, std::string& String) override;
	int GetDatabaseFormat() override;
	bool SetDatabaseFormat(int Format) override;
	
	/* StorageProvider Interface */
	virtual bool Open(std::string Database, bool bCreateIfNotFound) override;
	virtual bool Close() override;

	bool Keys(TArray<FString>& OutKeys) override;
	bool ForEach(TFunction<void(std::string Key, std::string Value)> CalledForEach) override;
	
	virtual bool Put(std::string Key, std::string Value)  override;
	virtual bool Get(std::string Key, std::string& Value) override;

	virtual const char* GetProviderName() override;
	virtual std::string GetDatabasePath(std::string Name) override;
	virtual bool VerifyDatabaseFormat(int Format) override;
	/* End StorageProvider Interface */

};
