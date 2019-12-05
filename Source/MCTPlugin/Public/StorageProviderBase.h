// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include <string>
#include "CoreMinimal.h"

/**
 * 
 */
class MCTPLUGIN_API StorageProviderBase
{
public:
	StorageProviderBase();
	~StorageProviderBase();

	/* StorageProvider Interface */
	virtual bool Open(std::string Database) = 0;
	virtual bool Close() = 0;
	
	virtual bool Put(std::string Key, std::string Value) = 0;
	virtual bool Get(std::string Key, std::string& Value) = 0;
	/* End StorageProvider Interface */

	bool PutBytes(std::string Key, TArray<uint8>& Bytes);
	bool GetBytes(std::string Key, TArray<uint8>& Bytes);
	
	int GetDatabaseFormat();
	bool SetDatabaseFormat(int Format);

	static std::string ArchiveToString(TArray<uint8>& Archive);
	static void ArchiveFromString(std::string Input, TArray<uint8>& Archive);
	static std::string SerializeLocationToString(int32_t X, int32_t Y, int32_t Z, uint8 W);
};
