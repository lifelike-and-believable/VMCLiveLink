// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "VRMDocument.h"
#include "VRMSpringBonesTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMResolveNamesFromGltf, "VRM.SpringBones.ResolveNamesFromGltf",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMResolveNamesFromGltf::RunTest(const FString& Parameters)
{
    // Create a minimal .gltf on disk with 3 named nodes
    const FString TempDir = FPaths::ProjectSavedDir() / TEXT("VRMTests");
    IFileManager::Get().MakeDirectory(*TempDir, true);
    const FString GltfPath = TempDir / TEXT("NodesOnly.gltf");
    const FString GltfJson = TEXT(R"JSON(
    {
      "asset": {"version":"2.0"},
      "nodes": [
        { "name":"Center", "children":[1] },
        { "name":"Head", "children":[2] },
        { "name":"Hair_01" }
      ]
    })JSON");
    if (!FFileHelper::SaveStringToFile(GltfJson, *GltfPath))
    {
        AddError(TEXT("Failed to write temporary .gltf"));
        return false;
    }

    FString Error;
    const TSharedPtr<const FVRMDocument> Document = FVRMDocument::LoadFile(GltfPath, Error);
    if (!TestTrue(FString::Printf(TEXT("Document loads (%s)"), *Error), Document.IsValid()))
    {
        return false;
    }
    TestEqual(TEXT("Three nodes"), Document->GetNodes().Num(), 3);
    TestEqual(TEXT("Head's parent"), Document->GetNodes()[1].Parent, 0);
    TestTrue(TEXT("Head's children"), Document->GetNodes()[1].Children == TArray<int32>{ 2 });
    TestEqual(TEXT("Center is a root"), Document->GetNodes()[0].Parent, int32(INDEX_NONE));
    TestEqual(TEXT("Out of range"), Document->GetNodeName(3), FName(NAME_None));
    TestTrue(TEXT("Not a VRM file"), Document->GetVersion() == VRM::Coord::EVRMVersion::Unknown);

    // A config that references those node indices resolves to the node names, as the spring
    // pipeline does.
    FVRMSpringConfig Cfg;
    Cfg.Spec = EVRMSpringSpec::VRM1;

    FVRMSpringCollider Collider;
    Collider.NodeIndex = 1; // Head
    Cfg.Colliders.Add(Collider);

    FVRMSpringJoint Joint;
    Joint.NodeIndex = 2; // Hair_01
    Cfg.Joints.Add(Joint);

    FVRMSpring Spring;
    Spring.CenterNodeIndex = 0; // Center
    Spring.JointIndices = { 0 }; // references Cfg.Joints[0]
    Cfg.Springs.Add(Spring);

    for (FVRMSpringCollider& C : Cfg.Colliders) { C.BoneName = Document->GetNodeName(C.NodeIndex); }
    for (FVRMSpringJoint& J : Cfg.Joints) { J.BoneName = Document->GetNodeName(J.NodeIndex); }
    for (FVRMSpring& S : Cfg.Springs) { S.CenterBoneName = Document->GetNodeName(S.CenterNodeIndex); }
    TestEqual(TEXT("Collider BoneName == Head"), Cfg.Colliders[0].BoneName, FName(TEXT("Head")));
    TestEqual(TEXT("Joint BoneName == Hair_01"), Cfg.Joints[0].BoneName, FName(TEXT("Hair_01")));
    TestEqual(TEXT("Spring CenterBoneName == Center"), Cfg.Springs[0].CenterBoneName, FName(TEXT("Center")));

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
