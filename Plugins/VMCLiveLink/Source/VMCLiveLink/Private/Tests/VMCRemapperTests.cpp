// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "VMCLiveLinkRemapper.h"
#include "Roles/LiveLinkAnimationTypes.h"

namespace VMCRemapperTests
{
	/** Static data as the VMC source publishes it: root, Hips, LeftUpperArm, Spine, and two curves. */
	FLiveLinkStaticDataStruct MakeStatic()
	{
		FLiveLinkStaticDataStruct Static(FLiveLinkSkeletonStaticData::StaticStruct());
		FLiveLinkSkeletonStaticData& Skel = *Static.Cast<FLiveLinkSkeletonStaticData>();
		Skel.SetBoneNames({ TEXT("root"), TEXT("Hips"), TEXT("LeftUpperArm"), TEXT("Spine") });
		Skel.SetBoneParents({ INDEX_NONE, 0, 1, 1 });
		Skel.PropertyNames = { TEXT("Joy"), TEXT("A") };
		return Static;
	}

	FLiveLinkFrameDataStruct MakeFrame()
	{
		FLiveLinkFrameDataStruct Frame(FLiveLinkAnimationFrameData::StaticStruct());
		FLiveLinkAnimationFrameData& Anim = *Frame.Cast<FLiveLinkAnimationFrameData>();
		Anim.Transforms = {
			FTransform(FVector(10, 20, 0)),  // root: carries motion
			FTransform(FVector(0, 0, 95)),   // Hips: carries height
			FTransform::Identity,            // LeftUpperArm: rotation only, as VMC sends it
			FTransform::Identity,            // Spine: rotation only, not in the reference skeleton
		};
		Anim.PropertyValues = { 0.5f, 0.25f };
		return Frame;
	}

