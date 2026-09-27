// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// Tests for the IK Rig built from the humanoid map (P4.3), on a skeleton whose bones aren't VRoid's.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "InterchangeSourceData.h"
#include "InterchangeVRMNode.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "ReferenceSkeleton.h"
#include "Rig/IKRigDefinition.h"
#include "RigEditor/IKRigController.h"
#include "UObject/Package.h"
#include "VRMIKRigBuilder.h"
#include "VRMIKRigPostImportPipeline.h"

namespace VRMIKRigTests
{
	using B = EVRMHumanBone;

	struct FBoneSpec
	{
		const TCHAR* Name;
		const TCHAR* Parent; // nullptr for the root
		B Human;             // None for bones the humanoid map doesn't name
	};

	// A full humanoid with 3ds Max-style names, parents before children.
	const FBoneSpec Bones[] = {
		{ TEXT("Root"), nullptr, B::None },
		{ TEXT("Pelvis"), TEXT("Root"), B::Hips },
		{ TEXT("Spine1"), TEXT("Pelvis"), B::Spine },
		{ TEXT("Spine2"), TEXT("Spine1"), B::Chest },
		{ TEXT("Spine3"), TEXT("Spine2"), B::UpperChest },
		{ TEXT("Neck1"), TEXT("Spine3"), B::Neck },
		{ TEXT("Head1"), TEXT("Neck1"), B::Head },
		{ TEXT("L_Clavicle"), TEXT("Spine3"), B::LeftShoulder },
		{ TEXT("L_UpperArm"), TEXT("L_Clavicle"), B::LeftUpperArm },
		{ TEXT("L_Forearm"), TEXT("L_UpperArm"), B::LeftLowerArm },
		{ TEXT("L_Palm"), TEXT("L_Forearm"), B::LeftHand },
		{ TEXT("R_Clavicle"), TEXT("Spine3"), B::RightShoulder },
		{ TEXT("R_UpperArm"), TEXT("R_Clavicle"), B::RightUpperArm },
		{ TEXT("R_Forearm"), TEXT("R_UpperArm"), B::RightLowerArm },
		{ TEXT("R_Palm"), TEXT("R_Forearm"), B::RightHand },
		{ TEXT("L_Thigh"), TEXT("Pelvis"), B::LeftUpperLeg },
		{ TEXT("L_Calf"), TEXT("L_Thigh"), B::LeftLowerLeg },
		{ TEXT("L_Foot"), TEXT("L_Calf"), B::LeftFoot },
		{ TEXT("L_Toe0"), TEXT("L_Foot"), B::LeftToes },
		{ TEXT("R_Thigh"), TEXT("Pelvis"), B::RightUpperLeg },
		{ TEXT("R_Calf"), TEXT("R_Thigh"), B::RightLowerLeg },
		{ TEXT("R_Foot"), TEXT("R_Calf"), B::RightFoot },
		{ TEXT("R_Toe0"), TEXT("R_Foot"), B::RightToes },
		{ TEXT("L_Finger0_1"), TEXT("L_Palm"), B::LeftThumbMetacarpal },
		{ TEXT("L_Finger0_2"), TEXT("L_Finger0_1"), B::LeftThumbProximal },
		{ TEXT("L_Finger0_3"), TEXT("L_Finger0_2"), B::LeftThumbDistal },
		{ TEXT("L_Finger1_1"), TEXT("L_Palm"), B::LeftIndexProximal },
		{ TEXT("L_Finger1_2"), TEXT("L_Finger1_1"), B::LeftIndexIntermediate },
		{ TEXT("L_Finger1_3"), TEXT("L_Finger1_2"), B::LeftIndexDistal },
		{ TEXT("L_Finger2_1"), TEXT("L_Palm"), B::LeftMiddleProximal },
		{ TEXT("L_Finger2_2"), TEXT("L_Finger2_1"), B::LeftMiddleIntermediate },
		{ TEXT("L_Finger2_3"), TEXT("L_Finger2_2"), B::LeftMiddleDistal },
		{ TEXT("L_Finger3_1"), TEXT("L_Palm"), B::LeftRingProximal },
		{ TEXT("L_Finger3_2"), TEXT("L_Finger3_1"), B::LeftRingIntermediate },
		{ TEXT("L_Finger3_3"), TEXT("L_Finger3_2"), B::LeftRingDistal },
		{ TEXT("L_Finger4_1"), TEXT("L_Palm"), B::LeftLittleProximal },
		{ TEXT("L_Finger4_2"), TEXT("L_Finger4_1"), B::LeftLittleIntermediate },
		{ TEXT("L_Finger4_3"), TEXT("L_Finger4_2"), B::LeftLittleDistal },
		{ TEXT("R_Finger0_1"), TEXT("R_Palm"), B::RightThumbMetacarpal },
		{ TEXT("R_Finger0_2"), TEXT("R_Finger0_1"), B::RightThumbProximal },
		{ TEXT("R_Finger0_3"), TEXT("R_Finger0_2"), B::RightThumbDistal },
		{ TEXT("R_Finger1_1"), TEXT("R_Palm"), B::RightIndexProximal },
		{ TEXT("R_Finger1_2"), TEXT("R_Finger1_1"), B::RightIndexIntermediate },
		{ TEXT("R_Finger1_3"), TEXT("R_Finger1_2"), B::RightIndexDistal },
		{ TEXT("R_Finger2_1"), TEXT("R_Palm"), B::RightMiddleProximal },
		{ TEXT("R_Finger2_2"), TEXT("R_Finger2_1"), B::RightMiddleIntermediate },
		{ TEXT("R_Finger2_3"), TEXT("R_Finger2_2"), B::RightMiddleDistal },
		{ TEXT("R_Finger3_1"), TEXT("R_Palm"), B::RightRingProximal },
		{ TEXT("R_Finger3_2"), TEXT("R_Finger3_1"), B::RightRingIntermediate },
		{ TEXT("R_Finger3_3"), TEXT("R_Finger3_2"), B::RightRingDistal },
		{ TEXT("R_Finger4_1"), TEXT("R_Palm"), B::RightLittleProximal },
		{ TEXT("R_Finger4_2"), TEXT("R_Finger4_1"), B::RightLittleIntermediate },
		{ TEXT("R_Finger4_3"), TEXT("R_Finger4_2"), B::RightLittleDistal },
	};

