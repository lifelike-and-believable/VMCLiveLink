// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "VMCHumanoid.h"
#include "VMCProtocol.h"

namespace VMCProtocolTests
{
	using VMCProtocol::FArg;
	using VMCProtocol::FArgs;

	static FArgs Floats(std::initializer_list<float> Values, const TCHAR* LeadingName = nullptr)
	{
		FArgs Args;
		if (LeadingName)
		{
			Args.Add(FArg::MakeString(LeadingName));
		}
		for (float V : Values)
		{
			Args.Add(FArg::MakeFloat(V));
		}
		return Args;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCProtocolRootPosTest, "VMC.Protocol.RootPos",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCProtocolRootPosTest::RunTest(const FString& Parameters)
{
	using namespace VMCProtocolTests;
	VMCProtocol::FPose Pose;
	bool bLegacy = true;

	// Spec form: name + 7 floats
	TestTrue(TEXT("8 arguments parse"), VMCProtocol::ParseRootPos(Floats({ 1, 2, 3, 0, 0, 0, 1 }, TEXT("root")), Pose, bLegacy));
	TestFalse(TEXT("8 arguments are not the legacy form"), bLegacy);
	TestEqual(TEXT("name"), Pose.Name, FString(TEXT("root")));
	TestTrue(TEXT("position"), Pose.Position.Equals(FVector3f(1, 2, 3)));
	TestFalse(TEXT("no scale/offset"), Pose.bHasScaleAndOffset);

	// v2.1: name + 7 floats + scale + offset
	TestTrue(TEXT("14 arguments parse"), VMCProtocol::ParseRootPos(Floats({ 1, 2, 3, 0, 0, 0, 1, 2, 2, 2, 0.1f, 0.2f, 0.3f }, TEXT("root")), Pose, bLegacy));
	TestTrue(TEXT("scale/offset present"), Pose.bHasScaleAndOffset);
	TestTrue(TEXT("scale"), Pose.Scale.Equals(FVector3f(2, 2, 2)));
	TestTrue(TEXT("offset"), Pose.Offset.Equals(FVector3f(0.1f, 0.2f, 0.3f)));

	// Legacy non-conformant form: 7 floats, no name
	TestTrue(TEXT("7 floats parse"), VMCProtocol::ParseRootPos(Floats({ 1, 2, 3, 0, 0, 0, 1 }), Pose, bLegacy));
	TestTrue(TEXT("7 floats are the legacy form"), bLegacy);
	TestTrue(TEXT("legacy position"), Pose.Position.Equals(FVector3f(1, 2, 3)));

	// Rejected
	TestFalse(TEXT("11 arguments rejected"), VMCProtocol::ParseRootPos(Floats({ 1, 2, 3, 0, 0, 0, 1, 1, 1, 1 }, TEXT("root")), Pose, bLegacy));
	TestFalse(TEXT("8 floats without a name rejected"), VMCProtocol::ParseRootPos(Floats({ 1, 2, 3, 0, 0, 0, 1, 1 }), Pose, bLegacy));
	FArgs WrongType = Floats({ 1, 2, 3, 0, 0, 0, 1 }, TEXT("root"));
	WrongType[3] = FArg::MakeString(TEXT("oops"));
	TestFalse(TEXT("string in a float slot rejected"), VMCProtocol::ParseRootPos(WrongType, Pose, bLegacy));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCProtocolBoneAndBlendTest, "VMC.Protocol.BoneAndBlend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCProtocolBoneAndBlendTest::RunTest(const FString& Parameters)
{
	using namespace VMCProtocolTests;
	VMCProtocol::FPose Pose;
	TestTrue(TEXT("Bone/Pos parses"), VMCProtocol::ParseBonePos(Floats({ 0, 1, 0, 0, 0, 0, 1 }, TEXT("Hips")), Pose));
	TestEqual(TEXT("bone name"), Pose.Name, FString(TEXT("Hips")));
	TestFalse(TEXT("Bone/Pos without a name rejected"), VMCProtocol::ParseBonePos(Floats({ 0, 1, 0, 0, 0, 0, 1, 0 }), Pose));
	TestFalse(TEXT("Bone/Pos with 6 floats rejected"), VMCProtocol::ParseBonePos(Floats({ 0, 1, 0, 0, 0, 1 }, TEXT("Hips")), Pose));

	FString Name;
	float Value = 0.f;
	FArgs Blend;
	Blend.Add(FArg::MakeString(TEXT("Joy")));
	Blend.Add(FArg::MakeFloat(0.25f));
	TestTrue(TEXT("Blend/Val parses"), VMCProtocol::ParseBlendVal(Blend, Name, Value));
	TestEqual(TEXT("blend name"), Name, FString(TEXT("Joy")));
	TestEqual(TEXT("blend value"), Value, 0.25f);

	Blend[1] = FArg::MakeInt(1);
	TestTrue(TEXT("integer blend value accepted"), VMCProtocol::ParseBlendVal(Blend, Name, Value));
	TestEqual(TEXT("integer blend value"), Value, 1.f);

	Blend[1] = FArg::MakeString(TEXT("1"));
	TestFalse(TEXT("string blend value rejected"), VMCProtocol::ParseBlendVal(Blend, Name, Value));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCProtocolConversionTest, "VMC.Protocol.UnityToUEConversion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCProtocolConversionTest::RunTest(const FString& Parameters)
{
	// Unity: +X right, +Y up, +Z forward (metres). UE: +Z up (cm); a character facing Unity +Z faces UE +Y.
	TestTrue(TEXT("up"), VMCProtocol::ToUEPosition(FVector3f(0, 1, 0), true, true).Equals(FVector(0, 0, 100)));
	TestTrue(TEXT("forward"), VMCProtocol::ToUEPosition(FVector3f(0, 0, 1), true, true).Equals(FVector(0, 100, 0)));
	TestTrue(TEXT("right"), VMCProtocol::ToUEPosition(FVector3f(1, 0, 0), true, false).Equals(FVector(-1, 0, 0)));
	TestTrue(TEXT("no conversion"), VMCProtocol::ToUEPosition(FVector3f(1, 2, 3), false, false).Equals(FVector(1, 2, 3)));

	// Rotating in Unity then converting must equal converting both and rotating in UE.
	const FQuat4f UnityRot = FQuat4f(FVector3f(0.3f, 0.8f, -0.5f).GetSafeNormal(), 0.7f);
	const FVector3f UnityVec(0.2f, -0.4f, 0.9f);
	const FVector3f UnityRotated = UnityRot.RotateVector(UnityVec);

	const FQuat UERot = VMCProtocol::ToUERotation(UnityRot, true);
	const FVector Expected = VMCProtocol::ToUEPosition(UnityRotated, true, false);
	const FVector Actual = UERot.RotateVector(VMCProtocol::ToUEPosition(UnityVec, true, false));
	TestTrue(TEXT("rotation converts consistently with positions"), Actual.Equals(Expected, 1e-4));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCHumanoidSkeletonTest, "VMC.Humanoid.Skeleton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCHumanoidSkeletonTest::RunTest(const FString& Parameters)
{
	TArray<FName> Names;
	TArray<int32> Parents;
	VMCHumanoid::BuildSkeleton(Names, Parents);

	TestEqual(TEXT("root + 55 humanoid bones"), Names.Num(), 56);
	TestEqual(TEXT("parents array matches"), Parents.Num(), Names.Num());
	TestEqual(TEXT("root first"), Names[0], VMCHumanoid::RootBoneName);
	TestEqual(TEXT("Hips at HipsSkeletonIndex"), Names[VMCHumanoid::HipsSkeletonIndex], FName(TEXT("Hips")));

	int32 Roots = 0;
	for (int32 i = 0; i < Parents.Num(); ++i)
	{
		if (Parents[i] == INDEX_NONE)
		{
			++Roots;
		}
		else
		{
			TestTrue(FString::Printf(TEXT("%s: parent precedes child"), *Names[i].ToString()), Parents[i] < i);
		}
	}
	TestEqual(TEXT("exactly one root"), Roots, 1);
	TestEqual(TEXT("Hips parented to root"), Parents[VMCHumanoid::HipsSkeletonIndex], 0);

	auto ParentName = [&](const TCHAR* Bone)
	{
		const int32 Index = Names.IndexOfByKey(FName(Bone));
		return (Index != INDEX_NONE && Parents[Index] != INDEX_NONE) ? Names[Parents[Index]] : NAME_None;
	};
	TestEqual(TEXT("Head under Neck"), ParentName(TEXT("Head")), FName(TEXT("Neck")));
	TestEqual(TEXT("LeftHand under LeftLowerArm"), ParentName(TEXT("LeftHand")), FName(TEXT("LeftLowerArm")));
	TestEqual(TEXT("RightLittleDistal under RightLittleIntermediate"), ParentName(TEXT("RightLittleDistal")), FName(TEXT("RightLittleIntermediate")));

	TestEqual(TEXT("FindBone ignores case"), VMCHumanoid::FindBone(FName(TEXT("lefthand"))), VMCHumanoid::FindBone(FName(TEXT("LeftHand"))));
	TestEqual(TEXT("FindBone unknown"), VMCHumanoid::FindBone(FName(TEXT("TailTip"))), INDEX_NONE);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
