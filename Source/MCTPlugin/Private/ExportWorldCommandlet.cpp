// Fill out your copyright notice in the Description page of Project Settings.


#include "ExportWorldCommandlet.h"
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

UExportWorldCommandlet::UExportWorldCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer) {
	IsClient = false;
	IsServer = false;
	IsEditor = false;
	LogToConsole = true;

	HelpDescription = "Exports the given world database as a JSON file";

	HelpParamNames.Add("world");
	HelpParamDescriptions.Add("The world database to convert");

	HelpParamNames.Add("out");
	HelpParamDescriptions.Add("Where to put the result of the conversion. Uses same location as the world database if not specified");
}


// json structure

// each region is a json file
// region {
//	materials: {   where 0: ... is the slice or W 
//	0: [1,1,1,1,1,4,2,7,3, ...] for x, for y order? or other way around?
//	1: [1,1,1,1,1,4,2,7,3, ...]
//	}
//	densities: {
//	0: [255,255,255,255,255, ...]
//	1: [255,255,255,255,255, ...]
//	}
//	entities: {
//		[
//		{ transform:"1 2 3|4 5 6|1 1 1", name: "SavedEntity_04", data: "aa658b678o967aw485ga4b..."} // todo maybe theres a way to scan the class for what savegame properties it has, so we can parse data
//		]
//	}
// }
//
// // all other keys into one json file?
// or individual??


