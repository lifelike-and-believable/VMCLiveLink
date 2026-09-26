// Copyright (c) 2025 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
using UnrealBuildTool;

public class VRMSpringBonesEditor : ModuleRules
{
    public VRMSpringBonesEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "BlueprintGraph",
            "AnimGraph",
            "AnimGraphRuntime",
            "VRMSpringBonesRuntime",
            "VRMCore" // the VRM Expressions node (P4.2)
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "UnrealEd",
            "Slate",
            "SlateCore"
        });
    }
}
