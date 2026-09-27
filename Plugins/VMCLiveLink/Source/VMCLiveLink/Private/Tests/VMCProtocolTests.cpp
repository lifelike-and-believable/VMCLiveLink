// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "VMCConnectionSettings.h"
#include "VMCFrameAssembler.h"
#include "VMCHumanoid.h"
#include "VMCProtocol.h"
#include "VMCSenderFilter.h"

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
	TestEqual(TEXT("name"), Pose.Name, FName(TEXT("root")));
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
	TestEqual(TEXT("bone name"), Pose.Name, FName(TEXT("Hips")));
	TestFalse(TEXT("Bone/Pos without a name rejected"), VMCProtocol::ParseBonePos(Floats({ 0, 1, 0, 0, 0, 0, 1, 0 }), Pose));
	TestFalse(TEXT("Bone/Pos with 6 floats rejected"), VMCProtocol::ParseBonePos(Floats({ 0, 1, 0, 0, 0, 1 }, TEXT("Hips")), Pose));

	FName Name;
	float Value = 0.f;
	FArgs Blend;
	Blend.Add(FArg::MakeString(TEXT("Joy")));
	Blend.Add(FArg::MakeFloat(0.25f));
	TestTrue(TEXT("Blend/Val parses"), VMCProtocol::ParseBlendVal(Blend, Name, Value));
	TestEqual(TEXT("blend name"), Name, FName(TEXT("Joy")));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCProtocolAvailableTest, "VMC.Protocol.Available",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCProtocolAvailableTest::RunTest(const FString& Parameters)
{
	using namespace VMCProtocol;
	auto Ints = [](std::initializer_list<int32> Values)
	{
		FArgs Args;
		for (int32 V : Values)
		{
			Args.Add(FArg::MakeInt(V));
		}
		return Args;
	};

	// /VMC/Ext/OK comes in three sizes.
	FSenderState State;
	TestTrue(TEXT("1 argument"), ParseAvailable(Ints({ 1 }), State));
	TestTrue(TEXT("... loaded"), State.bLoaded);
	TestFalse(TEXT("... no calibration state"), State.Calibration.IsSet());
	TestEqual(TEXT("Loaded, nothing else said: nothing to show"), DescribeSenderState(State), FString());

	TestTrue(TEXT("3 arguments"), ParseAvailable(Ints({ 1, 2, 0 }), State));
	TestEqual(TEXT("... calibrating"), DescribeSenderState(State), FString(TEXT("calibrating")));

	TestTrue(TEXT("4 arguments"), ParseAvailable(Ints({ 0, 1, 1, 0 }), State));
	TestFalse(TEXT("... not loaded"), State.bLoaded);
	TestEqual(TEXT("... mode"), State.CalibrationMode.Get(-1), 1);
	TestEqual(TEXT("... everything worth saying"), DescribeSenderState(State), FString(TEXT("no avatar loaded, waiting for calibration, tracking lost")));

	TestTrue(TEXT("Calibrated and tracking"), ParseAvailable(Ints({ 1, 3, 0, 1 }), State));
	TestEqual(TEXT("... nothing to show"), DescribeSenderState(State), FString());

	TestFalse(TEXT("2 arguments"), ParseAvailable(Ints({ 1, 3 }), State));
	TestFalse(TEXT("No arguments"), ParseAvailable(FArgs(), State));
	FArgs NotNumber;
	NotNumber.Add(FArg::MakeString(TEXT("yes")));
	TestFalse(TEXT("A string"), ParseAvailable(NotNumber, State));

	// The assembler keeps the last one and reports changes only.
	FVMCFrameAssembler Assembler;
	const FVMCConnectionSettings Conn;
	TestTrue(TEXT("First OK is a change"), Assembler.ApplyMessage(EAddress::Available, Ints({ 1, 2, 0 }), Conn).bSenderStateChanged);
	TestFalse(TEXT("The same again is not"), Assembler.ApplyMessage(EAddress::Available, Ints({ 1, 2, 0 }), Conn).bSenderStateChanged);
	TestTrue(TEXT("Calibrated is"), Assembler.ApplyMessage(EAddress::Available, Ints({ 1, 3, 0 }), Conn).bSenderStateChanged);
	TestTrue(TEXT("Kept"), Assembler.GetSenderState().IsSet() && Assembler.GetSenderState()->Calibration.Get(-1) == 3);
	TestTrue(TEXT("A malformed OK is reported"), Assembler.ApplyMessage(EAddress::Available, Ints({ 1, 3 }), Conn).bMalformed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCSenderFilterTest, "VMC.SenderFilter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCSenderFilterTest::RunTest(const FString& Parameters)
{
	using EResult = FVMCSenderFilter::EResult;
	const FString A = TEXT("192.168.1.20"), B = TEXT("192.168.1.21"), C = TEXT("10.0.0.2");

	FVMCSenderFilter Filter;
	TestFalse(TEXT("Default: inactive"), Filter.IsActive());
	TestTrue(TEXT("Default: anyone"), Filter.Check(A) == EResult::Accepted && Filter.Check(B) == EResult::Accepted);

	Filter.Configure({ A, B }, false);
	TestTrue(TEXT("Allowlist: active"), Filter.IsActive());
	TestTrue(TEXT("Allowed"), Filter.Check(B) == EResult::Accepted);
	TestTrue(TEXT("Not allowed, first time"), Filter.Check(C) == EResult::RejectedNew);
	TestTrue(TEXT("Not allowed, again"), Filter.Check(C) == EResult::Rejected);

	Filter.Configure({}, true);
	TestTrue(TEXT("Lock: the first sender locks"), Filter.Check(C) == EResult::Locked);
	TestEqual(TEXT("... and is remembered"), Filter.GetLockedSender(), C);
	TestTrue(TEXT("... then is accepted"), Filter.Check(C) == EResult::Accepted);
	TestTrue(TEXT("Another sender is not"), Filter.Check(A) == EResult::RejectedNew);

	Filter.Configure({ A, B }, true);
	TestTrue(TEXT("Configure forgets the lock"), Filter.GetLockedSender().IsEmpty());
	TestTrue(TEXT("Both: a sender not allowed doesn't lock"), Filter.Check(C) == EResult::RejectedNew);
	TestTrue(TEXT("Both: the first allowed one does"), Filter.Check(B) == EResult::Locked);
	TestTrue(TEXT("Both: the other allowed one is then rejected"), Filter.Check(A) == EResult::RejectedNew);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCProtocolDevicesTest, "VMC.Protocol.Devices",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCProtocolDevicesTest::RunTest(const FString& Parameters)
{
	using namespace VMCProtocol;
	using VMCProtocolTests::Floats;

	// /VMC/Ext/Cam: name, 7 floats, field of view.
	FPose Pose;
	float Fov = 0.f;
	TestTrue(TEXT("Camera parses"), ParseCamera(Floats({ 1, 2, 3, 0, 0, 0, 1, 60 }, TEXT("Camera")), Pose, Fov));
	TestEqual(TEXT("... name"), Pose.Name, FName(TEXT("Camera")));
	TestEqual(TEXT("... field of view"), Fov, 60.f);
	TestFalse(TEXT("Camera without a field of view"), ParseCamera(Floats({ 1, 2, 3, 0, 0, 0, 1 }, TEXT("Camera")), Pose, Fov));
	TestFalse(TEXT("Camera without a name"), ParseCamera(Floats({ 1, 2, 3, 0, 0, 0, 1, 60, 0 }), Pose, Fov));
	TestEqual(TEXT("Subject names"), MakeDeviceSubjectName(TEXT("VMC_Subject"), TEXT("LHR-1234ABCD")), FName(TEXT("VMC_Subject_LHR-1234ABCD")));

	// The assembler hands devices and the camera back in UE world space, with the root's yaw; the
	// /Local variants are not used.
	FVMCFrameAssembler Assembler;
	FVMCConnectionSettings Conn;
	Conn.YawOffsetDeg = 90.f;
	const FVMCFrameAssembler::FMessageResult Tracker = Assembler.ApplyMessage(EAddress::DevicePos, Floats({ 1, 2, 3, 0, 0, 0, 1 }, TEXT("LHR-1234ABCD")), Conn);
	if (TestTrue(TEXT("A tracker is a device"), Tracker.Device.IsSet()))
	{
		const FQuat Yaw(FVector::UpVector, FMath::DegreesToRadians(90.f));
		const FVector Expected = Yaw.RotateVector(ToUEPosition(FVector3f(1, 2, 3), true, true));
		TestFalse(TEXT("... not a camera"), Tracker.Device->bCamera);
		TestEqual(TEXT("... serial"), Tracker.Device->Name, FName(TEXT("LHR-1234ABCD")));
		TestEqual(TEXT("... converted like the root"), Tracker.Device->Transform.GetLocation(), Expected, 1e-3f);
	}
	const FVMCFrameAssembler::FMessageResult Cam = Assembler.ApplyMessage(EAddress::Camera, Floats({ 0, 1, 0, 0, 0, 0, 1, 45 }, TEXT("Camera")), Conn);
	TestTrue(TEXT("The camera"), Cam.Device.IsSet() && Cam.Device->bCamera && Cam.Device->FieldOfView == 45.f);
	TestFalse(TEXT("/Local poses are not devices"), Assembler.ApplyMessage(EAddress::DevicePosLocal, Floats({ 1, 2, 3, 0, 0, 0, 1 }, TEXT("LHR-1234ABCD")), Conn).Device.IsSet());
	const FVMCFrameAssembler::FMessageResult Bad = Assembler.ApplyMessage(EAddress::DevicePos, Floats({ 1, 2, 3 }, TEXT("LHR-1234ABCD")), Conn);
	TestTrue(TEXT("A short device message is malformed"), Bad.bMalformed && !Bad.Device.IsSet());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS


