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
