// Fill out your copyright notice in the Description page of Project Settings.


#include "StorageProviderNull.h"

StorageProviderNull::StorageProviderNull() {

}

StorageProviderNull::~StorageProviderNull() {
}

bool StorageProviderNull::PutBytes(std::string Key, TArray<uint8>& Bytes) {
	return true;
}

bool StorageProviderNull::GetBytes(std::string Key, TArray<uint8>& Bytes) {
	return false;
}

bool StorageProviderNull::PutRegion(FIntVector Region, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk* RegionData) {
	return true;
}

bool StorageProviderNull::GetRegion(FIntVector Region, PolyVox::PagedVolume<PolyVox::MaterialDensityPair88>::Chunk* RegionData) {
	return false;
}

bool StorageProviderNull::PutRegionBinary(FIntVector Region, TArray<uint8>& Bytes) {
	return true;
	
}

bool StorageProviderNull::GetRegionBinary(FIntVector Region, TArray<uint8>& Bytes) {
	return false;
}

bool StorageProviderNull::PutRegionalData(FIntVector Region, uint8 Index, TArray<uint8>& Bytes) {
	return true;
}

bool StorageProviderNull::GetRegionalData(FIntVector Region, uint8 Index, TArray<uint8>& Bytes) {
	return false;
}

bool StorageProviderNull::PutGlobalData(std::string Key, TArray<uint8>& Bytes) {
	return true;
}

bool StorageProviderNull::GetGlobalData(std::string Key, TArray<uint8>& Bytes) {
	return false;
}

bool StorageProviderNull::PutGlobalString(std::string Key, std::string String) {
	return true;
}

bool StorageProviderNull::GetGlobalString(std::string Key, std::string& String) {
	return false;
}

int StorageProviderNull::GetDatabaseFormat() {
	return 1;
}

bool StorageProviderNull::SetDatabaseFormat(int Format) {
	return true;
}

bool StorageProviderNull::Open(std::string Database, bool bCreateIfNotFound) {
	return bCreateIfNotFound;
}

bool StorageProviderNull::Close() {	
	return true;
}

bool StorageProviderNull::Keys(TArray<FString>& OutKeys) {
	return true;
}

bool StorageProviderNull::ForEach(TFunction<void(std::string Key, std::string Value)> CalledForEach) {
	return true;
}


bool StorageProviderNull::Put(std::string Key, std::string Value) {
	return true;
}

bool StorageProviderNull::Get(std::string Key, std::string& Value) {
	return false;
}

const char* StorageProviderNull::GetProviderName() {
	return "Null";
}

std::string StorageProviderNull::GetDatabasePath(std::string Name) {
	return Name;
}

bool StorageProviderNull::VerifyDatabaseFormat(int Format) {
	return true;
}

