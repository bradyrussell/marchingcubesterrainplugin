// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IRegionUnloadListener.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UIRegionUnloadListener : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class MCTPLUGIN_API IIRegionUnloadListener
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Region Unload Listener") void OnUnloaded();
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Region Unload Listener") FIntVector GetRegion();
};
