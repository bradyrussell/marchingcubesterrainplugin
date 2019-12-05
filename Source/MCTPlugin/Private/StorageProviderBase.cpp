// Fill out your copyright notice in the Description page of Project Settings.


#include "StorageProviderBase.h"
#include "BufferArchive.h"
#include "Config.h"

StorageProviderBase::StorageProviderBase()
{
}

StorageProviderBase::~StorageProviderBase()
{
}

bool StorageProviderBase::PutBytes(std::string Key, TArray<uint8>& Bytes) {
	return Put(Key, ArchiveToString(Bytes));
}

bool StorageProviderBase::GetBytes(std::string Key, TArray<uint8>& Bytes) {
	std::string value;
	const auto bExists =  Get(Key, value);
	if(!bExists) return false;
	ArchiveFromString(value, Bytes);
	return true;
}

int StorageProviderBase::GetDatabaseFormat() {
	std::string value;
	const auto bExists = Get(DB_VERSION_TAG, value);
	return bExists ? std::stoi(value) : -1;
}

bool StorageProviderBase::SetDatabaseFormat(int Format) {
	char buf[16];
	itoa(Format, buf, 10);
	return Put(DB_VERSION_TAG, buf);
}

std::string StorageProviderBase::ArchiveToString(TArray<uint8>& Archive) {
	const auto out = std::string((char*)Archive.GetData(), Archive.Num());
	return out;
}

void StorageProviderBase::ArchiveFromString(std::string Input, TArray<uint8>& Archive) {
	const int len = Input.length();
	if (len <= 0)
		return;

	if (Archive.Num() > 0)
		Archive.Empty(len);
	Archive.AddZeroed(len);

	for (int i = 0; i < len; i++) { Archive[i] = (unsigned char)Input[i]; }
}

std::string StorageProviderBase::SerializeLocationToString(int32_t X, int32_t Y, int32_t Z, uint8 W) {
	FBufferArchive tempBuffer(true);
	tempBuffer << X;
	tempBuffer << Y;
	tempBuffer << Z;
	tempBuffer << W;

	return ArchiveToString(tempBuffer);
}