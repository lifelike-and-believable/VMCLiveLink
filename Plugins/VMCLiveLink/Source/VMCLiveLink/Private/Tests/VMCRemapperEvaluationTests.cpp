// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// The remapper's names as Live Link's consumers see them: through the client's evaluation (what the
// Live Link Pose node reads), not through the remapper's worker alone.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Features/IModularFeatures.h"
#include "InterpolationProcessor/LiveLinkAnimationFrameInterpolateProcessor.h"
#include "ILiveLinkClient.h"
#include "LiveLinkPresetTypes.h"
#include "LiveLinkSubjectSettings.h"
#include "UObject/Package.h"
#include "LiveLinkTypes.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "VMCLiveLinkRemapper.h"
#include "VMCLiveLinkSource.h"

namespace VMCRemapperEvaluationTests
{
	void Pad(TArray<uint8>& Out) { while (Out.Num() % 4 != 0) Out.Add(0); }
	void AddString(TArray<uint8>& Out, const char* Text)
	{
		Out.Append(reinterpret_cast<const uint8*>(Text), FCStringAnsi::Strlen(Text));
		Out.Add(0);
		Pad(Out);
	}
	void AddFloat(TArray<uint8>& Out, float Value)
	{
		uint32 Bits;
		FMemory::Memcpy(&Bits, &Value, 4);
		for (int32 Shift = 24; Shift >= 0; Shift -= 8) Out.Add(uint8(Bits >> Shift));
	}

	// One OSC message per packet, as the parser accepts.
	TArray<uint8> BonePos(const char* Bone, const FQuat& UnityRotation)
	{
		TArray<uint8> Out;
		AddString(Out, "/VMC/Ext/Bone/Pos");
		AddString(Out, ",sfffffff");
		AddString(Out, Bone);
		for (float V : { 0.f, 1.f, 0.f, float(UnityRotation.X), float(UnityRotation.Y), float(UnityRotation.Z), float(UnityRotation.W) }) AddFloat(Out, V);
		return Out;
	}
	TArray<uint8> BlendVal(const char* Name, float Value)
	{
		TArray<uint8> Out;
		AddString(Out, "/VMC/Ext/Blend/Val");
		AddString(Out, ",sf");
		AddString(Out, Name);
		AddFloat(Out, Value);
		return Out;
	}
	TArray<uint8> BlendApply()
	{
		TArray<uint8> Out;
		AddString(Out, "/VMC/Ext/Blend/Apply");
		AddString(Out, ",");
		return Out;
	}

	// One VMC frame: Hips turned, a curve, then Apply (which publishes the frame).
	void SendFrame(FVMCLiveLinkSource& Source, float Angle)
	{
		Source.InjectPacketForTest(BonePos("Hips", FQuat(FVector::UpVector, Angle)));
		Source.InjectPacketForTest(BlendVal("a", 0.5f));
		Source.InjectPacketForTest(BlendApply());
	}

	struct FEvaluated
	{
		bool bOk = false;
		TArray<FName> Bones;
		TArray<FName> Curves;
		int32 NumTransforms = 0;
		int32 NumValues = 0;
	};

	FEvaluated Evaluate(ILiveLinkClient& Client, FName Subject)
	{
		FEvaluated Result;
		FLiveLinkSubjectFrameData Frame;
		Result.bOk = Client.EvaluateFrame_AnyThread(Subject, ULiveLinkAnimationRole::StaticClass(), Frame);
		if (const FLiveLinkSkeletonStaticData* Static = Frame.StaticData.Cast<FLiveLinkSkeletonStaticData>())
		{
			Result.Bones = Static->GetBoneNames();
			Result.Curves = Static->PropertyNames;
		}
		if (const FLiveLinkAnimationFrameData* Anim = Frame.FrameData.Cast<FLiveLinkAnimationFrameData>())
		{
			Result.NumTransforms = Anim->Transforms.Num();
			Result.NumValues = Anim->PropertyValues.Num();
		}
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCRemapperEvaluationTest, "VMC.Remapper.Evaluation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVMCRemapperEvaluationTest::RunTest(const FString& Parameters)
{
	using namespace VMCRemapperEvaluationTests;

	IModularFeatures& Features = IModularFeatures::Get();
	if (!TestTrue(TEXT("A Live Link client"), Features.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName)))
	{
		return false;
	}
	ILiveLinkClient& Client = Features.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);

