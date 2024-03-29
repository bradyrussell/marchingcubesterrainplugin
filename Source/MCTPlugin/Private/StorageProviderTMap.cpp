// Fill out your copyright notice in the Description page of Project Settings.


#include "StorageProviderTMap.h"

#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/FileHelper.h"
#include "Serialization/ArchiveLoadCompressedProxy.h"
#include "Serialization/ArchiveSaveCompressedProxy.h"
#include "Misc/Paths.h"
#include "VoxelNetThreads.h"


StorageProviderTMap::StorageProviderTMap(bool bShouldSaveToFile)
	: bShouldSaveToFile(bShouldSaveToFile) {

}

StorageProviderTMap::~StorageProviderTMap() {
}

bool StorageProviderTMap::Open(std::string Database, bool bCreateIfNotFound) {
	//return bCreateIfNotFound;
	DBName = Database;
	TArray<uint8> data;

	auto exists = FFileHelper::LoadFileToArray(data, UTF8_TO_TCHAR(GetDatabasePath(DBName).c_str()));

	if (!exists)
		return bCreateIfNotFound;

	FArchiveLoadCompressedProxy decompress(data, NAME_Zlib);
	DBLock.Lock();
	decompress << DatabaseMap;
	DBLock.Unlock();

	return true;
}

bool StorageProviderTMap::Close() {
	DBLock.Lock();
	if (bShouldSaveToFile) {
		TArray<uint8> data;
		FArchiveSaveCompressedProxy compress(data, NAME_Zlib, ECompressionFlags::COMPRESS_BiasMemory);

		compress << DatabaseMap;

		compress.Flush();
		compress.Close();

		FFileHelper::SaveArrayToFile(data, UTF8_TO_TCHAR(GetDatabasePath(DBName).c_str()));
	}

	DatabaseMap.Empty();
	DBLock.Unlock();
	return true;
}

bool StorageProviderTMap::Keys(TArray<FString>& OutKeys) {
	DBLock.Lock();
	DatabaseMap.GetKeys(OutKeys);
	DBLock.Unlock();
	return true;
}

bool StorageProviderTMap::ForEach(TFunction<void(std::string Key, std::string Value)> CalledForEach) {
	DBLock.Lock();
	for (TTuple<FString, TArray<uint8>> elem : DatabaseMap) { CalledForEach(TCHAR_TO_UTF8(*elem.Key), ArchiveToString(elem.Value)); }
	DBLock.Unlock();
	return true;
}

bool StorageProviderTMap::Put(std::string Key, std::string Value) {
	TArray<uint8> buf;
	ArchiveFromString(Value, buf);
	DBLock.Lock();
	DatabaseMap.Emplace(UTF8_TO_TCHAR(Key.c_str()), buf);
	DBLock.Unlock();
	return true;
}

bool StorageProviderTMap::Get(std::string Key, std::string& Value) {
	DBLock.Lock();
	if (!DatabaseMap.Contains(UTF8_TO_TCHAR(Key.c_str()))) {
		DBLock.Unlock();
		return false;
	}

	TArray<uint8> buf = DatabaseMap.FindRef(UTF8_TO_TCHAR(Key.c_str()));
	DBLock.Unlock();
	Value = ArchiveToString(buf);

	return true;
}

const char* StorageProviderTMap::GetProviderName() { return "TMap"; }

std::string StorageProviderTMap::GetDatabasePath(std::string Name) { return TCHAR_TO_UTF8(*(FPaths::ProjectSavedDir())) + Name + ".tmap"; }


std::string StorageProviderTMap::SerializeLocationToString(int32_t X, int32_t Y, int32_t Z, uint8 W) {
	char buf[64];
	sprintf_s(buf, "%d_%d_%d_%d", X, Y, Z, W);
	return std::string(buf);
}

FIntVector4 StorageProviderTMap::DeserializeLocationFromString(std::string Key) {
	FString KeyStr = UTF8_TO_TCHAR(Key.c_str());

	TArray<FString> Parts;
	KeyStr.ParseIntoArray(Parts, TEXT("_"));

	if (Parts.Num() != 4) {
		UE_LOG(LogVoxelDatabase, Warning, TEXT("Failed to deserialize the key [%s] into a location!"))
		return FIntVector4();
	}

	FIntVector4 out;

	out.X = FCString::Atoi(*Parts[0]);
	out.Y = FCString::Atoi(*Parts[1]);
	out.Z = FCString::Atoi(*Parts[2]);
	out.W = FCString::Atoi(*Parts[3]);

	return out;
}

bool StorageProviderTMap::IsRegionKey(std::string Key) {
	FString KeyStr = UTF8_TO_TCHAR(Key.c_str());

	TArray<FString> Parts;
	KeyStr.ParseIntoArray(Parts, TEXT("_"));

	return Parts.Num() == 4 && Parts[0].IsNumeric() && Parts[1].IsNumeric() && Parts[2].IsNumeric() && Parts[3].IsNumeric();
}