int32 UExportWorldCommandlet::Main(const FString& Params) {
	const TCHAR* ParamStr = *Params;
	ParseCommandLine(ParamStr, CmdLineTokens, CmdLineSwitches, CmdLineParams);

	if (!CmdLineParams.Contains("world")) {
		UE_LOG(LogExportWorldCommandlet, Error, TEXT("Please specify a world by adding -world=<world name> to the command line."));
		return 1;
	}

	if (!CmdLineParams.Contains("format")) {
		UE_LOG(LogExportWorldCommandlet, Error, TEXT("Please specify the world's format by adding -format=<world format> to the command line."));
		return 2;
	}

	FString WorldLocation = *CmdLineParams.Find("world");
	FString WorldFormat = CmdLineParams.Find("format")->ToLower();
	FString OutLocation; // todo implement
	bool bUseBase64 = true;

	if (CmdLineSwitches.Contains("base64"))
		bUseBase64 = true;
	if (CmdLineSwitches.Contains("hex"))
		bUseBase64 = false;

	if (CmdLineParams.Contains("out")) { OutLocation = *CmdLineParams.Find("out"); }
	else { OutLocation = WorldLocation; }

	if (WorldFormat == "flatfile" || WorldFormat == "ff") { StorageProvider = new StorageProviderFlatfile(); }
	else if (WorldFormat == "leveldb" || WorldFormat == "ldb") { StorageProvider = new StorageProviderLevelDB(true); }
	else if (WorldFormat == "tmap" || WorldFormat == "tmp") { StorageProvider = new StorageProviderTMap(true); }
	else {
		UE_LOG(LogExportWorldCommandlet, Error, TEXT("Unknown format [%s] specified. Please specify the world's format by adding -format=<world format> to the command line."), *WorldFormat);
		return 3;
	}

	UE_LOG(LogExportWorldCommandlet, Display, TEXT("Connecting to world database [%s]..."), *WorldLocation);

	bool bSuccess = StorageProvider->Open(TCHAR_TO_UTF8(*WorldLocation), false);

	UE_LOG(LogExportWorldCommandlet, Display, TEXT("Database connection to %hs using provider %hs: %s"), StorageProvider->GetDatabasePath(TCHAR_TO_UTF8(*WorldLocation)).c_str(),
	       StorageProvider->GetProviderName(), bSuccess ? TEXT("Success") : TEXT("Failure"));

	if (!bSuccess) {
		UE_LOG(LogExportWorldCommandlet, Error, TEXT("Failed to connect to database %hs using provider %hs. Please ensure that the name and format are correct."),
		       StorageProvider->GetDatabasePath(TCHAR_TO_UTF8(*WorldLocation)).c_str(),
		       StorageProvider->GetProviderName());
		return 4;
	}

	const auto compatible = StorageProvider->VerifyDatabaseFormat(DB_VERSION);

	UE_LOG(LogExportWorldCommandlet, Display, TEXT("Database version for %s: %d. %s"), *WorldLocation, StorageProvider->GetDatabaseFormat(),
	       compatible? TEXT("Version is compatible.") : TEXT("Version is NOT compatible! Cannot load data."));

	if (!compatible) {
		UE_LOG(LogExportWorldCommandlet, Error, TEXT("Incompatible database version for %s: %d. Please try running the conversion with a matching version of the game."), *WorldLocation,
		       StorageProvider->GetDatabaseFormat());
		return 5;
	}

	UE_LOG(LogExportWorldCommandlet, Display, TEXT("Converting database to %s JSON..."), bUseBase64 ? TEXT("Base64"):TEXT("Hex"));

	const auto beforeConversion = FDateTime::UtcNow();

	////////////////////////////////////////////////////////////
	//iterate keys

	//TSharedPtr<FJsonObject> RootJsonObject = MakeShareable(new FJsonObject);

	TSharedRef<FJsonObject> RootObject = MakeShareable(new FJsonObject);
	TSharedRef<FJsonObject> RegionsObject = MakeShareable(new FJsonObject);
	TSharedRef<FJsonObject> RegionsDataObject = MakeShareable(new FJsonObject);
	
	StorageProvider->ForEach([this, &RootObject, &RegionsObject, &RegionsDataObject, bUseBase64](std::string Key, std::string Value) {
		//UE_LOG(LogExportWorldCommandlet, Display, TEXT("[%s] : [%s]"), UTF8_TO_TCHAR(Key.c_str()), *);
		//UE_LOG(LogExportWorldCommandlet, Display, TEXT("[%s] : [%s]"), UTF8_TO_TCHAR(Key.c_str()), *FBase64::Encode((uint8*)Value.data(), Value.length()));

		FString EncodedData;

		if (bUseBase64) { EncodedData = FBase64::Encode((uint8*)Value.data(), Value.length()); }
		else { EncodedData = BytesToHex((uint8*)Value.data(), Value.length()); }

		if (StorageProvider->IsRegionKey(Key)) {
			auto Decoded = StorageProvider->DeserializeLocationToString(Key);

			if (Decoded.W == 0) {
				// this is a unique occurrence per region
				TArray<uint8> RegionBinary;
				StorageProvider->GetRegionBinary(FIntVector(Decoded.X, Decoded.Y, Decoded.Z), RegionBinary);

				FString EncodedRegionData = FBase64::Encode(RegionBinary.GetData(), RegionBinary.Num());

				TArray<FStringFormatArg> Args;
				Args.Emplace(Decoded.X);
				Args.Emplace(Decoded.Y);
				Args.Emplace(Decoded.Z);

				RegionsObject.Get().SetStringField(FString::Format(TEXT("{0}_{1}_{2}"), Args), *EncodedRegionData);
			}
			else {
				if (Decoded.W >= REGION_SIZE) {
					TArray<FStringFormatArg> Args;
					Args.Emplace(Decoded.X);
					Args.Emplace(Decoded.Y);
					Args.Emplace(Decoded.Z);
					Args.Emplace(Decoded.W);

					RegionsDataObject.Get().SetStringField(FString::Format(TEXT("{0}_{1}_{2}_{3}"), Args), *EncodedData);
				}
			}
		}
		else { RootObject.Get().SetStringField(UTF8_TO_TCHAR(Key.c_str()), EncodedData); }
	});

	RootObject.Get().SetObjectField("regions_terrain", RegionsObject);
	RootObject.Get().SetObjectField("regions_data", RegionsDataObject);

	FString OutputString;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutputString);

	FJsonSerializer::Serialize(RootObject, Writer);

	FString OutPath = FPaths::ProjectSavedDir().Append("/").Append(WorldLocation).Append(".json");

	FFileHelper::SaveStringToFile(OutputString, *OutPath);

	///////////////////////////////////////////////////////////

	const auto afterConversion = FDateTime::UtcNow() - beforeConversion;
	UE_LOG(LogExportWorldCommandlet, Display, TEXT("World database converted in %f ms."), afterConversion.GetTotalMicroseconds()*0.001);

	StorageProvider->Close();
	delete StorageProvider;

	return 0;
}
