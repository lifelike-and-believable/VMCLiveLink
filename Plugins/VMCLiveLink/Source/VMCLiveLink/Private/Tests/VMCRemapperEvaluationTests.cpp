// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// The remapper's names as Live Link's consumers see them: through the client's evaluation (what the
// Live Link Pose node reads), not through the remapper's worker alone.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Features/IModularFeatures.h"
#include "ILiveLinkClient.h"
#include "LiveLinkSubjectSettings.h"
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

	// What the Live Mapping table lists is still what VMC sends.
	TArray<FName> Bones, Curves;
	TestTrue(TEXT("Published names found"), FVMCLiveLinkSource::GetPublishedNames(Key, Bones, Curves));
	TestTrue(TEXT("Published names are VMC's"), Bones.Contains(FName(TEXT("Hips"))) && Curves.Contains(FName(TEXT("a"))));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
