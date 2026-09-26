// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// Copyright (c) 2024 Lifelike & Believable Animation Design, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "VRMSpringBonesParser.h"
#include "VRMSpringBonesTypes.h"
#include "VRMInterchangeSettings.h"
#include "VRMSpringBonesPostImportPipeline.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMIntegrationTestVRM10, "VRM.Integration.ParseVRM10File",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMIntegrationTestVRM10::RunTest(const FString& Parameters)
{
    // Create minimal VRM 1.0 test file
    const FString TestJson = TEXT(R"JSON({
  "asset": {
    "generator": "VRM Test Generator",
    "version": "2.0"
  },
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [
    {"name": "Root"},
    {"name": "Head", "translation": [0, 1.6, 0]},
    {"name": "Hair_01", "translation": [0, 1.8, 0]},
    {"name": "Hair_02", "translation": [0, 1.9, 0]}
  ],
  "extensions": {
    "VRMC_springBone": {
      "specVersion": "1.0",
      "colliders": [
        {
          "node": 1,
          "shape": {
            "sphere": {
              "offset": [0, 0, 0],
              "radius": 0.15
            }
          }
        }
      ],
      "colliderGroups": [
        {
          "name": "HeadCollider",
          "colliders": [0]
        }
      ],
      "springs": [
        {
          "name": "HairSpring",
          "joints": [
            {"node": 2, "hitRadius": 0.02, "stiffness": 0.8, "dragForce": 0.2, "gravityDir": [0, -1, 0], "gravityPower": 0.1},
            {"node": 3, "hitRadius": 0.015, "stiffness": 0.8, "dragForce": 0.2, "gravityDir": [0, -1, 0], "gravityPower": 0.1}
          ],
          "colliderGroups": [0],
          "center": 0
        }
      ]
    }
  }
})JSON");

    // Test parsing
    FVRMSpringConfig Config;
    FString Error;
    
    const bool bSuccess = VRM::ParseSpringBonesFromJson(TestJson, Config, Error);
    
    TestTrue(TEXT("VRM 1.0 parsing succeeds"), bSuccess);
    
    if (bSuccess)
    {
        TestEqual(TEXT("VRM 1.0 spec detected"), Config.Spec, EVRMSpringSpec::VRM1);
        TestEqual(TEXT("Colliders count"), Config.Colliders.Num(), 1);
        TestEqual(TEXT("Collider groups count"), Config.ColliderGroups.Num(), 1);
        TestEqual(TEXT("Joints count"), Config.Joints.Num(), 2);
        TestEqual(TEXT("Springs count"), Config.Springs.Num(), 1);
        
        // Validate spring configuration
        if (Config.Springs.Num() > 0)
        {
            const FVRMSpring& Spring = Config.Springs[0];
            TestEqual(TEXT("Spring name"), Spring.Name, FString(TEXT("HairSpring")));
            TestEqual(TEXT("Spring joint count"), Spring.JointIndices.Num(), 2);
            if (Spring.JointIndices.Num() == 2 && Config.Joints.IsValidIndex(Spring.JointIndices[0]) && Config.Joints.IsValidIndex(Spring.JointIndices[1]))
            {
                TestEqual(TEXT("First joint node"), Config.Joints[Spring.JointIndices[0]].NodeIndex, 2);
                TestEqual(TEXT("Second joint node"), Config.Joints[Spring.JointIndices[1]].NodeIndex, 3);
            }
            TestEqual(TEXT("Spring collider group count"), Spring.ColliderGroupIndices.Num(), 1);
            for (const int32 JointIndex : Spring.JointIndices)
            {
                if (!Config.Joints.IsValidIndex(JointIndex)) continue;
                TestEqual(TEXT("Joint stiffness"), Config.Joints[JointIndex].Stiffness, 0.8f);
                TestEqual(TEXT("Joint drag"), Config.Joints[JointIndex].Drag, 0.2f);
                TestEqual(TEXT("Joint gravity power (UE units)"), Config.Joints[JointIndex].GravityPower, 10.0f);
            }
        }
        
        // Validate collider configuration
        if (Config.Colliders.Num() > 0)
        {
            const FVRMSpringCollider& Collider = Config.Colliders[0];
            TestEqual(TEXT("Collider node index"), Collider.NodeIndex, 1);
            TestEqual(TEXT("Collider sphere count"), Collider.Spheres.Num(), 1);
            
            if (Collider.Spheres.Num() > 0)
            {
                TestEqual(TEXT("Sphere radius (cm)"), Collider.Spheres[0].Radius, 15.0f); // 0.15 m; the parser returns UE units
            }
        }
        
        TestTrue(TEXT("Config is valid"), Config.IsValid());
    }
    else
    {
        AddError(FString::Printf(TEXT("VRM 1.0 parsing failed: %s"), *Error));
    }
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMIntegrationTestVRM0x, "VRM.Integration.ParseVRM0xFile", 
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMIntegrationTestVRM0x::RunTest(const FString& Parameters)
{
    // Create minimal VRM 0.x test file
    const FString TestJson = TEXT(R"JSON({
  "asset": {
    "generator": "VRM Test Generator",
    "version": "2.0"
  },
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [
    {"name": "Root"},
    {"name": "Head", "translation": [0, 1.6, 0]},
    {"name": "Hair_01", "translation": [0, 1.8, 0], "children": [3]},
    {"name": "Hair_02", "translation": [0, 1.9, 0]}
  ],
  "extensions": {
    "VRM": {
      "specVersion": "0.0",
      "secondaryAnimation": {
        "boneGroups": [
          {
            "comment": "Hair",
            "stiffness": 0.8,
            "gravityPower": 0.1,
            "gravityDir": [0, -1, 0],
            "dragForce": 0.2,
            "center": 0,
            "hitRadius": 0.02,
            "bones": [2],
            "colliderGroups": [0]
          }
        ],
        "colliderGroups": [
          {
            "node": 1,
            "colliders": [
              {
                "offset": [0, 0, 0],
                "radius": 0.15
              }
            ]
          }
        ]
      }
    }
  }
})JSON");

    // Test parsing
    FVRMSpringConfig Config;
    FString Error;
    
    const bool bSuccess = VRM::ParseSpringBonesFromJson(TestJson, Config, Error);
    
    TestTrue(TEXT("VRM 0.x parsing succeeds"), bSuccess);
    
    if (bSuccess)
    {
        TestEqual(TEXT("VRM 0.x spec detected"), Config.Spec, EVRMSpringSpec::VRM0);
        TestEqual(TEXT("Springs count"), Config.Springs.Num(), 1);
        TestEqual(TEXT("Collider groups count"), Config.ColliderGroups.Num(), 1);
        
        // Validate spring configuration
        if (Config.Springs.Num() > 0)
        {
            const FVRMSpring& Spring = Config.Springs[0];
            TestEqual(TEXT("Spring stiffness"), Spring.Stiffness, 0.8f);
            TestEqual(TEXT("Spring drag"), Spring.Drag, 0.2f);
            TestEqual(TEXT("Spring gravity power (UE units)"), Spring.GravityPower, 10.0f); // 0.1, scaled like a length
            TestEqual(TEXT("Spring hit radius (cm)"), Spring.HitRadius, 2.0f);

            // "bones" lists the chain root; its child joins the chain. The group's parameters go to every joint.
            TestEqual(TEXT("Root and its child became joints"), Spring.JointIndices.Num(), 2);
            for (const int32 JointIndex : Spring.JointIndices)
            {
                if (!TestTrue(TEXT("Joint index valid"), Config.Joints.IsValidIndex(JointIndex))) continue;
                const FVRMSpringJoint& Joint = Config.Joints[JointIndex];
                TestEqual(TEXT("Joint stiffness from group"), Joint.Stiffness, 0.8f);
                TestEqual(TEXT("Joint drag from group"), Joint.Drag, 0.2f);
                TestEqual(TEXT("Joint gravity power from group (UE units)"), Joint.GravityPower, 10.0f);
                TestEqual(TEXT("Joint hit radius from group (cm)"), Joint.HitRadius, 2.0f);
            }
        }
        
        TestTrue(TEXT("Config is valid"), Config.IsValid());
    }
    else
    {
        AddError(FString::Printf(TEXT("VRM 0.x parsing failed: %s"), *Error));
    }
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMIntegrationTestPipelineSettings, "VRM.Integration.PipelineSettings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMIntegrationTestPipelineSettings::RunTest(const FString& Parameters)
{
    // Test that VRM settings can be accessed and have reasonable defaults
    const UVRMInterchangeSettings* Settings = GetDefault<UVRMInterchangeSettings>();
    
    TestTrue(TEXT("Settings object exists"), Settings != nullptr);
    
    if (Settings)
    {
        // The settings are config (DefaultGame.ini), so their values belong to the project and are
        // not asserted here. What must hold is that new import pipelines pick them up.
        const UVRMSpringBonesPostImportPipeline* Pipeline = NewObject<UVRMSpringBonesPostImportPipeline>();
        TestEqual(TEXT("Pipeline follows bGenerateSpringBoneData"), Pipeline->bGenerateSpringBoneData, Settings->bGenerateSpringBoneData);
        TestEqual(TEXT("Pipeline follows bGeneratePostProcessAnimBP"), Pipeline->bGeneratePostProcessAnimBP, Settings->bGeneratePostProcessAnimBP);
        TestEqual(TEXT("Pipeline follows bAssignPostProcessABP"), Pipeline->bAssignPostProcessABP, Settings->bAssignPostProcessABP);
        TestEqual(TEXT("Pipeline follows bOverwriteExistingSpringAssets"), Pipeline->bOverwriteExisting, Settings->bOverwriteExistingSpringAssets);
    }
    
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS