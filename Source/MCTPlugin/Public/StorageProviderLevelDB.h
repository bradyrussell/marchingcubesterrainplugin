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
	StorageProviderLevelDB();
	virtual ~StorageProviderLevelDB();

	leveldb::DB* db;
	
	/* StorageProvider Interface */
	virtual bool Open(std::string Database, bool bCreateIfNotFound, StorageOptimization Optimization) override;
	virtual bool Close() override;
	
	virtual bool Put(std::string Key, std::string Value)  override;
	virtual bool Get(std::string Key, std::string& Value) override;
	/* End StorageProvider Interface */

};
