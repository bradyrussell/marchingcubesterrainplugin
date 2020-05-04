// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ISavableComponent.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UISavableComponent : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class MCTPLUGIN_API IISavableComponent
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	// if true we will spawn the component on load if there is not one already
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Savable With Region") bool GetSpawnIfNotFound();
	
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Savable With Region") void OnPreSave();
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Savable With Region") void OnSaved();
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Savable With Region") void OnLoaded();
};
