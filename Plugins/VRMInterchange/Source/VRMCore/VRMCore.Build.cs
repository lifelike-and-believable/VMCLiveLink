// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
using UnrealBuildTool;
using System.IO;

// Reads VRM and glTF files: one parse per file into an FVRMDocument, and the glTF to UE
// conversion. The only module that compiles or includes cgltf (P3.3).
public class VRMCore : ModuleRules
{
    public VRMCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject", // the avatar description's USTRUCTs and UENUMs (VRMAvatarTypes.h)
            "Json", // FVRMDocument exposes the parsed top-level JSON
        });

        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "..", "ThirdParty", "cgltf"));
    }
}
