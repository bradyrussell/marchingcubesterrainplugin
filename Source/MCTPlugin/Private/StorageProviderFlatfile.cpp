// Fill out your copyright notice in the Description page of Project Settings.


#include "StorageProviderFlatfile.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"
#include "Serialization/ArchiveSaveCompressedProxy.h"
#include "Misc/FileHelper.h"
#include "Serialization/ArchiveLoadCompressedProxy.h"
#include "VoxelNetThreads.h"


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

bool StorageProviderFlatfile::Keys(TArray<FString>& OutKeys) {
	TArray<FString> tempOut;
	
	if(FPaths::DirectoryExists(UTF8_TO_TCHAR(KeyPrefix.c_str()))) {
		IFileManager::Get().FindFiles(tempOut,UTF8_TO_TCHAR(KeyPrefix.c_str()),UTF8_TO_TCHAR(KeySuffix.c_str()));

		for(auto&elem:tempOut) {
			OutKeys.Emplace(elem.LeftChop(KeySuffix.length())); // remove file extension
		}
		
		return true;
	}

	return false;
}

bool StorageProviderFlatfile::ForEach(TFunction<void(std::string Key, std::string Value)> CalledForEach) {
	if(!FPaths::DirectoryExists(UTF8_TO_TCHAR(KeyPrefix.c_str()))) return false;
	
	TArray<FString> Filenames;
	Keys(Filenames);
	
	for(auto&elem:Filenames) {
		std::string Data;
		auto b = this->Get(TCHAR_TO_UTF8(*elem), Data);

		if(b) {
			CalledForEach(TCHAR_TO_UTF8(*elem), Data);
		} else {
			UE_LOG(LogVoxelDatabase, Warning, TEXT("Failed to load the value for key [%s] while during iteration, skipping."), *elem);
		}
	}

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

FIntVector4 StorageProviderFlatfile::DeserializeLocationFromString(std::string Key) {
	FString KeyStr = UTF8_TO_TCHAR(Key.c_str());

	TArray<FString> Parts;
	KeyStr.ParseIntoArray(Parts, TEXT("_"));

	if(Parts.Num() != 4) {
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

bool StorageProviderFlatfile::IsRegionKey(std::string Key) {
	FString KeyStr = UTF8_TO_TCHAR(Key.c_str());

	TArray<FString> Parts;
	KeyStr.ParseIntoArray(Parts, TEXT("_"));

	return Parts.Num() == 4 && Parts[0].IsNumeric() && Parts[1].IsNumeric() && Parts[2].IsNumeric() && Parts[3].IsNumeric();
}