	FVRMAvatarData MakeAvatar()
	{
		FVRMAvatarData Avatar;
		for (const FBoneSpec& Bone : Bones)
		{
			if (Bone.Human != B::None)
			{
				Avatar.HumanoidToBone.Add(Bone.Human, FName(Bone.Name));
			}
		}
		return Avatar;
	}

	/** A skeletal mesh with the bones above, except those named in Skip. */
	USkeletalMesh* MakeMesh(TConstArrayView<const TCHAR*> Skip = {}, UObject* Outer = GetTransientPackage(), FName MeshName = NAME_None)
	{
		USkeletalMesh* Mesh = NewObject<USkeletalMesh>(Outer, MeshName, RF_Transient);
		USkeleton* Skeleton = NewObject<USkeleton>(GetTransientPackage());
		{
			FReferenceSkeletonModifier Modifier(Mesh->GetRefSkeleton(), Skeleton);
			for (const FBoneSpec& Bone : Bones)
			{
				if (Skip.ContainsByPredicate([&Bone](const TCHAR* Name) { return FCString::Strcmp(Name, Bone.Name) == 0; }))
				{
					continue; // skip whole branches, so no bone loses its parent
				}
				const int32 Parent = Bone.Parent ? Mesh->GetRefSkeleton().FindRawBoneIndex(FName(Bone.Parent)) : INDEX_NONE;
				Modifier.Add(FMeshBoneInfo(FName(Bone.Name), Bone.Name, Parent), FTransform(FVector(0, 0, 10)));
			}
		}
		Skeleton->MergeAllBonesToBoneTree(Mesh);
		Mesh->SetSkeleton(Skeleton);
		return Mesh;
	}

	const FVRMIKRigChain* FindChain(const TArray<FVRMIKRigChain>& Chains, const TCHAR* Name)
	{
		return Chains.FindByPredicate([Name](const FVRMIKRigChain& C) { return C.Name == FName(Name); });
	}

