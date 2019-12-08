// Fill out your copyright notice in the Description page of Project Settings.


#include "StorageProviderTMap.h"
#include "BufferArchive.h"
#include "Paths.h"
#include "PlatformFilemanager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "FileHelper.h"
#include "ArchiveSaveCompressedProxy.h"
#include "ArchiveLoadCompressedProxy.h"

StorageProviderTMap::StorageProviderTMap() {

}

StorageProviderTMap::~StorageProviderTMap() {
}

bool StorageProviderTMap::Open(std::string Database, bool bCreateIfNotFound) {
	return bCreateIfNotFound;
}

bool StorageProviderTMap::Close() {
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
	return Name;
}


std::string StorageProviderTMap::SerializeLocationToString(int32_t X, int32_t Y, int32_t Z, uint8 W) {
	char buf[64];
	sprintf_s(buf, "%d_%d_%d_%d", X,Y,Z,W);
	return std::string(buf);
}