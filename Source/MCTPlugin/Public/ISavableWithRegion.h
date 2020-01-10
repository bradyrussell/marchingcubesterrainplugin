// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ISavableWithRegion.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UISavableWithRegion : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class MCTPLUGIN_API IISavableWithRegion
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:

	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Savable With Region") FTransform GetSaveTransform();
	//static FTransform GetSaveTransform();
};
