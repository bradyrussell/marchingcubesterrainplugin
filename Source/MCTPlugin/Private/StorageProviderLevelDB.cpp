// Fill out your copyright notice in the Description page of Project Settings.


#include "StorageProviderLevelDB.h"
#include "BufferArchive.h"
#include "Config.h"
#include "Paths.h"
#include "leveldb/filter_policy.h"
#include "leveldb/cache.h"

StorageProviderLevelDB::StorageProviderLevelDB() {
}

StorageProviderLevelDB::~StorageProviderLevelDB() {
}

bool StorageProviderLevelDB::Open(std::string Database, bool bCreateIfNotFound, StorageOptimization Optimization) {
	leveldb::Options options;
	options.create_if_missing = bCreateIfNotFound;

	//todo remove debug
	Optimization = StorageOptimization::Speed;

	switch (Optimization) {
		case None: { break; }
		case Speed: {
			//options.write_buffer_size = 120 * 1048576; // todo make configurable in BP?
			options.block_cache = leveldb::NewLRUCache(8 * 1048576);
			options.filter_policy = leveldb::NewBloomFilterPolicy(10);
			break;
		}
		case Memory: { break; }
		case Disk: { break; }
		default: { break; }
	}

	FString dbname = FPaths::ProjectSavedDir() + "World_" + FString(UTF8_TO_TCHAR(Database.c_str()));
	leveldb::Status status = leveldb::DB::Open(options, std::string(TCHAR_TO_UTF8(*dbname)), &db);

	UE_LOG(LogTemp, Warning, TEXT("Database connection to %s: %s"), *dbname, status.ok() ? TEXT("Success") : TEXT("Failure"));
	ensure(status.ok());
	return status.ok();
}

bool StorageProviderLevelDB::Close() {
	delete db;
	return true;
}

bool StorageProviderLevelDB::Put(std::string Key, std::string Value) {
	const auto status = db->Put(leveldb::WriteOptions(), Key ,Value);
	return status.ok();
}

bool StorageProviderLevelDB::Get(std::string Key, std::string& Value) {
	const auto status = db->Get(leveldb::ReadOptions(), Key, &Value);
	return status.ok();
}