	const FBoneChain* FindRigChain(const UIKRigDefinition* IKRig, const TCHAR* Name)
	{
		return IKRig->GetRetargetChains().FindByPredicate([Name](const FBoneChain& C) { return C.ChainName == FName(Name); });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMIKRigChainsTest, "VRM.IKRig.Chains",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMIKRigChainsTest::RunTest(const FString& Parameters)
{
	using namespace VRMIKRigTests;

	// A full humanoid gives every mannequin chain, running between the mapped bones.
	FVRMAvatarData Avatar = MakeAvatar();
	TestEqual(TEXT("Retarget root is the hips bone"), VRMIKRig::RetargetRoot(Avatar), FName(TEXT("Pelvis")));
	TArray<FVRMIKRigChain> Chains = VRMIKRig::MakeChains(Avatar);
	TestEqual(TEXT("Every mannequin chain"), Chains.Num(), 19);
	struct FExpected { const TCHAR* Chain; const TCHAR* Start; const TCHAR* End; };
	for (const FExpected& E : {
		FExpected{ TEXT("Spine"), TEXT("Spine1"), TEXT("Spine3") },
		FExpected{ TEXT("Neck"), TEXT("Neck1"), TEXT("Neck1") },
		FExpected{ TEXT("Head"), TEXT("Head1"), TEXT("Head1") },
		FExpected{ TEXT("LeftClavicle"), TEXT("L_Clavicle"), TEXT("L_Clavicle") },
		FExpected{ TEXT("LeftArm"), TEXT("L_UpperArm"), TEXT("L_Palm") },
		FExpected{ TEXT("RightLeg"), TEXT("R_Thigh"), TEXT("R_Toe0") },
		FExpected{ TEXT("LeftThumb"), TEXT("L_Finger0_1"), TEXT("L_Finger0_3") },
		FExpected{ TEXT("RightPinky"), TEXT("R_Finger4_1"), TEXT("R_Finger4_3") } })
	{
		const FVRMIKRigChain* Chain = FindChain(Chains, E.Chain);
		if (TestNotNull(FString::Printf(TEXT("Chain %s"), E.Chain), Chain))
		{
			TestEqual(FString::Printf(TEXT("%s starts at %s"), E.Chain, E.Start), Chain->Start, FName(E.Start));
			TestEqual(FString::Printf(TEXT("%s ends at %s"), E.Chain, E.End), Chain->End, FName(E.End));
		}
	}

	// Optional bones fall back: no upper chest or chest, no toes, a VRM 0.x-style thumb (no metacarpal).
	Avatar.HumanoidToBone.Remove(B::UpperChest);
	Avatar.HumanoidToBone.Remove(B::Chest);
	Avatar.HumanoidToBone.Remove(B::LeftToes);
	Avatar.HumanoidToBone.Remove(B::LeftThumbMetacarpal);
	Chains = VRMIKRig::MakeChains(Avatar);
	const FVRMIKRigChain* Spine = FindChain(Chains, TEXT("Spine"));
	TestTrue(TEXT("Spine ends at the spine without a chest"), Spine && Spine->End == FName(TEXT("Spine1")));
	const FVRMIKRigChain* LeftLeg = FindChain(Chains, TEXT("LeftLeg"));
	TestTrue(TEXT("Leg ends at the foot without toes"), LeftLeg && LeftLeg->End == FName(TEXT("L_Foot")));
	const FVRMIKRigChain* LeftThumb = FindChain(Chains, TEXT("LeftThumb"));
	TestTrue(TEXT("Thumb starts at the proximal without a metacarpal"), LeftThumb && LeftThumb->Start == FName(TEXT("L_Finger0_2")));

	// Missing required bones leave the chain out; no hips, no root.
	Avatar.HumanoidToBone.Remove(B::RightLittleProximal);
	Avatar.HumanoidToBone.Remove(B::Hips);
	Chains = VRMIKRig::MakeChains(Avatar);
	TestNull(TEXT("No right pinky chain without its first bone"), FindChain(Chains, TEXT("RightPinky")));
	TestTrue(TEXT("No retarget root without hips"), VRMIKRig::RetargetRoot(Avatar).IsNone());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMIKRigBuildTest, "VRM.IKRig.Build",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMIKRigBuildTest::RunTest(const FString& Parameters)
{
	using namespace VRMIKRigTests;
	const FVRMAvatarData Avatar = MakeAvatar();

	// The whole skeleton: the rig gets the hips as its root and all 19 chains.
	{
		USkeletalMesh* Mesh = MakeMesh();
		UIKRigDefinition* IKRig = NewObject<UIKRigDefinition>(GetTransientPackage());
		TArray<FString> Problems;
		if (!TestTrue(TEXT("The rig builds"), VRMIKRig::Build(IKRig, Mesh, Avatar, &Problems)))
		{
			return false;
		}
		TestEqual(TEXT("No problems"), Problems.Num(), 0);
		TestEqual(TEXT("Retarget root"), UIKRigController::GetController(IKRig)->GetRetargetRoot(), FName(TEXT("Pelvis")));
		TestEqual(TEXT("All chains"), IKRig->GetRetargetChains().Num(), 19);
		const FBoneChain* LeftArm = FindRigChain(IKRig, TEXT("LeftArm"));
		if (TestNotNull(TEXT("LeftArm chain"), LeftArm))
		{
			TestEqual(TEXT("LeftArm start"), LeftArm->StartBone.BoneName, FName(TEXT("L_UpperArm")));
			TestEqual(TEXT("LeftArm end"), LeftArm->EndBone.BoneName, FName(TEXT("L_Palm")));
		}

		// Building again (a reimport with overwrite) replaces the chains rather than adding more.
		TestTrue(TEXT("The rig rebuilds"), VRMIKRig::Build(IKRig, Mesh, Avatar));
		TestEqual(TEXT("Still 19 chains"), IKRig->GetRetargetChains().Num(), 19);
	}

	// A mapped bone the skeleton lacks: that chain is skipped and reported.
	{
		const TCHAR* Skip[] = { TEXT("R_Finger4_1"), TEXT("R_Finger4_2"), TEXT("R_Finger4_3") };
		USkeletalMesh* Mesh = MakeMesh(Skip);
		UIKRigDefinition* IKRig = NewObject<UIKRigDefinition>(GetTransientPackage());
		TArray<FString> Problems;
		TestTrue(TEXT("The rig builds without the right pinky"), VRMIKRig::Build(IKRig, Mesh, Avatar, &Problems));
		TestEqual(TEXT("18 chains"), IKRig->GetRetargetChains().Num(), 18);
		TestNull(TEXT("No RightPinky chain"), FindRigChain(IKRig, TEXT("RightPinky")));
		TestTrue(TEXT("The skipped chain is reported"), Problems.Num() == 1 && Problems[0].Contains(TEXT("RightPinky")));
	}

	// A hips bone the skeleton doesn't have: nothing is built.
	{
		FVRMAvatarData NoHips = Avatar;
		NoHips.HumanoidToBone.Add(B::Hips, FName(TEXT("NotABone")));
		UIKRigDefinition* IKRig = NewObject<UIKRigDefinition>(GetTransientPackage());
		TArray<FString> Problems;
		TestFalse(TEXT("No rig without the hips bone"), VRMIKRig::Build(IKRig, MakeMesh(), NoHips, &Problems));
		TestEqual(TEXT("One problem"), Problems.Num(), 1);
	}
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMIKRigPipelineTest, "VRM.IKRig.Pipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMIKRigPipelineTest::RunTest(const FString& Parameters)
{
	// With a humanoid map on the VRM node, the pipeline builds the rig from it instead of copying
	// the VRoid template; a reimport with overwrite rebuilds the same asset.
	using namespace VRMIKRigTests;
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VRMInterchange"));
	if (!TestTrue(TEXT("Plugin found"), Plugin.IsValid()))
	{
		return false;
	}
	const FString ContentBase = TEXT("/Game/VRMIKRigTests");
	UInterchangeSourceData* Source = NewObject<UInterchangeSourceData>();
	Source->SetFilename(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Tests"), TEXT("Fixtures"), TEXT("vrm1_minimal.vrm")));
	UPackage* MeshPackage = CreatePackage(*(ContentBase / TEXT("vrm1_minimal") / TEXT("SK_IKRigTest")));
	USkeletalMesh* Mesh = MakeMesh({}, MeshPackage, FName(TEXT("SK_IKRigTest")));

	auto Run = [&](bool bReimport)
	{
		UInterchangeBaseNodeContainer* Container = NewObject<UInterchangeBaseNodeContainer>();
		UInterchangeVRMNode* Node = NewObject<UInterchangeVRMNode>(Container);
		Container->SetupNode(Node, TEXT("VRM_IKRigTest_Document"), TEXT("VRM_Document"), EInterchangeNodeContainerType::TranslatedAsset);
		Node->SetAvatarData(MakeAvatar());

		UVRMIKRigPostImportPipeline* Pipeline = NewObject<UVRMIKRigPostImportPipeline>();
		Pipeline->bGenerateIKRig = true;
		Pipeline->bOverwriteExisting = true;
		Pipeline->ExecutePipeline(Container, { Source }, ContentBase);
		TestTrue(TEXT("Waiting for the mesh"), Pipeline->HasPendingPostImportWork());
		Pipeline->HandleImportedAsset(Mesh, bReimport);
		return Pipeline->GetLastIKRig();
	};

	UIKRigDefinition* IKRig = Run(false);
	if (!TestNotNull(TEXT("IK Rig made"), IKRig))
	{
		return false;
	}
	TestEqual(TEXT("Named after the mesh, in the IK Rig folder"), IKRig->GetPathName(),
		ContentBase / TEXT("vrm1_minimal") / TEXT("IKRigDefinition") / TEXT("IK_Rig_VRM_SK_IKRigTest.IK_Rig_VRM_SK_IKRigTest"));
	TestTrue(TEXT("Preview mesh is the imported mesh"), IKRig->GetPreviewMesh() == Mesh);
	TestEqual(TEXT("Retarget root from the humanoid map"), UIKRigController::GetController(IKRig)->GetRetargetRoot(), FName(TEXT("Pelvis")));
	TestEqual(TEXT("Chains from the humanoid map"), IKRig->GetRetargetChains().Num(), 19);

	TestTrue(TEXT("Overwrite reuses the IK Rig"), Run(true) == IKRig);
	TestEqual(TEXT("... with the same chains, not twice as many"), IKRig->GetRetargetChains().Num(), 19);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