	// A source of its own, on any free port (nothing is sent to it: packets are injected), with a
	// subject name no one else uses.
	FVMCConnectionSettings Settings;
	Settings.BindAddress = TEXT("127.0.0.1");
	Settings.Port = 0;
	Settings.bReceiveThread = true;
	Settings.SubjectName = FName(*FString::Printf(TEXT("VMCRemapperEvaluation_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Short)));
	const TSharedPtr<FVMCLiveLinkSource> Source = MakeShared<FVMCLiveLinkSource>(Settings, TEXT("VMC remapper evaluation test"));
	const FGuid SourceGuid = Client.AddSource(Source);
	const FLiveLinkSubjectKey Key{ SourceGuid, Settings.SubjectName };
	ON_SCOPE_EXIT { Client.RemoveSource(SourceGuid); Client.ForceTick(); };

	// The subject with a VMC remapper, whatever the project's Default Remapper Class (the source
	// keeps the settings of a subject that already exists, as for one restored from a preset).
	{
		FLiveLinkSubjectPreset Preset;
		Preset.Key = Key;
		Preset.Role = ULiveLinkAnimationRole::StaticClass();
		Preset.Settings = NewObject<ULiveLinkSubjectSettings>(GetTransientPackage());
		Preset.Settings->Remapper = NewObject<UVMCLiveLinkRemapper>(Preset.Settings);
		Preset.bEnabled = true;
		Client.CreateSubject(Preset);
		Client.SetSubjectEnabled(Key, true);
	}

	// Frames until the subject exists, and the client has made it a snapshot.
	SendFrame(*Source, 0.1f);
	Client.ForceTick();
	SendFrame(*Source, 0.2f);
	Client.ForceTick();
	FEvaluated Raw = Evaluate(Client, Settings.SubjectName);
	if (!TestTrue(TEXT("Evaluates before mapping"), Raw.bOk)) return false;
	TestTrue(TEXT("Unmapped: VMC's Hips"), Raw.Bones.Contains(FName(TEXT("Hips"))));

	ULiveLinkSubjectSettings* SubjectSettings = Cast<ULiveLinkSubjectSettings>(Client.GetSubjectSettings(Key));
	UVMCLiveLinkRemapper* Remapper = SubjectSettings ? Cast<UVMCLiveLinkRemapper>(SubjectSettings->Remapper) : nullptr;
	if (!TestNotNull(TEXT("The subject has a VMC remapper"), Remapper)) return false;

	// Map, as the Mapping Tools do, and let the source and the client react as on an engine tick.
	auto Map = [&](const TCHAR* HipsTarget)
	{
		Remapper->BoneNameMap.Reset();
		Remapper->BoneNameMap.Add(TEXT("Hips"), FName(HipsTarget));
		Remapper->CurveNameMap.Reset();
		Remapper->CurveNameMap.Add(TEXT("a"), TEXT("TestMouthA"));
		Remapper->bEnableMetaHumanCurveNormalizer = false;
		Remapper->ForceRefreshStaticData();
		Client.ForceTick();
		Source->TickForTest();
		for (int32 i = 0; i < 3; ++i)
		{
			SendFrame(*Source, 0.3f + i * 0.1f);
			Client.ForceTick();
		}
	};

	Map(TEXT("TestHips"));
	FEvaluated Mapped = Evaluate(Client, Settings.SubjectName);
	TestTrue(TEXT("Evaluates after mapping"), Mapped.bOk);
	TestTrue(FString::Printf(TEXT("Mapped bone reaches the evaluated frame (bones: %s)"), *FString::JoinBy(Mapped.Bones.FilterByPredicate([](FName N) { return N.ToString().Contains(TEXT("Hips")); }), TEXT(", "), [](FName N) { return N.ToString(); })),
		Mapped.Bones.Contains(FName(TEXT("TestHips"))) && !Mapped.Bones.Contains(FName(TEXT("Hips"))));
	TestTrue(TEXT("Mapped curve reaches the evaluated frame"), Mapped.Curves.Contains(FName(TEXT("TestMouthA"))) && !Mapped.Curves.Contains(FName(TEXT("a"))));
	TestEqual(TEXT("A transform per bone"), Mapped.NumTransforms, Mapped.Bones.Num());
	TestEqual(TEXT("A value per curve"), Mapped.NumValues, Mapped.Curves.Num());

	// Changing the map again (the Reference Skeleton, a preset) must not fall back to VMC's names.
	Map(TEXT("TestHips2"));
	FEvaluated Remapped = Evaluate(Client, Settings.SubjectName);
	TestTrue(TEXT("Evaluates after remapping"), Remapped.bOk);
	TestTrue(TEXT("The new mapping reaches the evaluated frame"), Remapped.Bones.Contains(FName(TEXT("TestHips2"))) && !Remapped.Bones.Contains(FName(TEXT("Hips"))));

	// A new VMC remapper swapped in by code, and a Live Link tick before the source's: Live Link must
	// not get a worker that applies the remapper on top of the source (the normalizer's added curve
	// would then be added twice, and the frames rejected).
	{
		UVMCLiveLinkRemapper* Swapped = NewObject<UVMCLiveLinkRemapper>(SubjectSettings);
		Swapped->BoneNameMap.Add(TEXT("Hips"), TEXT("TestHips3"));
		Swapped->CurveNameMap.Add(TEXT("a"), TEXT("eyeBlinkLeft"));
		Swapped->bEnableMetaHumanCurveNormalizer = true; // adds eyeBlinkRight
		SubjectSettings->Remapper = Swapped;
		Client.ForceTick();
		Source->TickForTest();
		for (int32 i = 0; i < 3; ++i)
		{
			SendFrame(*Source, 0.7f + i * 0.1f);
			Client.ForceTick();
		}
		FEvaluated Swap = Evaluate(Client, Settings.SubjectName);
		TestTrue(TEXT("Evaluates after the swap"), Swap.bOk);
		TestTrue(TEXT("The swapped remapper's bone name"), Swap.Bones.Contains(FName(TEXT("TestHips3"))));
		TestEqual(TEXT("The normalizer's curve is added once"), Swap.Curves.FilterByPredicate([](FName N) { return N == FName(TEXT("eyeBlinkRight")); }).Num(), 1);
		TestEqual(TEXT("A value per curve after the swap"), Swap.NumValues, Swap.Curves.Num());
	}

	// What the Live Mapping table lists is still what VMC sends.
	TArray<FName> Bones, Curves;
	TestTrue(TEXT("Published names found"), FVMCLiveLinkSource::GetPublishedNames(Key, Bones, Curves));
	TestTrue(TEXT("Published names are VMC's"), Bones.Contains(FName(TEXT("Hips"))) && Curves.Contains(FName(TEXT("a"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCSubjectDefaultsTest, "VMC.Source.SubjectDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVMCSubjectDefaultsTest::RunTest(const FString& Parameters)
{
	using namespace VMCRemapperEvaluationTests;
	IModularFeatures& Features = IModularFeatures::Get();
	if (!TestTrue(TEXT("A Live Link client"), Features.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName)))
	{
		return false;
	}
	ILiveLinkClient& Client = Features.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);

	auto MakeSource = [&Client](TSharedPtr<FVMCLiveLinkSource>& OutSource, FLiveLinkSubjectKey& OutKey)
	{
		FVMCConnectionSettings Settings;
		Settings.BindAddress = TEXT("127.0.0.1");
		Settings.Port = 0;
		Settings.bReceiveThread = true;
		Settings.SubjectName = FName(*FString::Printf(TEXT("VMCSubjectDefaults_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Short)));
		OutSource = MakeShared<FVMCLiveLinkSource>(Settings, TEXT("VMC subject defaults test"));
		OutKey = { Client.AddSource(OutSource), Settings.SubjectName };
	};

	// A new subject: the source creates it with an interpolation processor that blends bones.
	{
		TSharedPtr<FVMCLiveLinkSource> Source;
		FLiveLinkSubjectKey Key;
		MakeSource(Source, Key);
		ON_SCOPE_EXIT { Client.RemoveSource(Key.Source); Client.ForceTick(); };
		SendFrame(*Source, 0.1f);
		Client.ForceTick();
		const ULiveLinkSubjectSettings* Settings = Cast<ULiveLinkSubjectSettings>(Client.GetSubjectSettings(Key));
		if (TestNotNull(TEXT("The new subject's settings"), Settings))
		{
			TestTrue(TEXT("Interpolates animation (bone transforms too)"), Settings->InterpolationProcessor && Settings->InterpolationProcessor->IsA<ULiveLinkAnimationFrameInterpolationProcessor>());
			TestTrue(TEXT("The processor belongs to the subject's settings"), Settings->InterpolationProcessor && Settings->InterpolationProcessor->GetOuter() == Settings);
			// The remapper class is a project setting (Default Remapper Class): only check it's there.
			TestNotNull(TEXT("And a remapper"), Settings->Remapper.Get());
		}
	}

	// A subject that already exists (a preset, the user's settings) keeps its settings.
	{
		TSharedPtr<FVMCLiveLinkSource> Source;
		FLiveLinkSubjectKey Key;
		MakeSource(Source, Key);
		ON_SCOPE_EXIT { Client.RemoveSource(Key.Source); Client.ForceTick(); };
		FLiveLinkSubjectPreset Preset;
		Preset.Key = Key;
		Preset.Role = ULiveLinkAnimationRole::StaticClass();
		Preset.Settings = NewObject<ULiveLinkSubjectSettings>(GetTransientPackage());
		Preset.bEnabled = true;
		Client.CreateSubject(Preset);
		SendFrame(*Source, 0.1f);
		Client.ForceTick();
		const ULiveLinkSubjectSettings* Settings = Cast<ULiveLinkSubjectSettings>(Client.GetSubjectSettings(Key));
		if (TestNotNull(TEXT("The existing subject's settings"), Settings))
		{
			TestNull(TEXT("Its choice of no interpolation is kept"), Settings->InterpolationProcessor.Get());
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCSourceTimingStatsTest, "VMC.Source.TimingStats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVMCSourceTimingStatsTest::RunTest(const FString& Parameters)
{
	// VMC.Stats reports the frames' intervals and how Live Link read the subject, through the
	// editor's Live Link client.
	using namespace VMCRemapperEvaluationTests;
	IModularFeatures& Features = IModularFeatures::Get();
	if (!TestTrue(TEXT("A Live Link client"), Features.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName)))
	{
		return false;
	}
	ILiveLinkClient& Client = Features.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);

	FVMCConnectionSettings Settings;
	Settings.BindAddress = TEXT("127.0.0.1");
	Settings.Port = 0;
	Settings.bReceiveThread = true;
	Settings.SubjectName = FName(*FString::Printf(TEXT("VMCTimingStats_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Short)));
	TSharedPtr<FVMCLiveLinkSource> Source = MakeShared<FVMCLiveLinkSource>(Settings, TEXT("VMC timing stats test"));
	const FGuid SourceGuid = Client.AddSource(Source);
	ON_SCOPE_EXIT { Client.RemoveSource(SourceGuid); Client.ForceTick(); };

	// Settle first: the first frame bootstraps the subject, and Live Link clears a subject's frames
	// when it first builds its remapper's worker (a VMC remapper's is never valid), so how many
	// ticks have frames to read depends on the remapper. Then start counting with a report.
	SendFrame(*Source, 0.1f);
	Client.ForceTick();
	SendFrame(*Source, 0.2f);
	Client.ForceTick();
	Source->GetStatsReport();

	FPlatformProcess::Sleep(0.02f);
	SendFrame(*Source, 0.3f);
	Client.ForceTick();
	Client.ForceTick();
	const FString Report = Source->GetStatsReport();
	AddInfo(Report);
	TArray<FString> Lines;
	Report.ParseIntoArrayLines(Lines);
	FString IntervalLine;
	for (const FString& Line : Lines)
	{
		if (Line.Contains(TEXT("frame interval:")))
		{
			IntervalLine = Line;
		}
	}
	TestTrue(TEXT("One interval"), IntervalLine.EndsWith(TEXT("ms (1)")));
	TestTrue(TEXT("Live Link's reads, once per tick"), Report.Contains(TEXT("Live Link evaluations: 2,")));
	TestTrue(TEXT("The offsets"), Report.Contains(TEXT("smooth offset:")) && Report.Contains(TEXT("clock offset:")));
	TestTrue(TEXT("Collected again after a report"), Source->GetStatsReport().Contains(TEXT("frame interval: no frames")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
