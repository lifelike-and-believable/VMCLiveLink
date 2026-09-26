// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// Builds in Editor only
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "HAL/IConsoleManager.h"
#include "VRMSpringBonesParser.h"
#include "VRMSpringBonesTypes.h"

namespace VRMSpringParserTests
{
    /** Sets vrm.SpringBones.LenientSchema for the lifetime of the scope. */
    struct FScopedLenientSchema
    {
        IConsoleVariable* Var = nullptr;
        bool bOld = true;

        explicit FScopedLenientSchema(bool bValue)
        {
            Var = IConsoleManager::Get().FindConsoleVariable(TEXT("vrm.SpringBones.LenientSchema"));
            if (Var)
            {
                bOld = Var->GetBool();
                Var->Set(bValue, ECVF_SetByCode);
            }
        }
        ~FScopedLenientSchema()
        {
            if (Var)
            {
                Var->Set(bOld, ECVF_SetByCode);
            }
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMParseVRM1Json, "VRM.SpringBones.Parse.VRM1",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMParseVRM1Json::RunTest(const FString& Parameters)
{
    // VRMC_springBone 1.0 as the spec writes it: one "shape" per collider, parameters per joint.
    const FString Json = TEXT(R"JSON(
    {
      "asset": {"version":"2.0"},
      "nodes": [ {"name":"Root"}, {"name":"Head"}, {"name":"Hair1"}, {"name":"Hair2"} ],
      "extensions": {
        "VRMC_springBone": {
          "specVersion": "1.0",
          "colliders": [
            { "node": 1, "shape": { "sphere": { "offset":[0,0,0], "radius": 0.02 } } },
            { "node": 1, "shape": { "capsule": { "offset":[0,0,0], "radius": 0.01, "tail":[0,0.1,0] } } }
          ],
          "colliderGroups": [ { "name": "HeadCG", "colliders": [ 0, 1 ] } ],
          "springs": [
            {
              "name": "Hair",
              "center": 0,
              "joints": [
                { "node": 2, "hitRadius": 0.01, "stiffness": 0.8, "dragForce": 0.2, "gravityPower": 1.0, "gravityDir": [1,0,0] },
                { "node": 3 }
              ],
              "colliderGroups": [0]
            }
          ]
        }
      }
    })JSON");

    FVRMSpringConfig Cfg;
    FString Err;
    TestTrue(TEXT("Parse VRM1 JSON"), VRM::ParseSpringBonesFromJson(Json, Cfg, Err));
    TestEqual(TEXT("Spec == VRM1"), (int32)Cfg.Spec, (int32)EVRMSpringSpec::VRM1);
    TestTrue(TEXT("IsValid()"), Cfg.IsValid());
    if (TestEqual(TEXT("Colliders"), Cfg.Colliders.Num(), 2))
    {
        TestEqual(TEXT("Sphere collider"), Cfg.Colliders[0].Spheres.Num(), 1);
        TestEqual(TEXT("Capsule collider"), Cfg.Colliders[1].Capsules.Num(), 1);
    }
    TestEqual(TEXT("ColliderGroups"), Cfg.ColliderGroups.Num(), 1);
    if (!TestEqual(TEXT("Springs"), Cfg.Springs.Num(), 1) || !TestEqual(TEXT("Joints"), Cfg.Joints.Num(), 2))
    {
        return false;
    }
    TestEqual(TEXT("Center node"), Cfg.Springs[0].CenterNodeIndex, 0);

    const float Tolerance = 1.0e-3f;
    const FVRMSpringJoint& First = Cfg.Joints[0];
    TestEqual(TEXT("Joint node"), First.NodeIndex, 2);
    TestEqual(TEXT("Joint stiffness"), First.Stiffness, 0.8f, Tolerance);
    TestEqual(TEXT("Joint drag (dragForce)"), First.Drag, 0.2f, Tolerance);
    TestEqual(TEXT("Joint gravity power (UE units)"), First.GravityPower, 100.f, Tolerance);
    TestEqual(TEXT("Joint hit radius (cm)"), First.HitRadius, 1.f, Tolerance);
    TestTrue(TEXT("Joint gravity +X stays +X"), First.GravityDir.Equals(FVector(1, 0, 0), Tolerance));

    // A joint that sets nothing gets the spec defaults, not the previous joint's values.
    const FVRMSpringJoint& Second = Cfg.Joints[1];
    TestEqual(TEXT("Default stiffness"), Second.Stiffness, 1.f, Tolerance);
    TestEqual(TEXT("Default drag"), Second.Drag, 0.5f, Tolerance);
    TestEqual(TEXT("Default gravity power"), Second.GravityPower, 0.f, Tolerance);
    TestEqual(TEXT("Default hit radius"), Second.HitRadius, 0.f, Tolerance);
    TestTrue(TEXT("Default gravity is down"), Second.GravityDir.Equals(FVector(0, 0, -1), Tolerance));

    // The spring-level fields are editor helpers holding the first joint's values.
    TestEqual(TEXT("Spring shows first joint stiffness"), Cfg.Springs[0].Stiffness, 0.8f, Tolerance);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMParseVRM1ExtendedCollider, "VRM.SpringBones.Parse.VRM1ExtendedCollider",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMParseVRM1ExtendedCollider::RunTest(const FString& Parameters)
{
    // VRMC_springBone_extended_collider: its shape replaces the base shape, which is only a
    // fallback for readers without the extension.
    const FString Json = TEXT(R"JSON(
    {
      "asset": {"version":"2.0"},
      "nodes": [ {"name":"Root"}, {"name":"Spine"}, {"name":"Hair"} ],
      "extensions": {
        "VRMC_springBone": {
          "specVersion": "1.0",
          "colliders": [
            { "node": 1, "shape": { "sphere": { "offset":[0,0,0], "radius": 0.05 } },
              "extensions": { "VRMC_springBone_extended_collider": { "specVersion": "1.0",
                "shape": { "sphere": { "offset":[0,0,0], "radius": 0.3, "inside": true } } } } },
            { "node": 1, "shape": { "sphere": { "offset":[0,0,0], "radius": 0.05 } },
              "extensions": { "VRMC_springBone_extended_collider": { "specVersion": "1.0",
                "shape": { "plane": { "offset":[0,0,0], "normal":[0,1,0] } } } } }
          ],
          "colliderGroups": [ { "name": "Body", "colliders": [ 0, 1 ] } ],
          "springs": [ { "name": "Hair", "joints": [ { "node": 2 } ], "colliderGroups": [0] } ]
        }
      }
    })JSON");

    FVRMSpringConfig Cfg;
    FString Err;
    TestTrue(TEXT("Parse"), VRM::ParseSpringBonesFromJson(Json, Cfg, Err));
    if (!TestEqual(TEXT("Colliders"), Cfg.Colliders.Num(), 2))
    {
        return false;
    }

    const FVRMSpringCollider& Inside = Cfg.Colliders[0];
    if (TestEqual(TEXT("Extended sphere replaces the base sphere"), Inside.Spheres.Num(), 1))
    {
        TestEqual(TEXT("Extended radius (cm)"), Inside.Spheres[0].Radius, 30.f, 1.0e-3f);
        TestTrue(TEXT("Extended sphere is inside"), Inside.Spheres[0].bInside);
    }

    const FVRMSpringCollider& Plane = Cfg.Colliders[1];
    TestEqual(TEXT("Base sphere dropped for the plane"), Plane.Spheres.Num(), 0);
    if (TestEqual(TEXT("Plane"), Plane.Planes.Num(), 1))
    {
        TestTrue(TEXT("Plane normal glTF up is UE up"), Plane.Planes[0].Normal.Equals(FVector(0, 0, 1), 1.0e-3f));
    }
    return true;
}

namespace VRMSpringParserTests
{
    // Layouts earlier exporters or drafts used: a "shapes" array, spring-level parameters, "drag".
    static const TCHAR* NonSpecJson = TEXT(R"JSON(
    {
      "asset": {"version":"2.0"},
      "nodes": [ {"name":"Root"}, {"name":"Head"}, {"name":"Hair"} ],
      "extensions": {
        "VRMC_springBone": {
          "colliders": [ { "node": 1, "shapes": [ { "sphere": { "offset":[0,0,0], "radius": 0.02 } } ] } ],
          "colliderGroups": [ { "name": "HeadCG", "colliders": [ 0 ] } ],
          "springs": [
            { "name": "Hair", "stiffness": 0.7, "drag": 0.3, "joints": [ { "node": 2 } ], "colliderGroups": [0] }
          ]
        }
      }
    })JSON");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMParseVRM1Lenient, "VRM.SpringBones.Parse.VRM1Lenient",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMParseVRM1Lenient::RunTest(const FString& Parameters)
{
    using namespace VRMSpringParserTests;
    {
        FScopedLenientSchema Lenient(true);
        FVRMSpringConfig Cfg;
        FString Err;
        TestTrue(TEXT("Lenient: parse"), VRM::ParseSpringBonesFromJson(NonSpecJson, Cfg, Err));
        if (TestEqual(TEXT("Lenient: collider"), Cfg.Colliders.Num(), 1))
        {
            TestEqual(TEXT("Lenient: 'shapes' array read"), Cfg.Colliders[0].Spheres.Num(), 1);
        }
        if (TestEqual(TEXT("Lenient: joint"), Cfg.Joints.Num(), 1))
        {
            TestEqual(TEXT("Lenient: spring-level stiffness reaches the joint"), Cfg.Joints[0].Stiffness, 0.7f, 1.0e-3f);
            TestEqual(TEXT("Lenient: 'drag' reaches the joint"), Cfg.Joints[0].Drag, 0.3f, 1.0e-3f);
        }
    }
    {
        // With the lenient schema off, the non-spec parts are ignored (and warned about).
        AddExpectedError(TEXT("ignored non-spec layout"), EAutomationExpectedErrorFlags::Contains, 0);
        FScopedLenientSchema Strict(false);
        FVRMSpringConfig Cfg;
        FString Err;
        TestTrue(TEXT("Strict: parse"), VRM::ParseSpringBonesFromJson(NonSpecJson, Cfg, Err));
        if (TestEqual(TEXT("Strict: collider kept (group indices stay valid)"), Cfg.Colliders.Num(), 1))
        {
            TestEqual(TEXT("Strict: 'shapes' array ignored"), Cfg.Colliders[0].Spheres.Num(), 0);
        }
        if (TestEqual(TEXT("Strict: joint"), Cfg.Joints.Num(), 1))
        {
            TestEqual(TEXT("Strict: spec default stiffness"), Cfg.Joints[0].Stiffness, 1.f, 1.0e-3f);
            TestEqual(TEXT("Strict: spec default drag"), Cfg.Joints[0].Drag, 0.5f, 1.0e-3f);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMParseVRM1NumericJoint, "VRM.SpringBones.Parse.VRM1NumericJoint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMParseVRM1NumericJoint::RunTest(const FString& Parameters)
{
    // A number in springs[].joints is not a spec joint. It used to be appended as a node index next
    // to a non-spec top-level joints array (X-05). Now both are warned about and skipped.
    const FString Json = TEXT(R"JSON(
    {
      "asset": {"version":"2.0"},
      "nodes": [ {"name":"Root"}, {"name":"Hair1"}, {"name":"Hair2"} ],
      "extensions": {
        "VRMC_springBone": {
          "joints": [ { "node": 1 }, { "node": 2 } ],
          "springs": [ { "name": "Hair", "joints": [ 0, { "node": 2 } ] } ]
        }
      }
    })JSON");

    AddExpectedError(TEXT("top-level 'joints' array"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedError(TEXT("not a joint object"), EAutomationExpectedErrorFlags::Contains, 1);

    FVRMSpringConfig Cfg;
    FString Err;
    TestTrue(TEXT("Parse"), VRM::ParseSpringBonesFromJson(Json, Cfg, Err));
    TestEqual(TEXT("Only the joint object became a joint"), Cfg.Joints.Num(), 1);
    if (TestEqual(TEXT("Spring"), Cfg.Springs.Num(), 1))
    {
        TestEqual(TEXT("Spring has one joint"), Cfg.Springs[0].JointIndices.Num(), 1);
    }
    if (Cfg.Joints.Num() == 1)
    {
        TestEqual(TEXT("Joint node"), Cfg.Joints[0].NodeIndex, 2);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMParseVRM0Json, "VRM.SpringBones.Parse.VRM0",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMParseVRM0Json::RunTest(const FString& Parameters)
{
    // Minimal VRM0 secondaryAnimation JSON
    const FString Json = TEXT(R"JSON(
    {
      "asset": {"version":"2.0"},
      "extensions": {
        "VRM": {
          "secondaryAnimation": {
            "colliderGroups": [
              { "node": 1, "colliders": [ { "offset":[0,0,0], "radius": 0.02 } ] }
            ],
            "boneGroups": [
              {
                "comment": "Hair",
                "center": 0,
                "stiffiness": 0.7,
                "dragForce": 0.2,
                "gravityDir": {"x": 0, "y": -1, "z": 0},
                "gravityPower": 1.0,
                "hitRadius": 0.03,
                "bones": [ 2, 3 ],
                "colliderGroups": [ 0 ]
              }
            ]
          }
        }
      }
    })JSON");

    FVRMSpringConfig Cfg;
    FString Err;
    TestTrue(TEXT("Parse VRM0 JSON"), VRM::ParseSpringBonesFromJson(Json, Cfg, Err));
    TestEqual(TEXT("Spec == VRM0"), (int32)Cfg.Spec, (int32)EVRMSpringSpec::VRM0);
    TestEqual(TEXT("Colliders"), Cfg.Colliders.Num(), 1);
    TestEqual(TEXT("ColliderGroups"), Cfg.ColliderGroups.Num(), 1);
    TestEqual(TEXT("Springs"), Cfg.Springs.Num(), 1);
    TestTrue(TEXT("IsValid()"), Cfg.IsValid());

    // Every joint gets the bone group's parameters.
    if (TestEqual(TEXT("Joints"), Cfg.Joints.Num(), 2))
    {
        for (const FVRMSpringJoint& Joint : Cfg.Joints)
        {
            TestEqual(TEXT("Joint stiffness from group ('stiffiness')"), Joint.Stiffness, 0.7f, 1.0e-3f);
            TestEqual(TEXT("Joint drag from group"), Joint.Drag, 0.2f, 1.0e-3f);
            TestEqual(TEXT("Joint gravity power from group (UE units)"), Joint.GravityPower, 100.f, 1.0e-3f);
            TestEqual(TEXT("Joint hit radius from group (cm)"), Joint.HitRadius, 3.f, 1.0e-3f);
            TestTrue(TEXT("Joint gravity is down"), Joint.GravityDir.Equals(FVector(0, 0, -1), 1.0e-3f));
        }
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
