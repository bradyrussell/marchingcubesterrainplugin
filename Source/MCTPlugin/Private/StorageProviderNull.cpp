// Fill out your copyright notice in the Description page of Project Settings.


#include "StorageProviderNull.h"

StorageProviderNull::StorageProviderNull() {

}

StorageProviderNull::~StorageProviderNull() {
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

