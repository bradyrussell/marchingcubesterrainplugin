// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include <string>
#include "CoreMinimal.h"
#include "StorageProviderBase.h"
#include "leveldb/db.h"

/**
 * 
 */
class MCTPLUGIN_API StorageProviderTMap: public StorageProviderBase
{
public:
	StorageProviderTMap(bool bShouldSaveToFile);
	virtual ~StorageProviderTMap();

	TMap<FString,TArray<uint8>> DatabaseMap;
	std::string DBName;

	bool bShouldSaveToFile = false;
	
	/* StorageProvider Interface */
	virtual bool Open(std::string Database, bool bCreateIfNotFound) override;
	virtual bool Close() override;


	bool Keys(TArray<FString>& OutKeys) override;
	bool ForEach(TFunction<void(std::string Key, std::string Value)> CalledForEach) override;
	
	virtual bool Put(std::string Key, std::string Value)  override;
	virtual bool Get(std::string Key, std::string& Value) override;

	virtual const char* GetProviderName() override;
	virtual std::string GetDatabasePath(std::string Name) override;
	std::string SerializeLocationToString(int32_t X, int32_t Y, int32_t Z, uint8 W) override;

	
	/* End StorageProvider Interface */

	FIntVector4 DeserializeLocationFromString(std::string Key) override;
	bool IsRegionKey(std::string Key) override;
};
