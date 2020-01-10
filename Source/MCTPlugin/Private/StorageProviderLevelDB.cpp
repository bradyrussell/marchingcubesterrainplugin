// Fill out your copyright notice in the Description page of Project Settings.


#include "StorageProviderLevelDB.h"
#include "BufferArchive.h"
#include "Paths.h"
#include "leveldb/filter_policy.h"
#include "leveldb/cache.h"

StorageProviderLevelDB::StorageProviderLevelDB(bool bUseOptimizations):bUseOptimizations(bUseOptimizations) {
}

StorageProviderLevelDB::~StorageProviderLevelDB() {
}

bool StorageProviderLevelDB::Open(std::string Database, bool bCreateIfNotFound) {
	leveldb::Options options;
	options.create_if_missing = bCreateIfNotFound;
	
	if(bUseOptimizations) {
			//options.write_buffer_size = 120 * 1048576; // todo make configurable in BP?
			options.block_cache = leveldb::NewLRUCache(8 * 1048576);
			options.filter_policy = leveldb::NewBloomFilterPolicy(10);
		}

	leveldb::Status status = leveldb::DB::Open(options, GetDatabasePath(Database), &db);
	
	return status.ok();
}

bool StorageProviderLevelDB::Close() {
	if(db)  delete db;
	return true;
}

bool StorageProviderLevelDB::Put(std::string Key, std::string Value) {
	const auto status = db->Put(leveldb::WriteOptions(), MakeKey(Key),Value);
	return status.ok();
}

bool StorageProviderLevelDB::Get(std::string Key, std::string& Value) {
	const auto status = db->Get(leveldb::ReadOptions(), MakeKey(Key), &Value);
	return status.ok();
}

const char* StorageProviderLevelDB::GetProviderName() {
	return "LevelDB";
}

std::string StorageProviderLevelDB::GetDatabasePath(std::string Name) {
	return TCHAR_TO_UTF8(*(FPaths::ProjectSavedDir()+ FString("LDB_")))  + Name;
}
