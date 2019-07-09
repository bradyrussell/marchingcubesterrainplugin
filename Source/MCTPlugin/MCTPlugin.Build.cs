// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class MCTPlugin : ModuleRules
{
    private string ThirdPartyPath
    {
        get { return Path.GetFullPath(Path.Combine(ModuleDirectory, "../../ThirdParty/")); }
    }
    public MCTPlugin(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        bEnableExceptions = true;

        PublicIncludePaths.Add(Path.Combine(ThirdPartyPath, "polyvox", "include"));


        PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
                "CoreUObject",
                "LDBPlugin",
                "UnrealFastNoisePlugin",
                "RuntimeMeshComponent",
                "LDBPluginSnappy",
                "LDBPluginLevelDB",
                "Projects",
                "Engine",
                "Sockets",
                "Networking"
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
                                 "UnrealFastNoisePlugin",
                                  "LDBPlugin",
                "RuntimeMeshComponent",
                                "LDBPluginSnappy",
                "LDBPluginLevelDB"//"UnrealFastNoisePlugin", "RuntimeMeshComponent"// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
