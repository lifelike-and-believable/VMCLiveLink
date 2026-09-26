// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "VMCFrameAssembler.h"
#include "VMCHumanoid.h"
#include "VMCProtocol.h"
#include "Math/RandomStream.h"
#include "Roles/LiveLinkAnimationTypes.h"

namespace VMCFrameAssemblerTests
{
	const FLiveLinkAnimationFrameData& Anim(const FLiveLinkFrameDataStruct& Frame)
	{
		return *Frame.Cast<FLiveLinkAnimationFrameData>();
	}

	const FLiveLinkSkeletonStaticData& Skel(const FLiveLinkStaticDataStruct& Static)
	{
		return *Static.Cast<FLiveLinkSkeletonStaticData>();
	}

	int32 Index(const FVMCFrameAssembler& A, const TCHAR* Bone)
	{
		return A.GetBoneNames().IndexOfByKey(FName(Bone));
	}

	/**
	 * The frame building of FVMCLiveLinkSource before P3.1 (name-keyed maps), kept as the reference
	 * the assembler must match. Same inputs as the old source's state.
	 */
	struct FLegacyModel
	{
		TArray<FName> BoneNames;
		TArray<int32> BoneParents;
		TMap<FName, int32> BoneIndexByName;
		TMap<FName, FTransform> PendingPose;
		FTransform PendingRoot = FTransform::Identity;
		TArray<FName> CurveNamesOrdered;
		TMap<FName, int32> CurveNameToIndex;
		TMap<FName, float> PendingCurves;

		FLegacyModel()
		{
			VMCHumanoid::BuildSkeleton(BoneNames, BoneParents);
			for (int32 i = 0; i < BoneNames.Num(); ++i) BoneIndexByName.Add(BoneNames[i], i);
		}

		void Bone(FName Name, const FTransform& Xf)
		{
			if (!BoneIndexByName.Contains(Name))
			{
				const int32 NewIndex = BoneNames.Add(Name);
				BoneParents.Add(VMCHumanoid::FallbackParentIndex);
				BoneIndexByName.Add(Name, NewIndex);
			}
			PendingPose.Add(Name, Xf);
		}

		void Curve(FName Name, float Value)
		{
			if (!CurveNameToIndex.Contains(Name))
			{
				CurveNameToIndex.Add(Name, CurveNamesOrdered.Add(Name));
			}
			PendingCurves.Add(Name, Value);
		}

		void Frame(const TMap<FName, FName>& BoneMap, const TMap<FName, FVector>& RefOffsets, bool bUseRefOffsets, bool bPreferIncoming,
			TArray<FTransform>& OutTransforms, TArray<float>& OutValues) const
		{
			const bool bHaveRefOffsets = RefOffsets.Num() > 0;
			OutTransforms.SetNum(BoneNames.Num());
			OutValues.Init(0.f, CurveNamesOrdered.Num());
			auto MapBoneName = [&](const FName& Src) { const FName* M = BoneMap.Find(Src); return M ? *M : Src; };
			for (int32 i = 0; i < BoneNames.Num(); ++i)
			{
				const FTransform* In = PendingPose.Find(BoneNames[i]);
				FTransform X = FTransform::Identity;
				if (i == 0)
				{
					X = In ? *In : PendingRoot;
				}
				else
				{
					if (In) X.SetRotation(In->GetRotation());
					const bool bIsHips = (i == VMCHumanoid::HipsSkeletonIndex);
					bool bHaveTranslation = false;
					if (In && (bIsHips || bPreferIncoming))
					{
						X.SetTranslation(In->GetTranslation());
						bHaveTranslation = bIsHips || !X.GetTranslation().IsNearlyZero();
					}
					if (!bHaveTranslation && bUseRefOffsets && bHaveRefOffsets)
					{
						if (const FVector* Off = RefOffsets.Find(MapBoneName(BoneNames[i]))) X.SetTranslation(*Off);
					}
				}
				OutTransforms[i] = X;
			}
			for (const TPair<FName, float>& KV : PendingCurves)
			{
				if (const int32* Idx = CurveNameToIndex.Find(KV.Key)) OutValues[*Idx] = KV.Value;
			}
		}

