// Fill out your copyright notice in the Description page of Project Settings.


#include "StorageProviderJson.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "VoxelNetThreads.h"


StorageProviderJson::StorageProviderJson(bool bShouldSaveToFile)
	: bShouldSaveToFile(bShouldSaveToFile) {

}

StorageProviderJson::~StorageProviderJson() {
}

bool StorageProviderJson::Open(std::string Database, bool bCreateIfNotFound) {
	//return bCreateIfNotFound;
	DBName = Database;
	TArray<uint8> data;

	FString JsonString;
	auto exists = FFileHelper::LoadFileToString(JsonString, UTF8_TO_TCHAR(GetDatabasePath(DBName).c_str()));

	DatabaseJson = MakeShareable(new FJsonObject);
	
	if (!exists)
		return bCreateIfNotFound;

	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	
	DBLock.Lock();
	if (!FJsonSerializer::Deserialize(Reader, DatabaseJson)) {
		DBLock.Unlock();
		return false;
	}
	DBLock.Unlock();
	return true;
}

bool StorageProviderJson::Close() {
	DBLock.Lock();
	if (bShouldSaveToFile) {
		FString OutputString;
		auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputString);

		FJsonSerializer::Serialize(DatabaseJson.ToSharedRef(), Writer);

		FFileHelper::SaveStringToFile(OutputString, UTF8_TO_TCHAR(GetDatabasePath(DBName).c_str()));
	}

	DatabaseJson.Get()->Values.Reset();
	DBLock.Unlock();
	return true;
}

bool StorageProviderJson::Keys(TArray<FString>& OutKeys) {
	DBLock.Lock();
	DatabaseJson.Get()->Values.GetKeys(OutKeys);
	DBLock.Unlock();
	return true;
}

bool StorageProviderJson::ForEach(TFunction<void(std::string Key, std::string Value)> CalledForEach) {
	check(false);
	return false;
}

bool StorageProviderJson::Put(std::string Key, std::string Value) {
	TArray<uint8> buf;
	ArchiveFromString(Value, buf);
	DBLock.Lock();
	DatabaseJson.Get()->SetStringField(UTF8_TO_TCHAR(Key.c_str()), FBase64::Encode(buf));
	DBLock.Unlock();
	return true;
}

bool StorageProviderJson::Get(std::string Key, std::string& Value) {
	DBLock.Lock();
	
	if (!DatabaseJson.Get()->Values.Contains(UTF8_TO_TCHAR(Key.c_str()))) {
		DBLock.Unlock();
		return false;
	}

	TArray<uint8> buf;
	FBase64::Decode(DatabaseJson.Get()->GetStringField(UTF8_TO_TCHAR(Key.c_str())), buf);
	DBLock.Unlock();
	Value = ArchiveToString(buf);

	return true;
}

const char* StorageProviderJson::GetProviderName() { return "Json"; }

std::string StorageProviderJson::GetDatabasePath(std::string Name) { return TCHAR_TO_UTF8(*(FPaths::ProjectSavedDir())) + Name + ".json"; }


std::string StorageProviderJson::SerializeLocationToString(int32_t X, int32_t Y, int32_t Z, uint8 W) {
	char buf[64];
	sprintf_s(buf, "%d_%d_%d_%d", X, Y, Z, W);
	return std::string(buf);
}

FIntVector4 StorageProviderJson::DeserializeLocationFromString(std::string Key) {
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

bool StorageProviderJson::IsRegionKey(std::string Key) {
	FString KeyStr = UTF8_TO_TCHAR(Key.c_str());

	TArray<FString> Parts;
	KeyStr.ParseIntoArray(Parts, TEXT("_"));

	return Parts.Num() == 4 && Parts[0].IsNumeric() && Parts[1].IsNumeric() && Parts[2].IsNumeric() && Parts[3].IsNumeric();
}
