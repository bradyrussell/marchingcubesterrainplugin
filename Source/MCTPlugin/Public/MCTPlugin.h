// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

	DECLARE_LOG_CATEGORY_EXTERN(LogVoxelNet, Log, All);
	DECLARE_LOG_CATEGORY_EXTERN(LogVoxelWorld, Log, All);
	DECLARE_LOG_CATEGORY_EXTERN(LogVoxelDatabase, Log, All);

class FMCTPluginModule : public IModuleInterface {
public:


	
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
