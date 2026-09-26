// Copyright (c) 2024-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "VRMSpringBonesPostImportPipeline.h"
#include "VRMSpringBoneData.h"
#include "VRMSpringBonesParser.h"
#include "VRMInterchangeSettings.h"
#include "InterchangeSourceData.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMPipelineValidation, "VRM.Pipeline.ValidationTest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMPipelineValidation::RunTest(const FString& Parameters)
{
    // Test pipeline setup and basic functionality
    UVRMSpringBonesPostImportPipeline* Pipeline = NewObject<UVRMSpringBonesPostImportPipeline>();
    
    TestTrue(TEXT("Pipeline object created"), Pipeline != nullptr);
    
    if (Pipeline)
    {
        // The pipeline copies the project's VRM Interchange settings when it is created, so its
        // values depend on the project config (this project's DefaultGame.ini enables them).
        // Check that it follows the settings rather than asserting fixed values.
        const UVRMInterchangeSettings* Settings = GetDefault<UVRMInterchangeSettings>();
        TestNotNull(TEXT("Settings object exists"), Settings);
        if (Settings)
        {
            TestEqual(TEXT("bGenerateSpringBoneData follows settings"), Pipeline->bGenerateSpringBoneData, Settings->bGenerateSpringBoneData);
            TestEqual(TEXT("bOverwriteExisting follows settings"), Pipeline->bOverwriteExisting, Settings->bOverwriteExistingSpringAssets);
            TestEqual(TEXT("bGeneratePostProcessAnimBP follows settings"), Pipeline->bGeneratePostProcessAnimBP, Settings->bGeneratePostProcessAnimBP);
            TestEqual(TEXT("bAssignPostProcessABP follows settings"), Pipeline->bAssignPostProcessABP, Settings->bAssignPostProcessABP);
        }

        // Test subfolder settings (the spring pipeline puts its animation assets under SpringBones;
        // "Animation" is the Live Link pipeline's default)
        TestEqual(TEXT("Animation subfolder default"), Pipeline->AnimationSubFolder, FString(TEXT("SpringBones")));
        TestEqual(TEXT("Main subfolder default"), Pipeline->SubFolder, FString(TEXT("SpringBones")));
        
        // Test that pipeline doesn't crash with minimal inputs
        UInterchangeBaseNodeContainer* NodeContainer = NewObject<UInterchangeBaseNodeContainer>();
        TArray<UInterchangeSourceData*> SourceDatas;
        FString ContentBasePath = TEXT("/Game/TestContent");
        
        // With no source data this should return early without error
        Pipeline->bGenerateSpringBoneData = false;
        Pipeline->ExecutePipeline(NodeContainer, SourceDatas, ContentBasePath);
        
        TestTrue(TEXT("Pipeline executes without error when disabled"), true);
    }
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMTemplateAnimBPValidation, "VRM.Pipeline.TemplateABPValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMTemplateAnimBPValidation::RunTest(const FString& Parameters)
{
    // Test that the template AnimBP can be loaded
    const TCHAR* TemplatePath = TEXT("/VRMInterchange/Animation/ABP_VRMSpringBones_Template.ABP_VRMSpringBones_Template");
    
    UObject* TemplateObj = StaticLoadObject(UObject::StaticClass(), nullptr, TemplatePath);
    
    if (!TemplateObj)
    {
        AddWarning(FString::Printf(TEXT("Template AnimBlueprint not found at '%s'. This is expected in minimal test environments."), TemplatePath));
        return true; // Not a hard failure - template may not exist in all test scenarios
    }
    
    TestTrue(TEXT("Template AnimBlueprint loads successfully"), TemplateObj != nullptr);
    
    // Check if it's actually an AnimBlueprint
    UAnimBlueprint* TemplateABP = Cast<UAnimBlueprint>(TemplateObj);
    if (TemplateABP)
    {
        TestTrue(TEXT("Template is valid AnimBlueprint"), true);
        AddInfo(TEXT("Template AnimBlueprint validation passed"));
    }
    else
    {
        AddWarning(TEXT("Loaded template object is not an AnimBlueprint"));
    }
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMEndToEndParsingValidation, "VRM.Pipeline.EndToEndParsing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMEndToEndParsingValidation::RunTest(const FString& Parameters)
{
    // Create a temporary test file and validate the parsing pipeline
    const FString TestDir = FPaths::ProjectSavedDir() / TEXT("VRMTests");
    IFileManager::Get().MakeDirectory(*TestDir, true);
    
    const FString TestGltfPath = TestDir / TEXT("ValidationTest.gltf");
    
    // Create a comprehensive test VRM with both 0.x and 1.0 style data for robustness
    const FString TestContent = TEXT(R"JSON({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [
    {"name": "Root"},
    {"name": "Head", "translation": [0, 1.6, 0]},
    {"name": "Hair_Root", "translation": [0, 1.8, 0]},
    {"name": "Hair_Mid", "translation": [0, 1.85, 0]},
    {"name": "Hair_Tip", "translation": [0, 1.9, 0]}
  ],
  "extensions": {
    "VRMC_springBone": {
      "specVersion": "1.0",
      "colliders": [
        {
          "node": 1,
          "shape": {"sphere": {"offset": [0, 0, 0], "radius": 0.12}}
        }
      ],
      "colliderGroups": [
        {"name": "HeadGroup", "colliders": [0]}
      ],
      "springs": [
        {
          "name": "MainHairSpring",
          "joints": [
            {"node": 2, "hitRadius": 0.02, "stiffness": 0.7, "dragForce": 0.3, "gravityDir": [0, -1, 0], "gravityPower": 0.15},
            {"node": 3, "hitRadius": 0.015, "stiffness": 0.7, "dragForce": 0.3, "gravityDir": [0, -1, 0], "gravityPower": 0.15},
            {"node": 4, "hitRadius": 0.01}
          ],
          "colliderGroups": [0],
          "center": 0
        }
      ]
    }
  }
})JSON");

    // Save test file
    if (!FFileHelper::SaveStringToFile(TestContent, *TestGltfPath))
    {
        AddError(TEXT("Failed to create test file"));
        return false;
    }
    
    // Test file-based parsing
    FVRMSpringConfig Config;
    FString Error;
    
    const bool bParseSuccess = VRM::ParseSpringBonesFromFile(TestGltfPath, Config, Error);
    
    TestTrue(TEXT("File parsing succeeds"), bParseSuccess);
    
    if (bParseSuccess)
    {
        TestEqual(TEXT("VRM 1.0 detected"), Config.Spec, EVRMSpringSpec::VRM1);
        TestEqual(TEXT("Has 1 collider"), Config.Colliders.Num(), 1);
        TestEqual(TEXT("Has 1 collider group"), Config.ColliderGroups.Num(), 1);
        TestEqual(TEXT("Has 3 joints"), Config.Joints.Num(), 3);
        TestEqual(TEXT("Has 1 spring"), Config.Springs.Num(), 1);
        
        TestTrue(TEXT("Configuration is valid"), Config.IsValid());
        
        // Test spring configuration details
        if (Config.Springs.Num() > 0)
        {
            const FVRMSpring& Spring = Config.Springs[0];
            TestEqual(TEXT("Spring has correct name"), Spring.Name, FString(TEXT("MainHairSpring")));
            TestEqual(TEXT("Spring has 3 joints"), Spring.JointIndices.Num(), 3);
            TestEqual(TEXT("Spring references collider group"), Spring.ColliderGroupIndices.Num(), 1);
            for (const int32 JointIndex : Spring.JointIndices)
            {
                if (!TestTrue(TEXT("Joint index valid"), Config.Joints.IsValidIndex(JointIndex))) continue;
                const FVRMSpringJoint& Joint = Config.Joints[JointIndex];
                TestTrue(TEXT("Joint stiffness in valid range"), Joint.Stiffness >= 0.0f && Joint.Stiffness <= 1.0f);
                TestTrue(TEXT("Joint drag in valid range"), Joint.Drag >= 0.0f && Joint.Drag <= 1.0f);
                TestTrue(TEXT("Joint gravity power reasonable (UE units)"), Joint.GravityPower >= 0.0f && Joint.GravityPower <= 100.0f);
            }
            // The spring's joints are glTF nodes 2, 3 and 4, in order (node indices, not array indices).
            for (int32 i = 0; i < Spring.JointIndices.Num(); ++i)
            {
                if (Config.Joints.IsValidIndex(Spring.JointIndices[i]))
                {
                    TestEqual(*FString::Printf(TEXT("Spring joint %d is node %d"), i, 2 + i), Config.Joints[Spring.JointIndices[i]].NodeIndex, 2 + i);
                }
            }
        }
        
        AddInfo(FString::Printf(TEXT("Successfully parsed VRM with %d springs, %d colliders, %d joints"), 
            Config.Springs.Num(), Config.Colliders.Num(), Config.Joints.Num()));
    }
    else
    {
        AddError(FString::Printf(TEXT("File parsing failed: %s"), *Error));
    }
    
    // Cleanup
    IFileManager::Get().Delete(*TestGltfPath);
    
    return bParseSuccess;
}

#endif // WITH_DEV_AUTOMATION_TESTS