		void EndFrame(bool bZeroMissingCurves)
		{
			if (bZeroMissingCurves) PendingCurves.Reset();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCFrameAssemblerBasicsTest, "VMC.FrameAssembler.Basics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCFrameAssemblerBasicsTest::RunTest(const FString& Parameters)
{
	using namespace VMCFrameAssemblerTests;
	FVMCFrameAssembler A;
	const int32 Hips = VMCHumanoid::HipsSkeletonIndex;
	const int32 Arm = Index(A, TEXT("LeftUpperArm"));

	const FQuat Turn(FVector::UpVector, 0.5);
	TestFalse(TEXT("A humanoid bone is not new"), A.SetBone(TEXT("Hips"), FTransform(Turn, FVector(0, 0, 90))));
	TestFalse(TEXT("... in any case"), A.SetBone(TEXT("leftupperarm"), FTransform(Turn, FVector(5, 5, 5))));
	A.SetRoot(FTransform(FVector(10, 20, 0)));
	TestTrue(TEXT("A new curve is new"), A.SetCurve(TEXT("Joy"), 0.5f));
	TestFalse(TEXT("... once"), A.SetCurve(TEXT("Joy"), 0.75f));

	TMap<FName, FName> BoneMap;
	BoneMap.Add(TEXT("LeftUpperArm"), TEXT("upperarm_l"));
	TMap<FName, FVector> RefOffsets;
	RefOffsets.Add(TEXT("upperarm_l"), FVector(1, 2, 3));
	FVMCFrameAssembler::FFrameOptions Options;
	Options.BoneMap = &BoneMap;
	Options.RefOffsets = &RefOffsets;

	const FLiveLinkFrameDataStruct Frame = A.MakeFrameData(Options);
	const TArray<FTransform>& T = Anim(Frame).Transforms;
	if (!TestEqual(TEXT("One transform per bone"), T.Num(), A.GetBoneNames().Num())) return false;
	TestTrue(TEXT("Root from Root/Pos"), T[0].GetTranslation().Equals(FVector(10, 20, 0)));
	TestTrue(TEXT("Hips keeps its streamed translation"), T[Hips].GetTranslation().Equals(FVector(0, 0, 90)));
	TestTrue(TEXT("Hips rotation"), T[Hips].GetRotation().Equals(Turn));
	TestTrue(TEXT("Arm rotation"), T[Arm].GetRotation().Equals(Turn));
	TestTrue(TEXT("Arm translation from the reference, looked up by its mapped name"), T[Arm].GetTranslation().Equals(FVector(1, 2, 3)));
	TestEqual(TEXT("Curve value"), Anim(Frame).PropertyValues[0], 0.75f);

	// A Bone/Pos named "root" wins over Root/Pos.
	A.SetBone(VMCHumanoid::RootBoneName, FTransform(FVector(7, 7, 7)));
	TestTrue(TEXT("Root from Bone/Pos 'root'"), Anim(A.MakeFrameData(Options)).Transforms[0].GetTranslation().Equals(FVector(7, 7, 7)));

	// An unknown bone is appended under Hips; indices don't move.
	const int32 NumBefore = A.GetBoneNames().Num();
	TestTrue(TEXT("A non-humanoid bone is new"), A.SetBone(TEXT("TailTip"), FTransform::Identity));
	TestEqual(TEXT("Appended at the end"), Index(A, TEXT("TailTip")), NumBefore);
	TestEqual(TEXT("Parented to Hips"), A.GetBoneParents()[NumBefore], Hips);
	TestEqual(TEXT("Arm keeps its index"), Index(A, TEXT("LeftUpperArm")), Arm);

	// Static data maps names without reordering.
	TMap<FName, FName> CurveMap;
	CurveMap.Add(TEXT("Joy"), TEXT("happy"));
	const FLiveLinkStaticDataStruct Static = A.MakeStaticData(&BoneMap, &CurveMap);
	TestEqual(TEXT("Mapped bone name in place"), Skel(Static).GetBoneNames()[Arm], FName(TEXT("upperarm_l")));
	TestEqual(TEXT("Parents published"), Skel(Static).GetBoneParents().Num(), A.GetBoneNames().Num());
	TestEqual(TEXT("Mapped curve name"), Skel(Static).PropertyNames[0], FName(TEXT("happy")));

	// Curves hold between frames, or read 0 when the sender stopped sending them.
	A.EndFrame(false);
	TestEqual(TEXT("Held"), Anim(A.MakeFrameData(Options)).PropertyValues[0], 0.75f);
	A.EndFrame(true);
	TestEqual(TEXT("Zeroed"), Anim(A.MakeFrameData(Options)).PropertyValues[0], 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCFrameAssemblerMatchesLegacyTest, "VMC.FrameAssembler.MatchesLegacy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCFrameAssemblerMatchesLegacyTest::RunTest(const FString& Parameters)
{
	using namespace VMCFrameAssemblerTests;
	// Random message streams through the assembler and through the pre-P3.1 frame building: every
	// frame and every static publish must be the same.
	const TArray<FName> ExtraBones = { TEXT("TailTip"), TEXT("Ear_L"), TEXT("root") };
	const TArray<FName> Curves = { TEXT("A"), TEXT("I"), TEXT("U"), TEXT("Blink"), TEXT("Joy") };
	TArray<FName> Humanoid;
	for (const VMCHumanoid::FBone& Bone : VMCHumanoid::GetBones()) Humanoid.Add(Bone.Name);

	for (int32 Seed = 1; Seed <= 4; ++Seed)
	{
		FRandomStream Random(Seed);
		const bool bZeroMissing = (Seed % 2) == 0;
		const bool bPreferIncoming = Seed >= 3;

		TMap<FName, FName> BoneMap;
		TMap<FName, FVector> RefOffsets;
		for (int32 i = 0; i < Humanoid.Num(); i += 3)
		{
			const FName Mapped(*FString::Printf(TEXT("mapped_%d"), i));
			BoneMap.Add(Humanoid[i], Mapped);
			RefOffsets.Add(Mapped, Random.GetUnitVector() * 10.0);
		}
		RefOffsets.Add(TEXT("Chest"), FVector(0, 0, 12)); // an unmapped bone with an offset

		FVMCFrameAssembler A;
		FLegacyModel L;
		FVMCFrameAssembler::FFrameOptions Options;
		Options.BoneMap = &BoneMap;
		Options.RefOffsets = &RefOffsets;
		Options.bPreferIncomingTranslations = bPreferIncoming;

		for (int32 Frame = 0; Frame < 40; ++Frame)
		{
			for (int32 m = Random.RandRange(0, 30); m > 0; --m)
			{
				const int32 Kind = Random.RandRange(0, 9);
				if (Kind < 6)
				{
					const FName Bone = Random.FRand() < 0.1f ? ExtraBones[Random.RandRange(0, ExtraBones.Num() - 1)] : Humanoid[Random.RandRange(0, Humanoid.Num() - 1)];
					// Some translations zero, to exercise the "streamed but zero" fallback.
					const FVector Pos = Random.FRand() < 0.3f ? FVector::ZeroVector : Random.GetUnitVector() * 20.0;
					const FTransform Xf(FQuat(Random.GetUnitVector(), Random.FRandRange(-3.f, 3.f)), Pos);
					A.SetBone(Bone, Xf);
					L.Bone(Bone, Xf);
				}
				else if (Kind < 9)
				{
					const FName Curve = Curves[Random.RandRange(0, Curves.Num() - 1)];
					const float Value = Random.FRand();
					A.SetCurve(Curve, Value);
					L.Curve(Curve, Value);
				}
				else
				{
					const FTransform Xf(FQuat(Random.GetUnitVector(), Random.FRand()), Random.GetUnitVector() * 50.0);
					A.SetRoot(Xf);
					L.PendingRoot = Xf;
				}
			}

			const FLiveLinkStaticDataStruct Static = A.MakeStaticData(&BoneMap, nullptr);
			TArray<FName> LegacyNames = L.BoneNames;
			for (FName& N : LegacyNames) if (const FName* M = BoneMap.Find(N)) N = *M;
			if (!TestTrue(FString::Printf(TEXT("Seed %d frame %d: same bone names"), Seed, Frame), Skel(Static).GetBoneNames() == LegacyNames)) return false;
			if (!TestTrue(FString::Printf(TEXT("Seed %d frame %d: same parents"), Seed, Frame), Skel(Static).GetBoneParents() == L.BoneParents)) return false;
			if (!TestTrue(FString::Printf(TEXT("Seed %d frame %d: same curve names"), Seed, Frame), Skel(Static).PropertyNames == L.CurveNamesOrdered)) return false;

			const FLiveLinkFrameDataStruct Out = A.MakeFrameData(Options);
			TArray<FTransform> LegacyTransforms;
			TArray<float> LegacyValues;
			L.Frame(BoneMap, RefOffsets, true, bPreferIncoming, LegacyTransforms, LegacyValues);

			const TArray<FTransform>& T = Anim(Out).Transforms;
			if (!TestEqual(TEXT("Same transform count"), T.Num(), LegacyTransforms.Num())) return false;
			for (int32 i = 0; i < T.Num(); ++i)
			{
				if (!T[i].Equals(LegacyTransforms[i], 1.0e-5))
				{
					AddError(FString::Printf(TEXT("Seed %d frame %d bone %s: %s vs legacy %s"), Seed, Frame,
						*A.GetBoneNames()[i].ToString(), *T[i].ToString(), *LegacyTransforms[i].ToString()));
					return false;
				}
			}
			if (!TestTrue(FString::Printf(TEXT("Seed %d frame %d: same curve values"), Seed, Frame), Anim(Out).PropertyValues == LegacyValues)) return false;

			A.EndFrame(bZeroMissing);
			L.EndFrame(bZeroMissing);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCProtocolAddressTest, "VMC.Protocol.Addresses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCProtocolAddressTest::RunTest(const FString& Parameters)
{
	using VMCProtocol::EAddress;
	struct FCase { const TCHAR* Address; EAddress Expected; };
	const FCase Cases[] = {
		{ TEXT("/VMC/Ext/Root/Pos"), EAddress::RootPos },
		{ TEXT("/VMC/Ext/Bone/Pos"), EAddress::BonePos },
		{ TEXT("/VMC/Ext/Blend/Val"), EAddress::BlendVal },
		{ TEXT("/VMC/Ext/Blend/Apply"), EAddress::BlendApply },
		{ TEXT("/VMC/Ext/T"), EAddress::Time },
		{ TEXT("/VMC/Ext/OK"), EAddress::Available },
		{ TEXT("/VMC/Ext/Hmd/Pos"), EAddress::DevicePos },
		{ TEXT("/VMC/Ext/Con/Pos"), EAddress::DevicePos },
		{ TEXT("/VMC/Ext/Tra/Pos"), EAddress::DevicePos },
		{ TEXT("/VMC/Ext/Hmd/Pos/Local"), EAddress::DevicePos },
		{ TEXT("/VMC/Ext/Con/Pos/Local"), EAddress::DevicePos },
		{ TEXT("/VMC/Ext/Tra/Pos/Local"), EAddress::DevicePos },
		{ TEXT("/VMC/Ext/Cam"), EAddress::Other },
		{ TEXT("/VMC/Ext/Key"), EAddress::Other },
		{ TEXT("/VMC/Ext/Midi/Note"), EAddress::Other },
		{ TEXT("/VMC/Ext/Bone/Pos/Extra"), EAddress::Other },
		{ TEXT("/vmc/ext/bone/pos"), EAddress::Other }, // OSC addresses are case-sensitive
		{ TEXT("/VMC/Ext/"), EAddress::Other },
		{ TEXT("/avatar/parameters/Foo"), EAddress::Other },
		{ TEXT(""), EAddress::Other },
	};
	for (const FCase& Case : Cases)
	{
		TestEqual(FString::Printf(TEXT("'%s'"), Case.Address), (int32)VMCProtocol::ClassifyAddress(Case.Address), (int32)Case.Expected);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
