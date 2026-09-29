// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// Phase 5 benchmark: the VMC stream path (P5.1) at the plan's target size. Reports timings (test
// info and "PERF" log lines) and only fails on wrong results, never on time: the CI runner is shared.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/PlatformTime.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "VMCConnectionSettings.h"
#include "VMCFrameAssembler.h"
#include "VMCHumanoid.h"
#include "VMCProtocol.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCPerfStreamFrame, "VMC.Perf.StreamFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVMCPerfStreamFrame::RunTest(const FString& Parameters)
{
	using namespace VMCProtocol;
	// 55 bones and 60 curves per frame (the P5.1 target), with names as the receive thread's parser
	// passes them: UTF-8 views into the packet.
	TArray<FName> HumanoidNames;
	TArray<int32> Parents;
	VMCHumanoid::BuildSkeleton(HumanoidNames, Parents);
	TArray<FString> BoneNames;
	for (int32 i = 1; i < HumanoidNames.Num() && BoneNames.Num() < 55; ++i) // index 0 is the root
	{
		BoneNames.Add(HumanoidNames[i].ToString());
	}
	for (int32 i = 0; BoneNames.Num() < 55; ++i)
	{
		BoneNames.Add(FString::Printf(TEXT("Extra_%d"), i));
	}
	TArray<FString> CurveNames;
	for (int32 i = 0; i < 60; ++i)
	{
		CurveNames.Add(FString::Printf(TEXT("Curve_%d"), i));
	}

	auto ToUtf8 = [](const FString& S)
	{
		const FTCHARToUTF8 Converted(*S);
		return TArray<UTF8CHAR>(reinterpret_cast<const UTF8CHAR*>(Converted.Get()), Converted.Length());
	};
	TArray<TArray<UTF8CHAR>> BoneUtf8, CurveUtf8;
	for (const FString& Name : BoneNames) BoneUtf8.Add(ToUtf8(Name));
	for (const FString& Name : CurveNames) CurveUtf8.Add(ToUtf8(Name));

	const FVMCConnectionSettings Settings;
	FVMCFrameAssembler Assembler;
	FVMCFrameAssembler::FFrameOptions Options;
	Options.bUseRefOffsets = false; // as FVMCLiveLinkSource::PushFrame

	auto SendFrame = [&](int32 Frame)
	{
		FArg Args[8];
		const float Angle = Frame * 0.01f;
		Args[1] = FArg::MakeFloat(0.f); Args[2] = FArg::MakeFloat(1.f); Args[3] = FArg::MakeFloat(0.f);
		Args[4] = FArg::MakeFloat(0.f); Args[5] = FArg::MakeFloat(FMath::Sin(Angle)); Args[6] = FArg::MakeFloat(0.f); Args[7] = FArg::MakeFloat(FMath::Cos(Angle));
		Args[0] = FArg::MakeString(TEXT("root"));
		Assembler.ApplyMessage(EAddress::RootPos, MakeArrayView(Args, 8), Settings);
		for (const TArray<UTF8CHAR>& Name : BoneUtf8)
		{
			Args[0] = FArg::MakeUtf8(FUtf8StringView(Name.GetData(), Name.Num()));
			Assembler.ApplyMessage(EAddress::BonePos, MakeArrayView(Args, 8), Settings);
		}
		for (const TArray<UTF8CHAR>& Name : CurveUtf8)
		{
			FArg Blend[2] = { FArg::MakeUtf8(FUtf8StringView(Name.GetData(), Name.Num())), FArg::MakeFloat(0.5f) };
			Assembler.ApplyMessage(EAddress::BlendVal, MakeArrayView(Blend, 2), Settings);
		}
		return Assembler.ApplyMessage(EAddress::BlendApply, {}, Settings).bApply;
	};

	// First frame: every bone and curve is new
	TestTrue(TEXT("Apply completes the first frame"), SendFrame(0));
	Assembler.EndFrame(false);
	TestEqual(TEXT("Curves"), Assembler.GetCurveNames().Num(), 60);

	constexpr int32 Frames = 2000;
	double MessageSeconds = 0.0;
	double BuildSeconds = 0.0;
	int32 Transforms = 0;
	for (int32 Frame = 1; Frame <= Frames; ++Frame)
	{
		double Start = FPlatformTime::Seconds();
		SendFrame(Frame);
		MessageSeconds += FPlatformTime::Seconds() - Start;

		Start = FPlatformTime::Seconds();
		const FLiveLinkFrameDataStruct Data = Assembler.MakeFrameData(Options);
		Assembler.EndFrame(false);
		BuildSeconds += FPlatformTime::Seconds() - Start;
		Transforms = Data.Cast<FLiveLinkAnimationFrameData>()->Transforms.Num();
	}
	TestEqual(TEXT("Every bone in the frame"), Transforms, Assembler.GetBoneNames().Num());

	const auto Report = [this](const FString& What, double Value)
	{
		const FString Line = FString::Printf(TEXT("PERF %s: %.2f us"), *What, Value);
		AddInfo(Line);
		UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
	};
	Report(TEXT("VMC stream, messages per frame (55 bones, 60 curves)"), MessageSeconds * 1e6 / Frames);
	Report(TEXT("VMC stream, frame build per Apply"), BuildSeconds * 1e6 / Frames);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
