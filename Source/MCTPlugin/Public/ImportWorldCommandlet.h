// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "StorageProviderBase.h"
#include "ImportWorldCommandlet.generated.h"

DEFINE_LOG_CATEGORY_STATIC(LogImportWorldCommandlet, Log, All);

/**
 * 
 */
UCLASS()
class MCTPLUGIN_API UImportWorldCommandlet : public UCommandlet
{
	GENERATED_UCLASS_BODY()

	/** Parsed commandline tokens */
	TArray<FString> CmdLineTokens;

	/** Parsed commandline switches */
	TArray<FString> CmdLineSwitches;

	TMap<FString,FString> CmdLineParams;

	StorageProviderBase* StorageProvider;
	
	//UImportWorldCommandlet(const FObjectInitializer& ObjectInitializer);
public:

	int32 Main(const FString& Params) override;
};
