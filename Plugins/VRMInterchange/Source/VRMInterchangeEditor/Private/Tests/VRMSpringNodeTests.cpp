// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// Tests for the spring bone anim node's robustness (P1.14): data swapped while running, two
// evaluations in one frame, resets, and bones appearing or disappearing with LOD.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AnimNode_VRMSpringBones.h"
#include "VRMSpringBoneData.h"
#include "Animation/Skeleton.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "Math/RandomStream.h"
#include "Misc/MemStack.h"

namespace VRMSpringNodeTests
{
	/** Root -> Hair1 -> Hair2 -> Hair3, hanging down 10 cm per bone, 100 cm above the root. */
	struct FRig
	{
		USkeleton* Skeleton = nullptr;
		USkeletalMesh* Mesh = nullptr;

		FRig()
		{
			Mesh = NewObject<USkeletalMesh>(GetTransientPackage());
			Skeleton = NewObject<USkeleton>(GetTransientPackage());
			{
				FReferenceSkeletonModifier Modifier(Mesh->GetRefSkeleton(), Skeleton);
				Modifier.Add(FMeshBoneInfo(TEXT("Root"), TEXT("Root"), INDEX_NONE), FTransform::Identity);
				Modifier.Add(FMeshBoneInfo(TEXT("Hair1"), TEXT("Hair1"), 0), FTransform(FVector(0, 0, 100)));
				Modifier.Add(FMeshBoneInfo(TEXT("Hair2"), TEXT("Hair2"), 1), FTransform(FVector(0, 0, -10)));
				Modifier.Add(FMeshBoneInfo(TEXT("Hair3"), TEXT("Hair3"), 2), FTransform(FVector(0, 0, -10)));
			}
			Skeleton->MergeAllBonesToBoneTree(Mesh, /*bShowProgress*/ false);
			Mesh->SetSkeleton(Skeleton);
		}
	};

	/** A pose of the rig with only the listed bones required, as an LOD would. Poses allocate from the
	 *  thread's FMemStack, so a test must hold an FMemMark for as long as its poses live. */
	struct FPose
	{
		FBoneContainer Bones;
		FCompactPose Compact;
		FCSPose<FCompactPose> CS;

		FPose(const FRig& Rig, const TArray<FBoneIndexType>& Required)
			: Bones(Required, UE::Anim::FCurveFilterSettings(), *Rig.Skeleton)
		{
			Compact.SetBoneContainer(&Bones);
			Compact.ResetToRefPose();
			CS.InitPose(Compact);
		}

		/** A fresh component-space pose for the next evaluation (the anim graph provides one each frame). */
		void Reset()
		{
			CS.InitPose(Compact);
		}
	};

	/** A VRM 1.0 spring over Hair1..Hair3. Hair3 only marks the tail, so Hair1 and Hair2 are simulated. */
	UVRMSpringBoneData* MakeHair(float Stiffness = 0.2f)
	{
		UVRMSpringBoneData* Data = NewObject<UVRMSpringBoneData>(GetTransientPackage());
		FVRMSpringConfig& Cfg = Data->SpringConfig;
		Cfg.Spec = EVRMSpringSpec::VRM1;
		for (const TCHAR* Bone : { TEXT("Hair1"), TEXT("Hair2"), TEXT("Hair3") })
		{
			FVRMSpringJoint Joint;
			Joint.BoneName = Bone;
			Joint.Stiffness = Stiffness;
			Joint.Drag = 0.4f;
			Joint.GravityDir = FVector(1, 0, 0); // sideways, so the chain visibly moves
			Joint.GravityPower = 500.f;
			Cfg.Joints.Add(Joint);
		}
		FVRMSpring Spring;
		Spring.Name = TEXT("Hair");
		Spring.JointIndices = { 0, 1, 2 };
		Cfg.Springs.Add(Spring);
		return Data;
	}

	void Step(FAnimNode_VRMSpringBones& Node, FPose& Pose, TArray<FBoneTransform>& Out, const FTransform& ComponentTM = FTransform::Identity)
	{
		Pose.Reset();
		Node.BeginFrame(1.f / 30.f);
		Node.EvaluateInternal(nullptr, Pose.CS, ComponentTM, Out);
	}

