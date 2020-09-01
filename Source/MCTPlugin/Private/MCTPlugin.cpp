// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MCTPlugin.h"

#define LOCTEXT_NAMESPACE "FMCTPluginModule"

/*
 *
 *
 * 
	DECLARE_LOG_CATEGORY_EXTERN(LogVoxelNet, Log, All);
	DECLARE_LOG_CATEGORY_EXTERN(LogVoxelWorld, Log, All);
	DECLARE_LOG_CATEGORY_EXTERN(LogVoxelDatabase, Log, All);
 */

DEFINE_LOG_CATEGORY(LogVoxelNet);
DEFINE_LOG_CATEGORY(LogVoxelWorld);
DEFINE_LOG_CATEGORY(LogVoxelDatabase);

void FMCTPluginModule::StartupModule() {
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
}

void FMCTPluginModule::ShutdownModule() {
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMCTPluginModule, MCTPlugin)
