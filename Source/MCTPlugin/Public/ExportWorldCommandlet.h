// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "StorageProviderBase.h"
#include "ExportWorldCommandlet.generated.h"

DEFINE_LOG_CATEGORY_STATIC(LogExportWorldCommandlet, Log, All);

/**
 * 
 */
UCLASS()
class MCTPLUGIN_API UExportWorldCommandlet : public UCommandlet
{
	GENERATED_UCLASS_BODY()

	/** Parsed commandline tokens */
	TArray<FString> CmdLineTokens;

	/** Parsed commandline switches */
	TArray<FString> CmdLineSwitches;

	TMap<FString,FString> CmdLineParams;

	StorageProviderBase* StorageProvider;
	
	//UExportWorldCommandlet(const FObjectInitializer& ObjectInitializer);
public:

	int32 Main(const FString& Params) override;
};