	bool SameTransforms(const TArray<FBoneTransform>& A, const TArray<FBoneTransform>& B)
	{
		if (A.Num() != B.Num()) return false;
		for (int32 i = 0; i < A.Num(); ++i)
		{
			if (A[i].BoneIndex != B[i].BoneIndex || !A[i].Transform.Equals(B[i].Transform, 1.0e-3f)) return false;
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMSpringNodeEvaluateTwice, "VRM.SpringBones.Node.EvaluateTwice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMSpringNodeEvaluateTwice::RunTest(const FString& Parameters)
{
	using namespace VRMSpringNodeTests;
	FMemMark Mark(FMemStack::Get()); // poses allocate from the mem stack, as inside an anim graph evaluation
	FRig Rig;
	FPose Pose(Rig, { 0, 1, 2, 3 });
	FAnimNode_VRMSpringBones Node;
	Node.SpringData = MakeHair();
	Node.RebuildForBones(Pose.Bones);

	TArray<FBoneTransform> First, Second;
	Step(Node, Pose, First);
	Step(Node, Pose, First); // a second frame, so the chain has moved
	Pose.Reset();
	Node.EvaluateInternal(nullptr, Pose.CS, FTransform::Identity, Second); // same frame again

	TestEqual(TEXT("Both simulated joints are written"), First.Num(), 2);
	TestTrue(TEXT("A second evaluation in the same frame gives the same pose"), SameTransforms(First, Second));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMSpringNodeSwapData, "VRM.SpringBones.Node.SwapData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMSpringNodeSwapData::RunTest(const FString& Parameters)
{
	using namespace VRMSpringNodeTests;
	FMemMark Mark(FMemStack::Get()); // poses allocate from the mem stack, as inside an anim graph evaluation
	FRig Rig;
	FPose Pose(Rig, { 0, 1, 2, 3 });
	FAnimNode_VRMSpringBones Node;
	Node.SpringData = MakeHair();
	Node.RebuildForBones(Pose.Bones);
	TArray<FBoneTransform> Out;
	Step(Node, Pose, Out);

	// Bones not in the skeleton are reported once per asset.
	AddExpectedError(TEXT("not in the skeleton"), EAutomationExpectedErrorFlags::Contains, 0);

	// Swap in malformed data (bad joint, spring, collider group and collider indices; unknown bones;
	// spring chains longer than the joint list) and edit it in place without telling the node.
	// Nothing may read out of bounds.
	FRandomStream Random(1234);
	auto RandomIndex = [&Random]() { return Random.RandRange(-3, 8); };
	const TCHAR* BoneNames[] = { TEXT("Hair1"), TEXT("Hair2"), TEXT("Hair3"), TEXT("Root"), TEXT("NotABone") };
	for (int32 Round = 0; Round < 50; ++Round)
	{
		UVRMSpringBoneData* Data = NewObject<UVRMSpringBoneData>(GetTransientPackage());
		FVRMSpringConfig& Cfg = Data->SpringConfig;
		Cfg.Spec = EVRMSpringSpec::VRM1;
		for (int32 j = Random.RandRange(1, 5); j > 0; --j)
		{
			FVRMSpringJoint Joint;
			Joint.BoneName = BoneNames[Random.RandRange(0, UE_ARRAY_COUNT(BoneNames) - 1)];
			Joint.HitRadius = 2.f;
			Cfg.Joints.Add(Joint);
		}
		for (int32 c = Random.RandRange(0, 2); c > 0; --c)
		{
			FVRMSpringCollider Collider;
			Collider.BoneName = BoneNames[Random.RandRange(0, UE_ARRAY_COUNT(BoneNames) - 1)];
			Collider.Spheres.AddDefaulted();
			Collider.Spheres[0].Radius = 5.f;
			Cfg.Colliders.Add(Collider);
		}
		FVRMSpringColliderGroup Group;
		Group.ColliderIndices = { RandomIndex(), RandomIndex() };
		Cfg.ColliderGroups.Add(Group);
		for (int32 s = Random.RandRange(1, 3); s > 0; --s)
		{
			FVRMSpring Spring;
			for (int32 k = Random.RandRange(0, 6); k > 0; --k) { Spring.JointIndices.Add(RandomIndex()); }
			Spring.ColliderGroupIndices = { RandomIndex() };
			Cfg.Springs.Add(Spring);
		}

		Node.SpringData = Data;
		Step(Node, Pose, Out);

		// In-place edits the node isn't told about.
		if (Cfg.Joints.Num() > 1) { Cfg.Joints.Pop(); }
		Cfg.Springs[0].JointIndices.Add(RandomIndex());
		Step(Node, Pose, Out);
	}

	// Back to good data: the chain simulates again.
	Node.SpringData = MakeHair();
	Step(Node, Pose, Out);
	TestEqual(TEXT("Good data after bad data writes both joints"), Out.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMSpringNodeReset, "VRM.SpringBones.Node.Reset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMSpringNodeReset::RunTest(const FString& Parameters)
{
	using namespace VRMSpringNodeTests;
	FMemMark Mark(FMemStack::Get()); // poses allocate from the mem stack, as inside an anim graph evaluation
	FRig Rig;
	FPose Pose(Rig, { 0, 1, 2, 3 });
	UVRMSpringBoneData* Data = MakeHair();

	// Run a while, teleport the component far away and reset dynamics.
	FAnimNode_VRMSpringBones Node;
	Node.SpringData = Data;
	Node.RebuildForBones(Pose.Bones);
	TArray<FBoneTransform> Out;
	for (int32 Frame = 0; Frame < 20; ++Frame)
	{
		Step(Node, Pose, Out);
	}
	const FTransform Teleported(FQuat(FVector::UpVector, UE_HALF_PI), FVector(10000, -5000, 300));
	Node.ResetDynamics(ETeleportType::ResetPhysics);
	Step(Node, Pose, Out, Teleported);

	// A reset node must behave like a new one: the chain restarts from the current pose.
	FAnimNode_VRMSpringBones Fresh;
	Fresh.SpringData = Data;
	Fresh.RebuildForBones(Pose.Bones);
	TArray<FBoneTransform> FreshOut;
	Step(Fresh, Pose, FreshOut, Teleported);
	TestTrue(TEXT("After a reset the node matches a new node"), SameTransforms(Out, FreshOut));

	// And the chain isn't stretched: consecutive joints stay a bone length (10 cm) apart.
	if (TestEqual(TEXT("Two joints"), Out.Num(), 2))
	{
		for (int32 i = 1; i < Out.Num(); ++i)
		{
			const float Length = FVector::Dist(Out[i].Transform.GetLocation(), Out[i - 1].Transform.GetLocation());
			TestTrue(FString::Printf(TEXT("Segment %d length %.3f cm is the bone length"), i, Length), FMath::IsNearlyEqual(Length, 10.f, 0.05f));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMSpringNodeLOD, "VRM.SpringBones.Node.LODChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMSpringNodeLOD::RunTest(const FString& Parameters)
{
	using namespace VRMSpringNodeTests;
	FMemMark Mark(FMemStack::Get()); // poses allocate from the mem stack, as inside an anim graph evaluation
	FRig Rig;
	FPose Full(Rig, { 0, 1, 2, 3 });
	FPose Reduced(Rig, { 0, 1, 2 }); // Hair3 dropped, as a lower LOD might
	FAnimNode_VRMSpringBones Node;
	Node.SpringData = MakeHair();
	TArray<FBoneTransform> Out;

	Node.RebuildForBones(Full.Bones);
	Step(Node, Full, Out);
	TestEqual(TEXT("Full LOD writes two joints"), Out.Num(), 2);

	Node.RebuildForBones(Reduced.Bones);
	Step(Node, Reduced, Out);
	TestEqual(TEXT("Reduced LOD writes only Hair1 (Hair2 has lost its tail)"), Out.Num(), 1);

	// Hair3 comes back: its state must be set up before it is simulated, so it matches a new node.
	Node.RebuildForBones(Full.Bones);
	Step(Node, Full, Out);
	FAnimNode_VRMSpringBones Fresh;
	Fresh.SpringData = Node.SpringData;
	Fresh.RebuildForBones(Full.Bones);
	TArray<FBoneTransform> FreshOut;
	Step(Fresh, Full, FreshOut);
	TestTrue(TEXT("After the LOD returns, the node matches a new node"), SameTransforms(Out, FreshOut));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
