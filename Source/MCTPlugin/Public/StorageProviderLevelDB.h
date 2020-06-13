// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include <string>
#include "CoreMinimal.h"
#include "StorageProviderBase.h"
#include "leveldb/db.h"

/**
 * 
 */
class MCTPLUGIN_API StorageProviderLevelDB: public StorageProviderBase
{
public:
	StorageProviderLevelDB(bool bUseOptimizations);
	virtual ~StorageProviderLevelDB();

	leveldb::DB* db;
	bool bUseOptimizations = true;
	
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

};
