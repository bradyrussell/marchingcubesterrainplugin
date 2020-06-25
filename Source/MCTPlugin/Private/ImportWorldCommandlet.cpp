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
	bool bUseBase64 = true;
	bool bTerrainUseBase64 = true;
	bool bDumpRegions = true;
	bool bDumpTerrain = true;
	bool bDecodeRegionalData = true;

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

	bool bSuccess = StorageProvider->Open(TCHAR_TO_UTF8(*WorldLocation), true);

	UE_LOG(LogImportWorldCommandlet, Display, TEXT("Database connection to %hs using provider %hs: %s"), StorageProvider->GetDatabasePath(TCHAR_TO_UTF8(*WorldLocation)).c_str(),
	       StorageProvider->GetProviderName(), bSuccess ? TEXT("Success") : TEXT("Failure"));

	if (!bSuccess) {
		UE_LOG(LogImportWorldCommandlet, Error, TEXT("Failed to connect to database %hs using provider %hs. Please ensure that this location is able to be written to."),
		       StorageProvider->GetDatabasePath(TCHAR_TO_UTF8(*WorldLocation)).c_str(),
		       StorageProvider->GetProviderName());
		return 4;
	}


	UE_LOG(LogImportWorldCommandlet, Display, TEXT("Converting %s JSON to database..."), bUseBase64 ? TEXT("Base64"):TEXT("Hex"));

	const auto beforeConversion = FDateTime::UtcNow();

	////////////////////////////////////////////////////////////
	//iterate keys

	//TSharedPtr<FJsonObject> RootJsonObject = MakeShareable(new FJsonObject);

	TSharedRef<FJsonObject> RootObject = MakeShareable(new FJsonObject);

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

		if(Coords.Num() != 3) {
			UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to parse the region key [%s] expected format [X_Y_Z]. Skipping."), *Value.Key);
			continue;
		}

		FString value;
		if(!Value.Value->TryGetString(value)) {
			UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to parse the region value associated with the key [%s] expected a string. Skipping."), *Value.Key);
			continue;
		}

		TArray<uint8> decodedBase64;
		if(!FBase64::Decode(value, decodedBase64)) { // todo allow hex
			UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to decode the region value associated with the key [%s] expected Base64. Skipping."), *Value.Key);
			continue;
		}

		if(!StorageProvider->PutRegionBinary(FIntVector(FCString::Atoi(*Coords[0]),FCString::Atoi(*Coords[1]),FCString::Atoi(*Coords[2])), decodedBase64)) {
			UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to save the region [%s] to the new database. Skipping."), *Value.Key);
			continue;
		}
		NumberRegionsImported++;
	}

	// do regional data

	int32 NumberRegionsDataImported = 0;
	
	for (auto Value : RegionsObject->Values) {
		TArray<FString> Coords;
		Value.Key.ParseIntoArray(Coords, TEXT("_"));

		if(Coords.Num() != 3) {
			UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to parse the region key [%s] expected format [X_Y_Z]. Skipping."), *Value.Key);
			continue;
		}


		const TSharedPtr<FJsonObject>* regionalDataObject;
		
		if(!Value.Value->TryGetObject(regionalDataObject)) {
			UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to parse the region data associated with the key [%s] expected an object. Skipping."), *Value.Key);
			continue;
		}

		const TArray<TSharedPtr<FJsonValue>>* RegionalDataActors;
		if(!regionalDataObject->Get()->TryGetArrayField("actors", RegionalDataActors)) {
			UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to parse the region's actor data associated with the key [%s] expected an array. Skipping."), *Value.Key);
			continue;
		}

		for(auto& ActorData:*RegionalDataActors) {
			const TSharedPtr<FJsonObject>* ActorDataObject;
			if(!ActorData->TryGetObject(ActorDataObject)) {
				UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to parse an actor in the region's actor data associated with the key [%s] expected an object. Skipping."), *Value.Key);
				continue;
			}

			/*
			 *
			 *{
					"actor_class": "/Game/BPs/Entity/Nodes/BP_StoneNode.BP_StoneNode_C",
					"actor_transform": "1200.000000,1700.000000,0.000000|-9.429745,-107.329475,-9.429790|0.954106,0.954106,0.954106",
					"actor_PID": "0",
					"actor_data": "DgAAAGJDYW5CZURhbWFnZWQADQAAAEJvb2xQcm9wZXJ0eQAAAAAAAAAAAAEABQAAAE5vbmUAAAAAAA==",
					"actor_components": []
				}
			 * 
			 */

			bool success = true;
			FVoxelWorldActorRecord newRecord;

			success &= ActorDataObject->Get()->TryGetStringField("actor_class", newRecord.ActorClass);

			FString transformString;
			success &= ActorDataObject->Get()->TryGetStringField("actor_transform", transformString);
			newRecord.ActorTransform.InitFromString(transformString);

			FString actorPIDString;
			success &= ActorDataObject->Get()->TryGetStringField("actor_pid", actorPIDString);
			newRecord.PersistentActorID = FCString::Atoi64(*actorPIDString);

			FString actorDataString;
			success &= ActorDataObject->Get()->TryGetStringField("actor_data", actorDataString);
			success &= FBase64::Decode(actorDataString, newRecord.ActorData);
		}

		TArray<uint8> decodedBase64;
		if(!FBase64::Decode(value, decodedBase64)) { // todo allow hex
			UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to decode the region value associated with the key [%s] expected Base64. Skipping."), *Value.Key);
			continue;
		}

		if(!StorageProvider->PutRegionBinary(FIntVector(FCString::Atoi(*Coords[0]),FCString::Atoi(*Coords[1]),FCString::Atoi(*Coords[2])), decodedBase64)) {
			UE_LOG(LogImportWorldCommandlet, Warning, TEXT("Failed to save the region [%s] to the new database. Skipping."), *Value.Key);
			continue;
		}
		NumberRegionsImported++;
	}
	
	// do players

	//do global data (remaining)
	
	/*StorageProvider->ForEach(
		[this, &RootObject, &RegionsObject, &RegionsDataObject, &PlayersDataObject, bUseBase64, bTerrainUseBase64, bDumpRegions, bDumpTerrain, bDecodeRegionalData](std::string Key, std::string Value) {
			/*if (bUseBase64) { EncodedData = FBase64::Encode((uint8*)Value.data(), Value.length()); }
				   else { EncodedData = BytesToHex((uint8*)Value.data(), Value.length()); }#1#

			if (StorageProvider->IsRegionKey(Key)) { // regional terrain and data
				if (!bDumpRegions)
					return;
				auto Decoded = StorageProvider->DeserializeLocationFromString(Key);

				if (Decoded.W == 0 && bDumpTerrain) {
					// this is a unique occurrence per region
					TArray<uint8> RegionBinary;
					StorageProvider->GetRegionBinary(FIntVector(Decoded.X, Decoded.Y, Decoded.Z), RegionBinary);

					FString EncodedRegionData = bTerrainUseBase64 ? FBase64::Encode(RegionBinary.GetData(), RegionBinary.Num()) : BytesToHex(RegionBinary.GetData(), RegionBinary.Num());

					TArray<FStringFormatArg> Args;
					Args.Emplace(Decoded.X);
					Args.Emplace(Decoded.Y);
					Args.Emplace(Decoded.Z);

					RegionsObject.Get().SetStringField(FString::Format(TEXT("{0}_{1}_{2}"), Args), *EncodedRegionData);
				}
				else {
					if (Decoded.W >= REGION_SIZE) {
						if (bDecodeRegionalData) {
							TSharedRef<FJsonObject> CurrentRegionObject = MakeShareable(new FJsonObject);

							TArray<uint8> region_actors;
							StorageProvider->ArchiveFromString(Value, region_actors);


							/////////////////////////////////////////////////////////////
							TArray<TSharedPtr<FJsonValue>> RegionActorObjects;

							TArray<FVoxelWorldActorRecord> regionActorRecords;
							FMemoryReader reader(region_actors, true);
							reader << regionActorRecords;

							for (auto& record : regionActorRecords) {
								TSharedRef<FJsonObject> CurrentActorObject = MakeShareable(new FJsonObject);

								CurrentActorObject->SetStringField("actor_class", record.ActorClass);
								CurrentActorObject->SetStringField("actor_transform", record.ActorTransform.ToString());
								CurrentActorObject->SetStringField("actor_PID", FString::FromInt(record.PersistentActorID));

								FString recordActorData = bUseBase64
									                          ? FBase64::Encode(record.ActorData.GetData(), record.ActorData.Num())
									                          : BytesToHex(record.ActorData.GetData(), record.ActorData.Num());
								CurrentActorObject->SetStringField("actor_data", recordActorData);


								TArray<TSharedPtr<FJsonValue>> ActorComponentObjects;

								for (auto& compRecord : record.ActorComponents) {
									TSharedRef<FJsonObject> CurrentComponentObject = MakeShareable(new FJsonObject);
									CurrentComponentObject->SetStringField("component_class", compRecord.ComponentClass);
									CurrentComponentObject->SetStringField("component_transform", compRecord.ComponentTransform.ToString());
									CurrentComponentObject->SetStringField("component_spawn_if_not_found", FString::FromInt(compRecord.bSpawnIfNotFound));

									FString recordComponentData = bUseBase64
										                              ? FBase64::Encode(compRecord.ComponentData.GetData(), compRecord.ComponentData.Num())
										                              : BytesToHex(compRecord.ComponentData.GetData(), compRecord.ComponentData.Num());
									CurrentComponentObject->SetStringField("component_data", recordComponentData);

									ActorComponentObjects.Add(MakeShareable(new FJsonValueObject(CurrentComponentObject))); 
								}

								CurrentActorObject.Get().SetArrayField("actor_components", ActorComponentObjects);

								RegionActorObjects.Add(MakeShareable(new FJsonValueObject(CurrentActorObject)));
							}
							CurrentRegionObject.Get().SetArrayField("actors", RegionActorObjects);

							TArray<FStringFormatArg> Args;
							Args.Emplace(Decoded.X);
							Args.Emplace(Decoded.Y);
							Args.Emplace(Decoded.Z);

							RegionsDataObject.Get().SetObjectField(FString::Format(TEXT("{0}_{1}_{2}"), Args), CurrentRegionObject);

							/////////////////////////////////////////////////////////////
						}
						else {
							TArray<FStringFormatArg> Args;
							Args.Emplace(Decoded.X);
							Args.Emplace(Decoded.Y);
							Args.Emplace(Decoded.Z);
							Args.Emplace(Decoded.W);

							FString EncodedData = bUseBase64 ? FBase64::Encode((uint8*)Value.data(), Value.length()) : BytesToHex((uint8*)Value.data(), Value.length()); // todo
							RegionsDataObject.Get().SetStringField(FString::Format(TEXT("{0}_{1}_{2}_{3}"), Args), *EncodedData);
						}
					}
				}
			}
			else { // global data
				FString KeyFstr = UTF8_TO_TCHAR(Key.c_str());

				if(KeyFstr.StartsWith("MapGlobalData_PLAYER_")) {// todo replace with tag system?
					
					/////////////////////////////////////////////////////////////////////////////////////////////////
					TArray<uint8> playerSave;

					if (StorageProvider->GetGlobalData(Key.substr(14), playerSave)) {

						TSharedRef<FJsonObject> CurrentPlayerObject = MakeShareable(new FJsonObject);
					
						FVoxelWorldPlayerActorRecord record;
						FMemoryReader reader(playerSave, true);
						reader << record;

						CurrentPlayerObject->SetStringField("actor_class", record.ActorClass);
						CurrentPlayerObject->SetStringField("actor_transform", record.ActorTransform.ToString());
						CurrentPlayerObject->SetStringField("actor_saved_at", record.SavedAt.ToIso8601());

						FString recordActorData = bUseBase64
							                          ? FBase64::Encode(record.ActorData.GetData(), record.ActorData.Num())
							                          : BytesToHex(record.ActorData.GetData(), record.ActorData.Num());
						CurrentPlayerObject->SetStringField("actor_data", recordActorData);

						///////////////////////////////
						///////////////////////////////
						//FMemoryReader MemoryReader(record.ActorData, true);
						//FVoxelWorldSaveGameArchive Ar(MemoryReader);


						UClass* recordClass = FindObject<UClass>(ANY_PACKAGE, *record.ActorClass);

						if (!recordClass || !IsValid(recordClass)) {
							UE_LOG(LogImportWorldCommandlet, Error, TEXT("[db] ERROR LOADING Class %s from saved player actor %s! Class is null!"), *record.ActorClass, *KeyFstr);
							return;
						}

						TArray<TSharedPtr<FJsonValue>> ActorPropertiesObjects;
						
						for (TFieldIterator<UProperty> PropsIterator(recordClass); PropsIterator; ++PropsIterator) {
							UProperty* Property = *PropsIterator;

							if((Property->GetPropertyFlags() & EPropertyFlags::CPF_SaveGame) != 0){

								TSharedRef<FJsonObject> CurrentPropertyObject = MakeShareable(new FJsonObject);
								
								CurrentPropertyObject->SetStringField("name",Property->GetName());
								CurrentPropertyObject->SetNumberField("size", Property->GetSize());
								CurrentPropertyObject->SetStringField("type",Property->GetCPPType());
								CurrentPropertyObject->SetStringField("desc",Property->GetDesc());

								ActorPropertiesObjects.Add(MakeShareable(new FJsonValueObject(CurrentPropertyObject)));
							}
						}

						CurrentPlayerObject->SetArrayField("properties", ActorPropertiesObjects);
						
						/////////////////////////////////
						/////////////////////////////////
						TArray<TSharedPtr<FJsonValue>> ActorComponentObjects;
						
						for (auto& compRecord : record.ActorComponents) {
								TSharedRef<FJsonObject> CurrentComponentObject = MakeShareable(new FJsonObject);
								CurrentComponentObject->SetStringField("component_class", compRecord.ComponentClass);
								CurrentComponentObject->SetStringField("component_transform", compRecord.ComponentTransform.ToString());
								CurrentComponentObject->SetStringField("component_spawn_if_not_found", FString::FromInt(compRecord.bSpawnIfNotFound));

								FString recordComponentData = bUseBase64
									                              ? FBase64::Encode(compRecord.ComponentData.GetData(), compRecord.ComponentData.Num())
									                              : BytesToHex(compRecord.ComponentData.GetData(), compRecord.ComponentData.Num());
								CurrentComponentObject->SetStringField("component_data", recordComponentData);

								ActorComponentObjects.Add(MakeShareable(new FJsonValueObject(CurrentComponentObject))); 
						}

						CurrentPlayerObject->SetArrayField("components", ActorComponentObjects);
						PlayersDataObject->SetObjectField(KeyFstr.RightChop(21), CurrentPlayerObject);
					}
					
					
					/////////////////////////////////////////////////////////////////////////////////////////////////
					return;
				}

				//default to dumping Key:encode(data)
				FString EncodedData = bUseBase64 ? FBase64::Encode((uint8*)Value.data(), Value.length()) : BytesToHex((uint8*)Value.data(), Value.length()); 
				RootObject.Get().SetStringField(KeyFstr, EncodedData);
			}
		});*/

	RootObject.Get().SetObjectField("players", PlayersDataObject);
	if (bDumpRegions) {
		RootObject.Get().SetObjectField("regions_data", RegionsDataObject);
		if (bDumpTerrain) { RootObject.Get().SetObjectField("regions_terrain", RegionsObject); }
	}

	FString OutputString;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutputString);

	FJsonSerializer::Serialize(RootObject, Writer);

	TArray<FStringFormatArg> Args;
	Args.Emplace(FPaths::ProjectSavedDir());
	Args.Emplace(WorldLocation);
	Args.Emplace(FDateTime::Now().ToUnixTimestamp());

	FString OutPath = FString::Format(TEXT("{0}/{1}_export_{2}.json"), Args);

	FFileHelper::SaveStringToFile(OutputString, *OutPath);

	///////////////////////////////////////////////////////////

	const auto afterConversion = FDateTime::UtcNow() - beforeConversion;
	UE_LOG(LogImportWorldCommandlet, Display, TEXT("World database converted in %f ms."), afterConversion.GetTotalMicroseconds()*0.001);

	StorageProvider->Close();
	delete StorageProvider;

	return 0;
}
