// Copyright (c) 2025 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
using UnrealBuildTool;

// The VRM Interchange translator and the spring bone parser. A runtime module: everything that
// needs the editor (pipelines, asset creation) is in VRMInterchangeEditor (P3.5, PE-09).
public class VRMInterchange : ModuleRules
{
    public VRMInterchange(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Types in this module's public headers
        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InterchangeCore",       // UInterchangeTranslatorBase, UInterchangeBaseNode
            "InterchangeImport",     // mesh and texture payload interfaces
            "VRMCore",               // FVRMDocument, FVRMParsedModel
            "VRMSpringBonesRuntime", // FVRMSpringConfig
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "InterchangeEngine",     // UInterchangeManager, to register the translator
            "InterchangeNodes",
            "InterchangeFactoryNodes",
            "MeshDescription",
            "StaticMeshDescription",
            "SkeletalMeshDescription",
            "AnimationCore",         // bone weights
            "ImageWrapper",          // texture payloads
            "Json",                  // spring bone parser
            "JsonUtilities",         // the avatar description on the VRM node (FJsonObjectConverter)
        });
    }
}
