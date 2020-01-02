// Fill out your copyright notice in the Description page of Project Settings.


#include "StorageProviderFlatfile.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"
#include "Serialization/ArchiveSaveCompressedProxy.h"
#include "Misc/FileHelper.h"
#include "Serialization/ArchiveLoadCompressedProxy.h"


StorageProviderFlatfile::StorageProviderFlatfile() {
	bRegionsUseSingleKey = true;
}

StorageProviderFlatfile::~StorageProviderFlatfile() {
}

bool StorageProviderFlatfile::Open(std::string Database, bool bCreateIfNotFound) {
	SetKeyPrefix(GetDatabasePath(Database)+"/"); // this is not the intended use of prefix/suffix but it works
	SetKeySuffix(".bin");
	if(FPaths::DirectoryExists(UTF8_TO_TCHAR(GetDatabasePath(Database).c_str()))) {
		return true;
	} else {
		if(!bCreateIfNotFound) return false;
		IPlatformFile& FileManager = FPlatformFileManager::Get().GetPlatformFile();
		return FileManager.CreateDirectory(UTF8_TO_TCHAR(GetDatabasePath(Database).c_str()));
	}
}

bool StorageProviderFlatfile::Close() {
	
	return true;
}

bool StorageProviderFlatfile::Put(std::string Key, std::string Value) {
	//UE_LOG(LogTemp, Warning, TEXT("Save: %hs | %hs"), MakeKey(Key).c_str(), Value.c_str());
	TArray<uint8> Data;
	ArchiveFromString(Value, Data);

	TArray<uint8> CompressedData;
	FArchiveSaveCompressedProxy Compressor(CompressedData, NAME_Zlib, ECompressionFlags::COMPRESS_BiasSpeed);

	Compressor << Data;
	Compressor.Flush();
	Compressor.Close();

	return FFileHelper::SaveArrayToFile(CompressedData, UTF8_TO_TCHAR(MakeKey(Key).c_str()));
}

bool StorageProviderFlatfile::Get(std::string Key, std::string& Value) {
	if(!FPaths::FileExists(UTF8_TO_TCHAR(MakeKey(Key).c_str()))) return false;
	
	TArray<uint8> Data;
	auto b = FFileHelper::LoadFileToArray(Data, UTF8_TO_TCHAR(MakeKey(Key).c_str()));

	if(Data.Num() == 0) return false;
	
	FArchiveLoadCompressedProxy Decompressor(Data, NAME_Zlib);
	TArray<uint8> UncompressedData;

	Decompressor << UncompressedData;
	Decompressor.Flush();
	Decompressor.Close();
	Value = ArchiveToString(UncompressedData);
	return b;
}

const char* StorageProviderFlatfile::GetProviderName() {
	return "Flatfile";
}

std::string StorageProviderFlatfile::GetDatabasePath(std::string Name) {
	return TCHAR_TO_UTF8(*(FPaths::ProjectSavedDir()+ FString("FF_")))  + Name;
}

std::string StorageProviderFlatfile::SerializeLocationToString(int32_t X, int32_t Y, int32_t Z, uint8 W) {
	char buf[64];
	sprintf_s(buf, "%d_%d_%d_%d", X,Y,Z,W);
	return std::string(buf);
}
