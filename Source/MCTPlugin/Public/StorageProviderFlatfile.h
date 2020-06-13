// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include <string>
#include "CoreMinimal.h"
#include "StorageProviderBase.h"

/**
 * 
 */
class MCTPLUGIN_API StorageProviderFlatfile: public StorageProviderBase
{
public:
	//WARNING: This is only for demonstration purposes. This will write a LOT (easily 100k+ for large view distances) of small files to your disk and is very slow.
	StorageProviderFlatfile();
	virtual ~StorageProviderFlatfile();
	
	/* StorageProvider Interface */
	virtual bool Open(std::string Database, bool bCreateIfNotFound) override;
	virtual bool Close() override;

	virtual bool Keys(TArray<FString>& OutKeys) override;

	bool ForEach(TFunction<void(std::string Key, std::string Value)> CalledForEach) override;
	
	virtual bool Put(std::string Key, std::string Value)  override;
	virtual bool Get(std::string Key, std::string& Value) override;

	virtual const char* GetProviderName() override;
	virtual std::string GetDatabasePath(std::string Name) override;
	/* End StorageProvider Interface */

	virtual std::string SerializeLocationToString(int32_t X, int32_t Y, int32_t Z, uint8 W) override;
	FIntVector4 DeserializeLocationToString(std::string Key) override;
	bool IsRegionKey(std::string Key) override;
};
