// Fill out your copyright notice in the Description page of Project Settings.


#include "ImportWorldCommandlet.h"
#include "StorageProviderBase.h"
#include "StorageProviderFlatfile.h"
#include "StorageProviderLevelDB.h"
#include "StorageProviderTMap.h"
#include "Config.h"
#include "Misc/Base64.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Containers/UnrealString.h"
#include "Serialization/MemoryReader.h"
#include "Structs.h"
#include "Dom/JsonValue.h"
#include "UObject/Class.h"

UImportWorldCommandlet::UImportWorldCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer) {
	IsClient = false;
	IsServer = false;
	IsEditor = false;
	LogToConsole = true;

	HelpDescription = "Imports the given world database as a JSON file";

	HelpParamNames.Add("world");
	HelpParamDescriptions.Add("The exported world JSON file to convert");

	HelpParamNames.Add("out");
	HelpParamDescriptions.Add("Where to put the result of the conversion. Uses same location as the world database if not specified");
}


int32 UImportWorldCommandlet::Main(const FString& Params) {
	const TCHAR* ParamStr = *Params;
	ParseCommandLine(ParamStr, CmdLineTokens, CmdLineSwitches, CmdLineParams);

	if (!CmdLineParams.Contains("world")) {
		UE_LOG(LogImportWorldCommandlet, Error, TEXT("Please specify the output world name by adding -world=<world name> to the command line."));
		return 1;
	}

	if (!CmdLineParams.Contains("format")) {
		UE_LOG(LogImportWorldCommandlet, Error, TEXT("Please specify the output world's format by adding -format=<world format> to the command line."));
		return 2;
	}

	if (!CmdLineParams.Contains("input")) {
		UE_LOG(LogImportWorldCommandlet, Error, TEXT("Please specify the input JSON file by adding -input=<path to JSON file> to the command line."));
		return 6;
	}


	FString InputJSON;

	if (!FFileHelper::LoadFileToString(InputJSON, **CmdLineParams.Find("input"))) {
		UE_LOG(LogImportWorldCommandlet, Error, TEXT("Unable to import the specified file. Please ensure that the provided path \n\"%s\"\n is valid."), **CmdLineParams.Find("input"));
		return 7;
	}

	FString WorldLocation = *CmdLineParams.Find("world");
	FString WorldFormat = CmdLineParams.Find("format")->ToLower();
	FString OutLocation; // todo implement
	bool bOverwriteExisting = false;
	bool bUseBase64 = true;
	bool bTerrainUseBase64 = true;
	bool bDumpRegions = true;
	bool bDumpTerrain = true;
	bool bDecodeRegionalData = true;

	if (CmdLineSwitches.Contains("overwrite"))
		bOverwriteExisting = true;
	
	if (CmdLineSwitches.Contains("base64"))
		bUseBase64 = true;
	if (CmdLineSwitches.Contains("hex"))
		bUseBase64 = false;

	if (CmdLineSwitches.Contains("noregions"))
		bDumpRegions = false;
	if (CmdLineSwitches.Contains("noterrain"))
		bDumpTerrain = false;

	if (CmdLineSwitches.Contains("base64terrain"))
		bTerrainUseBase64 = true;
	if (CmdLineSwitches.Contains("hexterrain"))
		bTerrainUseBase64 = false;

	if (CmdLineParams.Contains("out")) { OutLocation = *CmdLineParams.Find("out"); }
	else { OutLocation = WorldLocation; }

	if (WorldFormat == "flatfile" || WorldFormat == "ff") { StorageProvider = new StorageProviderFlatfile(); }
	else if (WorldFormat == "leveldb" || WorldFormat == "ldb") { StorageProvider = new StorageProviderLevelDB(true); }
	else if (WorldFormat == "tmap" || WorldFormat == "tmp") { StorageProvider = new StorageProviderTMap(true); }
	else {
		UE_LOG(LogImportWorldCommandlet, Error, TEXT("Unknown format [%s] specified. Please specify the world's format by adding -format=<world format> to the command line."), *WorldFormat);
		return 3;
	}

	UE_LOG(LogImportWorldCommandlet, Display, TEXT("Connecting to world database [%s]..."), *WorldLocation);

	if((FPaths::DirectoryExists(WorldLocation) || FPaths::FileExists(WorldLocation)) && !bOverwriteExisting) {
		UE_LOG(LogImportWorldCommandlet, Error, TEXT("The specified database location already exists. If you wish to try and import to an existing database please add -overwrite to the command line."),
		       StorageProvider->GetDatabasePath(TCHAR_TO_UTF8(*WorldLocation)).c_str());
		return 9;
	}
	
	bool bSuccess = StorageProvider->Open(TCHAR_TO_UTF8(*WorldLocation), true);

	UE_LOG(LogImportWorldCommandlet, Display, TEXT("Database connection to %hs using provider %hs: %s"), StorageProvider->GetDatabasePath(TCHAR_TO_UTF8(*WorldLocation)).c_str(),
	       StorageProvider->GetProviderName(), bSuccess ? TEXT("Success") : TEXT("Failure"));

	if (!bSuccess) {
		UE_LOG(LogImportWorldCommandlet, Error, TEXT("Failed to connect to database %hs using provider %hs. Please ensure that this location is able to be written to."),
		       StorageProvider->GetDatabasePath(TCHAR_TO_UTF8(*WorldLocation)).c_str(),
		       StorageProvider->GetProviderName());
		return 4;
	}

	StorageProvider->SetDatabaseFormat(DB_VERSION);

	UE_LOG(LogImportWorldCommandlet, Display, TEXT("Converting %s JSON to database..."), bUseBase64 ? TEXT("Base64"):TEXT("Hex"));

	const auto beforeConversion = FDateTime::UtcNow();

	////////////////////////////////////////////////////////////
	//iterate keys

	//TSharedPtr<FJsonObject> RootJsonObject = MakeShareable(new FJsonObject);

	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InputJSON);

	if (!FJsonSerializer::Deserialize(Reader, RootObject)) {
		UE_LOG(LogImportWorldCommandlet, Error, TEXT("Failed to parse the contents of the specified file. Please ensure that the file is valid JSON."));
		return 8;
	}

	//if(RootObject->HasTypedField<FJsonObject>()) // todo use

	auto RegionsObject = RootObject->GetObjectField("regions_terrain");
	auto RegionsDataObject = RootObject->GetObjectField("regions_data");
	auto PlayersDataObject = RootObject->GetObjectField("players");

	/*TSharedRef<FJsonObject> RegionsObject = MakeShareable(new FJsonObject);
	TSharedRef<FJsonObject> RegionsDataObject = MakeShareable(new FJsonObject);
	TSharedRef<FJsonObject> PlayersDataObject = MakeShareable(new FJsonObject);*/

	// -- //

	//RootObject->RemoveField() after dealing with each field

	// do terrain

	int32 NumberRegionsImported = 0;

	for (auto Value : RegionsObject->Values) {
		TArray<FString> Coords;
		Value.Key.ParseIntoArray(Coords, TEXT("_"));

		if (Coords.Num() != 3) {
			UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to parse the region key [%s] expected format [X_Y_Z]. Skipping."), *Value.Key);
			continue;
		}

		FString value;
		if (!Value.Value->TryGetString(value)) {
			UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to parse the region value associated with the key [%s] expected a string. Skipping."), *Value.Key);
			continue;
		}

		TArray<uint8> decodedBase64;
		if (!FBase64::Decode(value, decodedBase64)) {
			// todo allow hex
			UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to decode the region value associated with the key [%s] expected Base64. Skipping."), *Value.Key);
			continue;
		}

		if (!StorageProvider->PutRegionBinary(FIntVector(FCString::Atoi(*Coords[0]), FCString::Atoi(*Coords[1]), FCString::Atoi(*Coords[2])), decodedBase64)) {
			UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to save the region [%s] to the new database. Skipping."), *Value.Key);
			continue;
		}
		NumberRegionsImported++;
	}

	// do regional data

	int32 NumberRegionsDataImported = 0;

	for (auto Value : RegionsDataObject->Values) {
		TArray<FString> Coords;
		Value.Key.ParseIntoArray(Coords, TEXT("_"));

		if (Coords.Num() != 3) {
			UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to parse the region key [%s] expected format [X_Y_Z]. Skipping."), *Value.Key);
			continue;
		}


		const TSharedPtr<FJsonObject>* regionalDataObject;

		if (!Value.Value->TryGetObject(regionalDataObject)) {
			UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to parse the region data associated with the key [%s] expected an object. Skipping."), *Value.Key);
			continue;
		}

		const TArray<TSharedPtr<FJsonValue>>* RegionalDataActors;
		if (!regionalDataObject->Get()->TryGetArrayField("actors", RegionalDataActors)) {
			UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to parse the region's actor data associated with the key [%s] expected an array. Skipping."), *Value.Key);
			continue;
		}

		TArray<FVoxelWorldActorRecord> importedActorRecords;

		for (auto& ActorData : *RegionalDataActors) {
			const TSharedPtr<FJsonObject>* ActorDataObject;
			if (!ActorData->TryGetObject(ActorDataObject)) {
				UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to parse an actor in the region's actor data associated with the key [%s] expected an object. Skipping."), *Value.Key);
				continue;
			}

			bool success = true; // todo replace with if for each field for more accurate error logging
			FVoxelWorldActorRecord newRecord;

			success &= ActorDataObject->Get()->TryGetStringField("actor_class", newRecord.ActorClass);

			FString transformString;
			success &= ActorDataObject->Get()->TryGetStringField("actor_transform", transformString);
			success &= newRecord.ActorTransform.InitFromString(transformString);

			FString actorPIDString;
			success &= ActorDataObject->Get()->TryGetStringField("actor_pid", actorPIDString);
			newRecord.PersistentActorID = FCString::Atoi64(*actorPIDString);

			FString actorDataString;
			success &= ActorDataObject->Get()->TryGetStringField("actor_data", actorDataString);
			success &= FBase64::Decode(actorDataString, newRecord.ActorData);

			const TArray<TSharedPtr<FJsonValue>>* ActorComponentsArray;
			success &= ActorDataObject->Get()->TryGetArrayField("actor_components", ActorComponentsArray);

			for (auto& CompData : *ActorComponentsArray) {
				const TSharedPtr<FJsonObject>* CompDataObject;
				if (!CompData->TryGetObject(CompDataObject)) {
					UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to parse a component on an actor in region [%s] expected an object. Skipping."), *Value.Key);
					continue;
				}
				// parse component record like above
				FVoxelWorldComponentRecord compRecord;

				if (!CompDataObject->Get()->TryGetStringField("component_class", compRecord.ComponentClass)) {
					UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to parse the class of a component on an actor in region [%s] expected a string. Skipping."), *Value.Key);
					continue;
				}

				FString compTransformString;
				if (!CompDataObject->Get()->TryGetStringField("component_transform", compTransformString)) {
					UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to parse the transform of a component on an actor in region [%s] expected a string. Skipping."), *Value.Key);
					continue;
				}
				compRecord.ComponentTransform.InitFromString(compTransformString); // todo add check

				FString spawnIfNotFoundString;
				if (!CompDataObject->Get()->TryGetStringField("component_spawn_if_not_found", spawnIfNotFoundString)) {
					UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to parse the spawn if not found flag of a component on an actor in region [%s] expected a string. Skipping."),
					       *Value.Key);
					continue;
				}
				compRecord.bSpawnIfNotFound = spawnIfNotFoundString.ToBool();

				FString compDataString;
				if (!CompDataObject->Get()->TryGetStringField("component_data", compDataString)) {
					UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to parse the data field of a component on an actor in region [%s] expected a string. Skipping."), *Value.Key);
					continue;
				}
				FBase64::Decode(compDataString, compRecord.ComponentData); //todo check

				newRecord.ActorComponents.Add(compRecord);
			}
			importedActorRecords.Add(newRecord);
		}


		//TArray<uint8> actorRecordsBytes;

		FBufferArchive region_actors(true);
		region_actors << importedActorRecords;

		region_actors.Flush();

		if (!StorageProvider->PutRegionalData(FIntVector(FCString::Atoi(*Coords[0]), FCString::Atoi(*Coords[1]), FCString::Atoi(*Coords[2])), 0, region_actors)) {
			UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to save the region data for region [%s] to the new database. Skipping."), *Value.Key);
			continue;
		}
		region_actors.Close();
		NumberRegionsDataImported++;
	}

	// do players

	int32 NumberPlayersImported = 0;
	//TArray<FVoxelWorldActorRecord> importedPlayerRecords;

	for (auto Value : PlayersDataObject->Values) {
		const TSharedPtr<FJsonObject>* PlayerDataObject;

		bool success = true; // todo replace with if for each field for more accurate error logging

		success &= Value.Value->TryGetObject(PlayerDataObject);

		FVoxelWorldPlayerActorRecord newRecord;

		success &= PlayerDataObject->Get()->TryGetStringField("actor_class", newRecord.ActorClass);

		FString transformString;
		success &= PlayerDataObject->Get()->TryGetStringField("actor_transform", transformString);
		success &= newRecord.ActorTransform.InitFromString(transformString);

		FString playerSavedAtString;
		success &= PlayerDataObject->Get()->TryGetStringField("actor_saved_at", playerSavedAtString);
		success &= FDateTime::ParseIso8601(*playerSavedAtString, newRecord.SavedAt);

		FString actorDataString;
		success &= PlayerDataObject->Get()->TryGetStringField("actor_data", actorDataString);
		success &= FBase64::Decode(actorDataString, newRecord.ActorData);

		const TArray<TSharedPtr<FJsonValue>>* ActorComponentsArray;
		success &= PlayerDataObject->Get()->TryGetArrayField("components", ActorComponentsArray);

		for (auto& CompData : *ActorComponentsArray) {
			const TSharedPtr<FJsonObject>* CompDataObject;
			if (!CompData->TryGetObject(CompDataObject)) {
				UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to parse a component on an actor in region [%s] expected an object. Skipping."), *Value.Key);
				continue;
			}
			// parse component record like above
			FVoxelWorldComponentRecord compRecord;

			if (!CompDataObject->Get()->TryGetStringField("component_class", compRecord.ComponentClass)) {
				UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to parse the class of a component on an actor in region [%s] expected a string. Skipping."), *Value.Key);
				continue;
			}

			FString compTransformString;
			if (!CompDataObject->Get()->TryGetStringField("component_transform", compTransformString)) {
				UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to parse the transform of a component on an actor in region [%s] expected a string. Skipping."), *Value.Key);
				continue;
			}
			compRecord.ComponentTransform.InitFromString(compTransformString); // todo add check

			FString spawnIfNotFoundString;
			if (!CompDataObject->Get()->TryGetStringField("component_spawn_if_not_found", spawnIfNotFoundString)) {
				UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to parse the spawn if not found flag of a component on an actor in region [%s] expected a string. Skipping."), *Value.Key);
				continue;
			}
			compRecord.bSpawnIfNotFound = spawnIfNotFoundString.ToBool();

			FString compDataString;
			if (!CompDataObject->Get()->TryGetStringField("component_data", compDataString)) {
				UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to parse the data field of a component on an actor in region [%s] expected a string. Skipping."), *Value.Key);
				continue;
			}
			FBase64::Decode(compDataString, compRecord.ComponentData); //todo check

			newRecord.ActorComponents.Add(compRecord);
		}
		
		//importedPlayerRecords.Add(newRecord);


		//TArray<uint8> actorRecordsBytes;

		FBufferArchive playerSave(true);
		playerSave << newRecord;

		playerSave.Flush();

		if (!StorageProvider->PutGlobalData("PLAYER_" + std::string(TCHAR_TO_UTF8(*Value.Key)), playerSave)) {
			UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to save the player [%s] to the new database. Skipping."), *Value.Key);
			continue;
		}
		playerSave.Close();
		NumberPlayersImported++;
	}

	//do global data (remaining)

	RootObject->RemoveField("regions_terrain");
	RootObject->RemoveField("regions_data");
	RootObject->RemoveField("players");

	int32 NumberGlobalKeysImported = 0;
	for(auto& Value: RootObject->Values) {
		FString base64String;
		Value.Value->TryGetString(base64String);
		
		TArray<uint8> data;
		FBase64::Decode(base64String, data); //todo check

		FString KeyString = Value.Key;

		if(KeyString.Equals(DB_VERSION_TAG, ESearchCase::IgnoreCase)) continue; 
		
		if(KeyString.StartsWith(DB_GLOBAL_TAG)) {
			KeyString.ReplaceInline(TEXT(DB_GLOBAL_TAG), TEXT(""));
		}
		
		StorageProvider->PutGlobalData(std::string(TCHAR_TO_UTF8(*KeyString)), data);
		NumberGlobalKeysImported++;
	}

	///////////////////////////////////////////////////////////

	const auto afterConversion = FDateTime::UtcNow() - beforeConversion;

	UE_LOG(LogImportWorldCommandlet, Display, TEXT("Imported %d regions, data for %d regions, %d player saves and %d global key value pairs."), NumberRegionsImported, NumberRegionsDataImported, NumberPlayersImported, NumberGlobalKeysImported);
	
	UE_LOG(LogImportWorldCommandlet, Display, TEXT("World database converted in %f ms."), afterConversion.GetTotalMicroseconds()*0.001);
	
	StorageProvider->Close();
	delete StorageProvider;

	return 0;
}
