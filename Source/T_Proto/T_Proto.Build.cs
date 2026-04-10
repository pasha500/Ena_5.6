// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class T_Proto : ModuleRules
{
    public T_Proto(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "NavigationSystem",
            "PCG",
            "UMG",
            "Landscape"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "AIModule",
            "AssetRegistry",
            "Json",
            "ImageWrapper"
        });

        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[]
            {
                "UnrealEd",
                "DesktopPlatform",
                "KismetCompiler",
                "LandscapeEditor",
                "ContentBrowser"
            });
        }
    }
}