	TArray<FName> RemappedBones(FVMCLiveLinkRemapperWorker& Worker)
	{
		FLiveLinkStaticDataStruct Static = MakeStatic();
		Worker.RemapStaticData(Static);
		return Static.Cast<FLiveLinkSkeletonStaticData>()->GetBoneNames();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCRemapperWorkerSnapshotTest, "VMC.Remapper.WorkerSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCRemapperWorkerSnapshotTest::RunTest(const FString& Parameters)
{
	using namespace VMCRemapperTests;
	// A worker keeps the maps it was created with; edits reach Live Link through a new worker.
	UVMCLiveLinkRemapper* Remapper = NewObject<UVMCLiveLinkRemapper>();
	Remapper->BoneNameMap.Add(TEXT("Hips"), TEXT("pelvis"));
	Remapper->CurveNameMap.Add(TEXT("Joy"), TEXT("mouthSmileLeft"));
	const TSharedPtr<FVMCLiveLinkRemapperWorker> Before = StaticCastSharedPtr<FVMCLiveLinkRemapperWorker>(Remapper->CreateWorker());

	const uint32 RevisionBefore = Remapper->GetRevision();
	Remapper->BoneNameMap.Add(TEXT("Hips"), TEXT("hip_edited"));
	Remapper->ApplyPreset(ELLRemapPreset::None); // any change through the API marks the remapper dirty
	TestTrue(TEXT("A change raises the revision"), Remapper->GetRevision() > RevisionBefore);
	const TSharedPtr<FVMCLiveLinkRemapperWorker> After = StaticCastSharedPtr<FVMCLiveLinkRemapperWorker>(Remapper->CreateWorker());

	if (!TestTrue(TEXT("Workers"), Before.IsValid() && After.IsValid() && Before != After)) return false;
	TestEqual(TEXT("The old worker keeps the old name"), RemappedBones(*Before)[1], FName(TEXT("pelvis")));
	TestEqual(TEXT("The new worker has the edit"), RemappedBones(*After)[1], FName(TEXT("hip_edited")));
	TestTrue(TEXT("GetWorker returns the newest worker"), Remapper->GetWorker().Get() == After.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCRemapperRestTranslationTest, "VMC.Remapper.RestTranslations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCRemapperRestTranslationTest::RunTest(const FString& Parameters)
{
	using namespace VMCRemapperTests;
	FVMCRemapConfig Config;
	Config.BoneNameMap.Add(TEXT("LeftUpperArm"), TEXT("upperarm_l"));
	Config.BoneNameMap.Add(TEXT("Hips"), TEXT("pelvis"));
	Config.CurveNameMap.Add(TEXT("Joy"), TEXT("mouthSmileLeft"));
	Config.RefTranslations.Add(TEXT("upperarm_l"), FVector(1, 2, 3));
	Config.RefTranslations.Add(TEXT("pelvis"), FVector(0, 0, 100)); // must not replace the streamed hips
	Config.RefTranslations.Add(TEXT("root"), FVector(7, 7, 7));     // nor the root

	FVMCLiveLinkRemapperWorker Worker(Config);
	FLiveLinkStaticDataStruct Static = MakeStatic();
	Worker.RemapStaticData(Static);
	const FLiveLinkSkeletonStaticData& Skel = *Static.Cast<FLiveLinkSkeletonStaticData>();
	TestEqual(TEXT("Bone renamed"), Skel.GetBoneNames()[2], FName(TEXT("upperarm_l")));
	TestEqual(TEXT("Curve renamed"), Skel.PropertyNames[0], FName(TEXT("mouthSmileLeft")));

	FLiveLinkFrameDataStruct Frame = MakeFrame();
	Worker.RemapFrameData(Static, Frame);
	const TArray<FTransform>& T = Frame.Cast<FLiveLinkAnimationFrameData>()->Transforms;
	TestTrue(TEXT("Root keeps the stream's translation"), T[0].GetTranslation().Equals(FVector(10, 20, 0)));
	TestTrue(TEXT("Hips keeps the stream's translation"), T[1].GetTranslation().Equals(FVector(0, 0, 95)));
	TestTrue(TEXT("Arm gets the target skeleton's rest translation"), T[2].GetTranslation().Equals(FVector(1, 2, 3)));
	TestTrue(TEXT("A bone the target skeleton lacks stays at zero"), T[3].GetTranslation().IsNearlyZero());

	// A translation the stream does send is kept.
	FLiveLinkFrameDataStruct Sent = MakeFrame();
	Sent.Cast<FLiveLinkAnimationFrameData>()->Transforms[2].SetTranslation(FVector(4, 5, 6));
	Worker.RemapFrameData(Static, Sent);
	TestTrue(TEXT("A streamed translation wins"), Sent.Cast<FLiveLinkAnimationFrameData>()->Transforms[2].GetTranslation().Equals(FVector(4, 5, 6)));

	// Turned off, nothing is added.
	FVMCRemapConfig Off = Config;
	Off.bUseRefTranslations = false;
	FVMCLiveLinkRemapperWorker OffWorker(Off);
	FLiveLinkStaticDataStruct OffStatic = MakeStatic();
	OffWorker.RemapStaticData(OffStatic);
	FLiveLinkFrameDataStruct OffFrame = MakeFrame();
	OffWorker.RemapFrameData(OffStatic, OffFrame);
	TestTrue(TEXT("Off: the arm stays at zero"), OffFrame.Cast<FLiveLinkAnimationFrameData>()->Transforms[2].GetTranslation().IsNearlyZero());

	// A frame with a different bone count (built against other static data) is left alone.
	FLiveLinkFrameDataStruct Short = MakeFrame();
	Short.Cast<FLiveLinkAnimationFrameData>()->Transforms.SetNum(2);
	Worker.RemapFrameData(Static, Short);
	TestEqual(TEXT("A mismatched frame is untouched"), Short.Cast<FLiveLinkAnimationFrameData>()->Transforms.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCRemapperInitializeTest, "VMC.Remapper.InitializeKeepsSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCRemapperInitializeTest::RunTest(const FString& Parameters)
{
	// Initialize runs whenever the subject is (re)created; it must not guess a preset or rewrite maps.
	UVMCLiveLinkRemapper* Remapper = NewObject<UVMCLiveLinkRemapper>();
	Remapper->Preset = ELLRemapPreset::VRoid;
	Remapper->BoneNameMap.Add(TEXT("Hips"), TEXT("my_hips"));
	Remapper->CurveNameMap.Add(TEXT("Joy"), TEXT("my_smile"));
	const TMap<FName, FName> Bones = Remapper->BoneNameMap;
	const TMap<FName, FName> Curves = Remapper->CurveNameMap;

	const uint32 RevisionBefore = Remapper->GetRevision();
	Remapper->Initialize(FLiveLinkSubjectKey(FGuid::NewGuid(), TEXT("VMC_Test")));
	Remapper->Initialize(FLiveLinkSubjectKey(FGuid::NewGuid(), TEXT("VMC_Test")));

	TestTrue(TEXT("Preset kept"), Remapper->Preset == ELLRemapPreset::VRoid);
	TestTrue(TEXT("Bone map kept"), Remapper->BoneNameMap.OrderIndependentCompareEqual(Bones));
	TestTrue(TEXT("Curve map kept"), Remapper->CurveNameMap.OrderIndependentCompareEqual(Curves));
	TestTrue(TEXT("Initialize asks for a fresh worker"), Remapper->GetRevision() > RevisionBefore);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
