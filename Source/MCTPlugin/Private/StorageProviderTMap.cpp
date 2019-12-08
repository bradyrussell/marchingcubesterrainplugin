// Fill out your copyright notice in the Description page of Project Settings.


#include "StorageProviderTMap.h"
#include "BufferArchive.h"
#include "Paths.h"
#include "PlatformFilemanager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "FileHelper.h"
#include "ArchiveSaveCompressedProxy.h"
#include "ArchiveLoadCompressedProxy.h"

StorageProviderTMap::StorageProviderTMap(bool bShouldSaveToFile):bShouldSaveToFile(bShouldSaveToFile) {

}

StorageProviderTMap::~StorageProviderTMap() {
}

bool StorageProviderTMap::Open(std::string Database, bool bCreateIfNotFound) {
	//return bCreateIfNotFound;
	DBName = Database;
	TArray<uint8> data;
	
	auto exists = FFileHelper::LoadFileToArray(data, UTF8_TO_TCHAR(GetDatabasePath(DBName).c_str()));

	if(!exists) return bCreateIfNotFound;

	FArchiveLoadCompressedProxy decompress(data, NAME_Zlib);
	decompress << DatabaseMap;

	return true;
}

bool StorageProviderTMap::Close() {
	if(bShouldSaveToFile) {
		TArray<uint8> data;
		FArchiveSaveCompressedProxy compress(data, NAME_Zlib, ECompressionFlags::COMPRESS_BiasMemory);

		compress << DatabaseMap;

		compress.Flush();
		compress.Close();

		FFileHelper::SaveArrayToFile(data, UTF8_TO_TCHAR(GetDatabasePath(DBName).c_str()));
	}
	
	DatabaseMap.Empty();
	return true;
}

bool StorageProviderTMap::Put(std::string Key, std::string Value) {
	TArray<uint8> buf;
	ArchiveFromString(Value, buf);
	DatabaseMap.Emplace(UTF8_TO_TCHAR(Key.c_str()), buf);
	return true;
}

bool StorageProviderTMap::Get(std::string Key, std::string& Value) {
	if(!DatabaseMap.Contains(UTF8_TO_TCHAR(Key.c_str()))) return false;

	TArray<uint8> buf = DatabaseMap.FindRef(UTF8_TO_TCHAR(Key.c_str()));
	Value = ArchiveToString(buf);
	
	return true;
}

const char* StorageProviderTMap::GetProviderName() {
	return "TMap";
}

std::string StorageProviderTMap::GetDatabasePath(std::string Name) {
	return TCHAR_TO_UTF8(*(FPaths::ProjectSavedDir()))  + Name + ".tmap";
}


std::string StorageProviderTMap::SerializeLocationToString(int32_t X, int32_t Y, int32_t Z, uint8 W) {
	char buf[64];
	sprintf_s(buf, "%d_%d_%d_%d", X,Y,Z,W);
	return std::string(buf);
}