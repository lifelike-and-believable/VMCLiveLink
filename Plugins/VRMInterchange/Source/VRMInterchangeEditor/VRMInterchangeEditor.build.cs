// Copyright (c) 2025 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
using UnrealBuildTool;
using System.IO;

public class VRMInterchangeEditor : ModuleRules
{
    public VRMInterchangeEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        // Editor-only
        if (!Target.bBuildEditor)
        {
            throw new BuildException("VRMInterchangeEditor is editor-only.");
        }

        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            // Interchange
            "InterchangeCore",
            "InterchangeEngine",
            "InterchangeEditor",
            "InterchangeNodes",
            "InterchangeFactoryNodes",
            "InterchangePipelines",
            "DeveloperSettings",
            // Asset authoring
            "AssetRegistry",
            "AssetTools",
            "UnrealEd",     // for package/asset creation utilities

            // We parse JSON directly from the VRM/GLB container
            "Json",

            "Projects",

            // Editor notifications (pipeline registration prompt)
            "Slate",
            "SlateCore",

            // Runtime plugin module this editor module depends on
            "VRMInterchange",
            "VRMCore",
            "VRMSpringBonesRuntime",
            "AnimGraphRuntime", // spring anim node base class, used by the node tests
            "ImageWrapper",     // texture tests build PNGs in memory
            "InterchangeImport", // FImportImage, which the texture tests inspect

            // For IKRigDefinition asset
            "IKRig",

            // UEdGraphSchema_K2 pin types, for the actor wiring tests
            "BlueprintGraph",
        });

        // The tests in Private/Tests include this module's private headers. VRMInterchange's headers
        // come from the module dependency above, not from its folder (P3.5, PE-09).
        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));
    }

}