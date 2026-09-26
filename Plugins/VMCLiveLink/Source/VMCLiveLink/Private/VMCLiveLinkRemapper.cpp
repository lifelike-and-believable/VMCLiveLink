// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VMCLiveLinkRemapper.h"
#include "VMCLog.h"
#include "VMCLiveLinkSettings.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include <Remapper/LiveLinkSkeletonRemapper.h>

#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"

// Presets that seed map entries (None and Custom seed nothing).
static const ELLRemapPreset AllSeedPresets[] = {
	ELLRemapPreset::ARKit, ELLRemapPreset::VMC_VRM, ELLRemapPreset::VMC_VRM1, ELLRemapPreset::VRoid, ELLRemapPreset::Rokoko
};

static void GetBoneNames(TSoftObjectPtr<USkeletalMesh> Mesh, TArray<FName>& Out)
{
	Out.Reset();
	if (!Mesh) return;

	const FReferenceSkeleton& RefSkel = Mesh->GetRefSkeleton();
	const int32 NumBones = RefSkel.GetNum();
	Out.Reserve(NumBones);
	for (int32 i = 0; i < NumBones; ++i)
	{
		Out.Add(RefSkel.GetBoneName(i));
	}
}

USkeletalMesh* UVMCLiveLinkRemapper::ResolveReferenceSkeleton() const
{
	if (USkeletalMesh* Mesh = ReferenceSkeleton.LoadSynchronous())
	{
		return Mesh;
	}
	const UVMCLiveLinkSettings* Project = GetDefault<UVMCLiveLinkSettings>();
	return Project ? Project->DefaultReferenceSkeleton.LoadSynchronous() : nullptr;
}

ULiveLinkSubjectRemapper::FWorkerSharedPtr UVMCLiveLinkRemapper::CreateWorker()
{
	FVMCRemapConfig Config;
	Config.BoneNameMap = BoneNameMap;     // base class map
	Config.CurveNameMap = CurveNameMap;
	Config.bUseRefTranslations = bUseReferenceTranslations;
	Config.bEnableMetaHumanCurveNormalizer = bEnableMetaHumanCurveNormalizer;
	Config.JoyToSmileStrength = JoyToSmileStrength;
	Config.BlinkMirrorStrength = BlinkMirrorStrength;
	if (bUseReferenceTranslations)
	{
		if (const USkeletalMesh* Ref = ResolveReferenceSkeleton())
		{
			const FReferenceSkeleton& RefSkel = Ref->GetRefSkeleton();
			const TArray<FTransform>& RefPose = RefSkel.GetRefBonePose(); // local (parent space)
			for (int32 i = 0; i < RefSkel.GetNum(); ++i)
			{
				Config.RefTranslations.Add(RefSkel.GetBoneName(i), RefPose[i].GetTranslation());
			}
		}
	}
	Worker = MakeShared<FVMCLiveLinkRemapperWorker>(MoveTemp(Config));
	return Worker;
}

void UVMCLiveLinkRemapper::Initialize(const FLiveLinkSubjectKey& InSubjectKey)
{
	CachedKey = InSubjectKey;

	// No guessing here: Initialize runs whenever the subject is (re)created, and must leave the
	// user's maps and preset as they are. Presets are applied by ApplyPreset or
	// DetectAndSeedFromSubject. The one automatic step is picking a mapping asset for the
	// reference skeleton, and only while the maps are still empty.
	if (bAutoDetectMappingFromReference && BoneNameMap.Num() == 0 && CurveNameMap.Num() == 0)
	{
		SeedFromReferenceSkeleton();
	}
	MarkDirty();
}

void UVMCLiveLinkRemapper::MarkDirty()
{
	bDirty = true;
	++Revision;
}

void UVMCLiveLinkRemapper::DetectAndSeedFromSubject()
{
	if (!IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName)) return;

	ILiveLinkClient& Client = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
	if (const FLiveLinkStaticDataStruct* SDS = Client.GetSubjectStaticData_AnyThread(CachedKey))
	{
		if (SDS->IsValid() && SDS->GetStruct()->IsChildOf<FLiveLinkSkeletonStaticData>())
		{
			const auto& Skel = *SDS->Cast<FLiveLinkSkeletonStaticData>();
			const FLiveLinkBaseStaticData& Base = static_cast<const FLiveLinkBaseStaticData&>(Skel);
			// List every incoming name (as itself) so it can be edited, then apply the best preset.
			for (const FName& N : Skel.GetBoneNames()) if (!BoneNameMap.Contains(N)) BoneNameMap.Add(N, N);
			for (const FName& N : Base.PropertyNames) if (!CurveNameMap.Contains(N)) CurveNameMap.Add(N, N);
			Preset = GuessPreset(Skel.GetBoneNames(), Base.PropertyNames);
			ApplyPreset(Preset);
		}
	}
}

void UVMCLiveLinkRemapper::ApplyPreset(ELLRemapPreset InPreset)
{
	// Remove entries that a preset added and the user has not changed since, so switching
	// presets does not accumulate stale mappings. Entries the user edited are kept.
	for (const ELLRemapPreset Other : AllSeedPresets)
	{
		TMap<FName, FName> OtherBones, OtherCurves;
		GetPresetMaps(Other, OtherBones, OtherCurves);
		RemoveUnchangedEntries(BoneNameMap, OtherBones);
		RemoveUnchangedEntries(CurveNameMap, OtherCurves);
	}

	TMap<FName, FName> PresetBones, PresetCurves;
	GetPresetMaps(InPreset, PresetBones, PresetCurves);
	BoneNameMap.Append(PresetBones);
	CurveNameMap.Append(PresetCurves);

	// If we have subject data, nudge humanoid bone names toward the reference mesh
	if (IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		ILiveLinkClient& Client = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
		if (const FLiveLinkStaticDataStruct* SDS = Client.GetSubjectStaticData_AnyThread(CachedKey))
		{
			if (SDS->IsValid() && SDS->GetStruct()->IsChildOf<FLiveLinkSkeletonStaticData>())
			{
				SeedBones_FromHumanoidLike(SDS->Cast<FLiveLinkSkeletonStaticData>()->GetBoneNames());
			}
		}
	}

	MarkDirty();
}

void UVMCLiveLinkRemapper::LoadCustomCurveMapFromJSON(const FString& JsonText)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
	{
		if (const TSharedPtr<FJsonObject>* CurvesObj; Root->TryGetObjectField(TEXT("Curves"), CurvesObj))
		{
			for (const auto& KV : (*CurvesObj)->Values)
			{
				if (KV.Value.IsValid() && KV.Value->Type == EJson::String)
				{
					CurveNameMap.Add(FName(*KV.Key), FName(*KV.Value->AsString()));
				}
			}
		}
		if (const TSharedPtr<FJsonObject>* BonesObj; Root->TryGetObjectField(TEXT("Bones"), BonesObj))
		{
			for (const auto& KV : (*BonesObj)->Values)
			{
				if (KV.Value.IsValid() && KV.Value->Type == EJson::String)
				{
					BoneNameMap.Add(FName(*KV.Key), FName(*KV.Value->AsString()));
				}
			}
		}
	}
	MarkDirty();
}

// -------------- Worker --------------

void FVMCLiveLinkRemapperWorker::RemapStaticData(FLiveLinkStaticDataStruct& InOutStaticData)
{
	if (!InOutStaticData.IsValid() ||
		!InOutStaticData.GetStruct()->IsChildOf<FLiveLinkSkeletonStaticData>()) return;

	auto& Skel = *InOutStaticData.Cast<FLiveLinkSkeletonStaticData>();

	// Bones (use accessors for safety). Before renaming, note each bone's rest translation in the
	// target skeleton, for RemapFrameData.
	TArray<FName> Remapped = Skel.GetBoneNames();
	const TArray<int32>& Parents = Skel.GetBoneParents();
	RestTranslations.SetNum(Remapped.Num());
	HasRestTranslation.Init(false, Remapped.Num());
	static const FName HipsName(TEXT("Hips"));
	for (int32 i = 0; i < Remapped.Num(); ++i)
	{
		const bool bCarriesMotion = !Parents.IsValidIndex(i) || Parents[i] == INDEX_NONE || Remapped[i] == HipsName;
		if (const FName* Out = Config.BoneNameMap.Find(Remapped[i])) Remapped[i] = *Out;
		if (Config.bUseRefTranslations && !bCarriesMotion)
		{
			if (const FVector* Rest = Config.RefTranslations.Find(Remapped[i]))
			{
				RestTranslations[i] = *Rest;
				HasRestTranslation[i] = true;
			}
		}
	}
	Skel.SetBoneNames(Remapped);

	// Curves live on base static data in 5.6
	FLiveLinkBaseStaticData& Base = static_cast<FLiveLinkBaseStaticData&>(Skel);
	for (FName& C : Base.PropertyNames)
	{
		if (const FName* Out = Config.CurveNameMap.Find(C)) C = *Out;
	}

	if (!bWarnedDuplicateCurves)
	{
		TSet<FName> Seen;
		TArray<FString> Duplicates;
		for (const FName& C : Base.PropertyNames)
		{
			bool bAlreadySeen = false;
			Seen.Add(C, &bAlreadySeen);
			if (bAlreadySeen)
			{
				Duplicates.AddUnique(C.ToString());
			}
		}
		if (Duplicates.Num() > 0)
		{
			UE_LOG(LogVMCLiveLink, Warning, TEXT("VMC remapper: several incoming curves map to the same name (%s). Only one value per name is usable; edit the Curve Name Map so each target has one source."),
				*FString::Join(Duplicates, TEXT(", ")));
			bWarnedDuplicateCurves = true;
		}
	}

	// Normalizer: add missing counterparts at the end of the property list. Incoming curves are
	// never modified.
	SynthesizedCurves.Reset();
	IncomingPropertyCount = Base.PropertyNames.Num();
	if (!Config.bEnableMetaHumanCurveNormalizer)
	{
		return;
	}

	auto IndexOf = [&Base](const TCHAR* Name) { return Base.PropertyNames.IndexOfByKey(FName(Name)); };
	auto AddCounterpart = [&](const TCHAR* A, const TCHAR* B, float Scale)
	{
		const int32 IndexA = IndexOf(A);
		const int32 IndexB = IndexOf(B);
		if (IndexA != INDEX_NONE && IndexB == INDEX_NONE)
		{
			SynthesizedCurves.Add({ IndexA, Scale });
			Base.PropertyNames.Add(FName(B));
		}
		else if (IndexB != INDEX_NONE && IndexA == INDEX_NONE)
		{
			SynthesizedCurves.Add({ IndexB, Scale });
			Base.PropertyNames.Add(FName(A));
		}
	};

	AddCounterpart(TEXT("eyeBlinkLeft"), TEXT("eyeBlinkRight"), Config.BlinkMirrorStrength);
	AddCounterpart(TEXT("mouthSmileLeft"), TEXT("mouthSmileRight"), Config.JoyToSmileStrength);

	const int32 FunnelIndex = IndexOf(TEXT("mouthFunnel"));
	if (FunnelIndex != INDEX_NONE && IndexOf(TEXT("mouthPucker")) == INDEX_NONE)
	{
		SynthesizedCurves.Add({ FunnelIndex, 0.5f });
		Base.PropertyNames.Add(FName(TEXT("mouthPucker")));
	}
}

void FVMCLiveLinkRemapperWorker::RemapFrameData(const FLiveLinkStaticDataStruct& InStatic, FLiveLinkFrameDataStruct& InOutFrameData)
{
	if (!InOutFrameData.IsValid() || !InOutFrameData.GetStruct()->IsChildOf<FLiveLinkAnimationFrameData>()) return;
	FLiveLinkAnimationFrameData& Anim = *InOutFrameData.Cast<FLiveLinkAnimationFrameData>();

	// Rest translations for bones the stream sent without one (VMC sends most bones as rotation
	// only). Only for frames built against the static data this worker saw.
	if (HasRestTranslation.Num() == Anim.Transforms.Num())
	{
		for (TConstSetBitIterator<> It(HasRestTranslation); It; ++It)
		{
			FTransform& Bone = Anim.Transforms[It.GetIndex()];
			if (Bone.GetTranslation().IsNearlyZero())
			{
				Bone.SetTranslation(RestTranslations[It.GetIndex()]);
			}
		}
	}

	if (SynthesizedCurves.Num() == 0) return;
	TArray<float>& Values = Anim.PropertyValues;

	// Only extend frames that carry exactly the incoming properties this worker saw at static
	// time; anything else was built against different static data.
	if (Values.Num() != IncomingPropertyCount) return;

	Values.Reserve(Values.Num() + SynthesizedCurves.Num());
	for (const FSynthesizedCurve& Synth : SynthesizedCurves)
	{
		const float Value = Values.IsValidIndex(Synth.SourceIndex)
			? FMath::Clamp(Values[Synth.SourceIndex] * Synth.Scale, 0.f, 1.f)
			: 0.f;
		Values.Add(Value);
	}
}

// -------------- Seeding --------------

namespace VMCRemapPresets
{
	template <int32 N>
	static void AddIdentity(TMap<FName, FName>& Map, const TCHAR* const (&Names)[N])
	{
		for (const TCHAR* Name : Names)
		{
			Map.Add(FName(Name), FName(Name));
		}
	}

	static void AddPairs(TMap<FName, FName>& Map, std::initializer_list<TPair<const TCHAR*, const TCHAR*>> Pairs)
	{
		for (const TPair<const TCHAR*, const TCHAR*>& Pair : Pairs)
		{
			Map.Add(FName(Pair.Key), FName(Pair.Value));
		}
	}

	static const TCHAR* const ARKitCurves[] = {
		TEXT("browDownLeft"), TEXT("browDownRight"), TEXT("browInnerUp"),
		TEXT("browOuterUpLeft"), TEXT("browOuterUpRight"),
		TEXT("cheekPuff"), TEXT("cheekSquintLeft"), TEXT("cheekSquintRight"),
		TEXT("eyeBlinkLeft"), TEXT("eyeBlinkRight"),
		TEXT("eyeLookDownLeft"), TEXT("eyeLookDownRight"),
		TEXT("eyeLookInLeft"), TEXT("eyeLookInRight"),
		TEXT("eyeLookOutLeft"), TEXT("eyeLookOutRight"),
		TEXT("eyeLookUpLeft"), TEXT("eyeLookUpRight"),
		TEXT("eyeSquintLeft"), TEXT("eyeSquintRight"),
		TEXT("eyeWideLeft"), TEXT("eyeWideRight"),
		TEXT("jawForward"), TEXT("jawLeft"), TEXT("jawOpen"), TEXT("jawRight"),
		TEXT("mouthClose"), TEXT("mouthDimpleLeft"), TEXT("mouthDimpleRight"),
		TEXT("mouthFrownLeft"), TEXT("mouthFrownRight"),
		TEXT("mouthFunnel"), TEXT("mouthLeft"), TEXT("mouthLowerDownLeft"),
		TEXT("mouthLowerDownRight"), TEXT("mouthPressLeft"), TEXT("mouthPressRight"),
		TEXT("mouthPucker"), TEXT("mouthRight"), TEXT("mouthRollLower"),
		TEXT("mouthRollUpper"), TEXT("mouthShrugLower"), TEXT("mouthShrugUpper"),
		TEXT("mouthSmileLeft"), TEXT("mouthSmileRight"),
		TEXT("mouthStretchLeft"), TEXT("mouthStretchRight"),
		TEXT("mouthUpperUpLeft"), TEXT("mouthUpperUpRight"),
		TEXT("noseSneerLeft"), TEXT("noseSneerRight"),
		TEXT("tongueOut")
	};
}

void UVMCLiveLinkRemapper::GetPresetMaps(ELLRemapPreset InPreset, TMap<FName, FName>& OutBones, TMap<FName, FName>& OutCurves)
{
	using namespace VMCRemapPresets;
	using FPair = TPair<const TCHAR*, const TCHAR*>;

	OutBones.Reset();
	OutCurves.Reset();

	// Each preset maps every target from at most one source, so the published curve list never
	// contains the same name twice. Expressions without a clear ARKit equivalent are left unmapped
	// (they pass through under their original names).
	switch (InPreset)
	{
	case ELLRemapPreset::ARKit:
		AddIdentity(OutCurves, ARKitCurves);
		break;

	case ELLRemapPreset::Rokoko:
		// Rokoko forwards ARKit names; add aliases for its short-form smile names.
		AddIdentity(OutCurves, ARKitCurves);
		AddPairs(OutCurves, {
			FPair(TEXT("mouthSmile_L"), TEXT("mouthSmileLeft")),
			FPair(TEXT("mouthSmile_R"), TEXT("mouthSmileRight")),
		});
		// The aliases share targets with the identity entries; only one of each pair is expected
		// in a given stream.
		break;

	case ELLRemapPreset::VMC_VRM:
		// VRM 0.x blend shape preset names -> ARKit-style targets.
		// Unmapped: Blink (both eyes; use Blink_L/Blink_R), I and E (no single ARKit shape),
		// Angry (BrowDownLeft already maps to browDownLeft), Surprised, Neutral.
		AddPairs(OutCurves, {
			FPair(TEXT("Blink_L"),       TEXT("eyeBlinkLeft")),
			FPair(TEXT("Blink_R"),       TEXT("eyeBlinkRight")),
			FPair(TEXT("Joy"),           TEXT("mouthSmileLeft")),
			FPair(TEXT("Sorrow"),        TEXT("mouthFrownLeft")),
			FPair(TEXT("Fun"),           TEXT("cheekPuff")),
			FPair(TEXT("A"),             TEXT("jawOpen")),
			FPair(TEXT("U"),             TEXT("mouthPucker")),
			FPair(TEXT("O"),             TEXT("mouthFunnel")),
			FPair(TEXT("BrowDownLeft"),  TEXT("browDownLeft")),
			FPair(TEXT("BrowDownRight"), TEXT("browDownRight")),
			FPair(TEXT("BrowUpLeft"),    TEXT("browOuterUpLeft")),
			FPair(TEXT("BrowUpRight"),   TEXT("browOuterUpRight")),
		});
		break;

	case ELLRemapPreset::VMC_VRM1:
		// VRM 1.0 expression preset names -> ARKit-style targets.
		// Unmapped: blink (both eyes; use blinkLeft/blinkRight), ih and ee (no single ARKit shape),
		// angry, relaxed, surprised, neutral, look*.
		AddPairs(OutCurves, {
			FPair(TEXT("blinkLeft"),  TEXT("eyeBlinkLeft")),
			FPair(TEXT("blinkRight"), TEXT("eyeBlinkRight")),
			FPair(TEXT("happy"),      TEXT("mouthSmileLeft")),
			FPair(TEXT("sad"),        TEXT("mouthFrownLeft")),
			FPair(TEXT("aa"),         TEXT("jawOpen")),
			FPair(TEXT("ou"),         TEXT("mouthPucker")),
			FPair(TEXT("oh"),         TEXT("mouthFunnel")),
		});
		break;

	case ELLRemapPreset::VRoid:
		// VMC humanoid bone names -> VRoid Studio skeleton names.
		AddPairs(OutBones, {
			FPair(TEXT("Hips"),          TEXT("J_Bip_C_Hips")),
			FPair(TEXT("Spine"),         TEXT("J_Bip_C_Spine")),
			FPair(TEXT("Chest"),         TEXT("J_Bip_C_Chest")),
			FPair(TEXT("UpperChest"),    TEXT("J_Bip_C_UpperChest")),
			FPair(TEXT("Neck"),          TEXT("J_Bip_C_Neck")),
			FPair(TEXT("Head"),          TEXT("J_Bip_C_Head")),
			FPair(TEXT("LeftEye"),       TEXT("J_Adj_L_FaceEye")),
			FPair(TEXT("RightEye"),      TEXT("J_Adj_R_FaceEye")),
			FPair(TEXT("LeftUpperLeg"),  TEXT("J_Bip_L_UpperLeg")),
			FPair(TEXT("RightUpperLeg"), TEXT("J_Bip_R_UpperLeg")),
			FPair(TEXT("LeftLowerLeg"),  TEXT("J_Bip_L_LowerLeg")),
			FPair(TEXT("RightLowerLeg"), TEXT("J_Bip_R_LowerLeg")),
			FPair(TEXT("LeftFoot"),      TEXT("J_Bip_L_Foot")),
			FPair(TEXT("RightFoot"),     TEXT("J_Bip_R_Foot")),
			FPair(TEXT("LeftToes"),      TEXT("J_Bip_L_Toes")),
			FPair(TEXT("RightToes"),     TEXT("J_Bip_R_Toes")),
			FPair(TEXT("LeftShoulder"),  TEXT("J_Bip_L_Shoulder")),
			FPair(TEXT("RightShoulder"), TEXT("J_Bip_R_Shoulder")),
			FPair(TEXT("LeftUpperArm"),  TEXT("J_Bip_L_UpperArm")),
			FPair(TEXT("RightUpperArm"), TEXT("J_Bip_R_UpperArm")),
			FPair(TEXT("LeftLowerArm"),  TEXT("J_Bip_L_LowerArm")),
			FPair(TEXT("RightLowerArm"), TEXT("J_Bip_R_LowerArm")),
			FPair(TEXT("LeftHand"),      TEXT("J_Bip_L_Hand")),
			FPair(TEXT("RightHand"),     TEXT("J_Bip_R_Hand")),
		});
		{
			// Fingers: <Side><Finger><Proximal|Intermediate|Distal> -> J_Bip_<S>_<Finger><1|2|3>
			static const TCHAR* const Sides[] = { TEXT("Left"), TEXT("Right") };
			static const TCHAR* const ShortSides[] = { TEXT("L"), TEXT("R") };
			static const TCHAR* const Fingers[] = { TEXT("Thumb"), TEXT("Index"), TEXT("Middle"), TEXT("Ring"), TEXT("Little") };
			static const TCHAR* const Segments[] = { TEXT("Proximal"), TEXT("Intermediate"), TEXT("Distal") };
			for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
			{
				const TCHAR* Side = Sides[SideIndex];
				const TCHAR* ShortSide = ShortSides[SideIndex];
				for (const TCHAR* Finger : Fingers)
				{
					for (int32 Seg = 0; Seg < 3; ++Seg)
					{
						OutBones.Add(
							FName(*FString::Printf(TEXT("%s%s%s"), Side, Finger, Segments[Seg])),
							FName(*FString::Printf(TEXT("J_Bip_%s_%s%d"), ShortSide, Finger, Seg + 1)));
					}
				}
			}
		}
		AddPairs(OutCurves, {
			FPair(TEXT("Blink"),     TEXT("Fcl_EYE_Close")),
			FPair(TEXT("Blink_L"),   TEXT("Fcl_EYE_Close_L")),
			FPair(TEXT("Blink_R"),   TEXT("Fcl_EYE_Close_R")),
			FPair(TEXT("Joy"),       TEXT("Fcl_ALL_Joy")),
			FPair(TEXT("Angry"),     TEXT("Fcl_ALL_Angry")),
			FPair(TEXT("Sorrow"),    TEXT("Fcl_ALL_Sorrow")),
			FPair(TEXT("Fun"),       TEXT("Fcl_ALL_Fun")),
			FPair(TEXT("Surprised"), TEXT("Fcl_ALL_Surprised")),
			FPair(TEXT("A"),         TEXT("Fcl_MTH_A")),
			FPair(TEXT("I"),         TEXT("Fcl_MTH_I")),
			FPair(TEXT("U"),         TEXT("Fcl_MTH_U")),
			FPair(TEXT("E"),         TEXT("Fcl_MTH_E")),
			FPair(TEXT("O"),         TEXT("Fcl_MTH_O")),
		});
		break;

	default:
		break;
	}
}

void UVMCLiveLinkRemapper::RemoveUnchangedEntries(TMap<FName, FName>& InOutMap, const TMap<FName, FName>& PresetEntries)
{
	for (const TPair<FName, FName>& Entry : PresetEntries)
	{
		if (const FName* Current = InOutMap.Find(Entry.Key); Current && *Current == Entry.Value)
		{
			InOutMap.Remove(Entry.Key);
		}
	}
}

void UVMCLiveLinkRemapper::SeedBones_FromHumanoidLike(const TArray<FName>& Incoming)
{
	USkeletalMesh* Ref = ReferenceSkeleton.LoadSynchronous();
	if (!Ref) return;

	auto Normalize = [](FString S)
		{
			S = S.ToLower();
			S.ReplaceInline(TEXT("_"), TEXT(""));
			S.ReplaceInline(TEXT("-"), TEXT(""));
			return S;
		};

	const FReferenceSkeleton& RefSkel = Ref->GetRefSkeleton();
	TMap<FString, FName> RefByNorm;
	for (int32 i = 0; i < RefSkel.GetNum(); ++i)
	{
		const FName B = RefSkel.GetBoneName(i);
		RefByNorm.Add(Normalize(B.ToString()), B);
	}

	auto TryMap = [&](FName Src, const TArray<FString>& Candidates)
		{
			for (const FString& C : Candidates)
			{
				if (const FName* Found = RefByNorm.Find(Normalize(C)))
				{
					BoneNameMap.Add(Src, *Found);
					return true;
				}
			}
			return false;
		};

	for (FName Src : Incoming)
	{
		const FString N = Src.ToString();

		if (N.Equals(TEXT("Hips"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("pelvis") }); continue; }
		if (N.Equals(TEXT("Spine"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("spine_01"), TEXT("spine01"), TEXT("spine") }); continue; }
		if (N.Equals(TEXT("Chest"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("spine_02"), TEXT("spine02") }); continue; }
		if (N.Equals(TEXT("UpperChest"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("spine_03"), TEXT("spine03") }); continue; }
		if (N.Equals(TEXT("Neck"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("neck_01"), TEXT("neck") }); continue; }
		if (N.Equals(TEXT("Head"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("head") }); continue; }

		// Arms
		if (N.Contains(TEXT("LeftUpperArm"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("upperarm_l") }); continue; }
		if (N.Contains(TEXT("LeftLowerArm"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("lowerarm_l"), TEXT("forearm_l") }); continue; }
		if (N.Contains(TEXT("LeftHand"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("hand_l") }); continue; }

		if (N.Contains(TEXT("RightUpperArm"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("upperarm_r") }); continue; }
		if (N.Contains(TEXT("RightLowerArm"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("lowerarm_r"), TEXT("forearm_r") }); continue; }
		if (N.Contains(TEXT("RightHand"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("hand_r") }); continue; }

		// Legs
		if (N.Contains(TEXT("LeftUpperLeg"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("thigh_l") }); continue; }
		if (N.Contains(TEXT("LeftLowerLeg"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("calf_l") }); continue; }
		if (N.Contains(TEXT("LeftFoot"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("foot_l") }); continue; }

		if (N.Contains(TEXT("RightUpperLeg"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("thigh_r") }); continue; }
		if (N.Contains(TEXT("RightLowerLeg"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("calf_r") }); continue; }
		if (N.Contains(TEXT("RightFoot"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("foot_r") }); continue; }
	}
}

void UVMCLiveLinkRemapper::SeedFromReferenceSkeleton()
{
	USkeletalMesh* Ref = ReferenceSkeleton.LoadSynchronous();
	if (!Ref) return;

	// If the user picked a specific asset, prefer it
	if (UVMCLiveLinkMappingAsset* Explicit = MappingAsset.LoadSynchronous())
	{
		if (Explicit->MatchesMesh(Ref))
		{
			ApplyMappingAsset(Explicit, /*bAlsoCaptureSignature=*/false);
			return;
		}
	}

	// Otherwise look for one, without blocking
	if (bAutoDetectMappingFromReference)
	{
		StartAutoDetectMapping(/*bOnlyIfMapsEmpty=*/true);
	}
}

void UVMCLiveLinkRemapper::ApplyMappingAsset(UVMCLiveLinkMappingAsset* Asset, bool bAlsoCaptureSignature)
{
	if (!Asset) return;

	// Reflect the applied asset in the UI
	MappingAsset = Asset;
	BoneNameMap = Asset->BoneNameMap;
	CurveNameMap = Asset->CurveNameMap;

	if (bAlsoCaptureSignature)
	{
		if (USkeletalMesh* Ref = ReferenceSkeleton.LoadSynchronous())
		{
			Asset->CaptureSignatureFrom(Ref);
		}
	}

	Preset = ELLRemapPreset::Custom;
	MarkDirty();
}

namespace
{
	FString NormalizeForMatch(FString S)
	{
		S = S.ToLower();
		S.ReplaceInline(TEXT("_"), TEXT(""));
		S.ReplaceInline(TEXT("-"), TEXT(""));
		return S;
	}
}

void UVMCLiveLinkRemapper::FindMappingCandidates(uint32 Signature, TArray<FSoftObjectPath>& OutLikely, TArray<FSoftObjectPath>& OutAll)
{
	OutLikely.Reset();
	OutAll.Reset();
	IAssetRegistry* Registry = IAssetRegistry::Get();
	if (!Registry)
	{
		return;
	}
	TArray<FAssetData> Assets;
	Registry->GetAssetsByClass(UVMCLiveLinkMappingAsset::StaticClass()->GetClassPathName(), Assets, /*bSearchSubClasses*/ true);

	const FString Wanted = FString::Printf(TEXT(";%s;"), *UVMCLiveLinkMappingAsset::SignatureToTag(Signature));
	TArray<FSoftObjectPath> OldTags;
	for (const FAssetData& Asset : Assets)
	{
		FString Tag;
		int32 Version = 0;
		Asset.GetTagValue(GET_MEMBER_NAME_CHECKED(UVMCLiveLinkMappingAsset, SignatureTag), Tag);
		Asset.GetTagValue(GET_MEMBER_NAME_CHECKED(UVMCLiveLinkMappingAsset, SignatureVersion), Version);
		if (Version >= UVMCLiveLinkMappingAsset::CurrentSignatureVersion)
		{
			if (Tag.Contains(Wanted))
			{
				OutLikely.Add(Asset.GetSoftObjectPath());
			}
		}
		else
		{
			OldTags.Add(Asset.GetSoftObjectPath()); // must be loaded to know
		}
		OutAll.Add(Asset.GetSoftObjectPath());
	}
	OutLikely.Append(OldTags);
}

UVMCLiveLinkMappingAsset* UVMCLiveLinkRemapper::ChooseMapping(USkeletalMesh* Ref, TConstArrayView<FSoftObjectPath> Candidates, bool bAllowHeuristic)
{
	if (!Ref) return nullptr;

	// 1) A signature or example-mesh match
	for (const FSoftObjectPath& Path : Candidates)
	{
		if (UVMCLiveLinkMappingAsset* Mapping = Cast<UVMCLiveLinkMappingAsset>(Path.ResolveObject()))
		{
			if (Mapping->MatchesMesh(Ref))
			{
				return Mapping;
			}
		}
	}
	if (!bAllowHeuristic)
	{
		return nullptr;
	}

	// 2) Best effort: the most target bone names found in the reference skeleton
	TSet<FString> RefNorm;
	const FReferenceSkeleton& RS = Ref->GetRefSkeleton();
	for (int32 i = 0; i < RS.GetNum(); ++i)
	{
		RefNorm.Add(NormalizeForMatch(RS.GetBoneName(i).ToString()));
	}
	int32 BestScore = 0;
	UVMCLiveLinkMappingAsset* Best = nullptr;
	for (const FSoftObjectPath& Path : Candidates)
	{
		UVMCLiveLinkMappingAsset* Mapping = Cast<UVMCLiveLinkMappingAsset>(Path.ResolveObject());
		if (!Mapping) continue;
		int32 Score = 0;
		for (const TPair<FName, FName>& KV : Mapping->BoneNameMap)
		{
			if (RefNorm.Contains(NormalizeForMatch(KV.Value.ToString()))) ++Score;
		}
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Mapping;
		}
	}
	return Best;
}

bool UVMCLiveLinkRemapper::AutoDetectAndApplyMapping()
{
	USkeletalMesh* Ref = ReferenceSkeleton.LoadSynchronous();
	if (!Ref) return false;

	TArray<FSoftObjectPath> Likely, All;
	FindMappingCandidates(UVMCLiveLinkMappingAsset::ComputeSignature(Ref), Likely, All);
	for (const FSoftObjectPath& Path : Likely) Path.TryLoad();
	UVMCLiveLinkMappingAsset* Chosen = ChooseMapping(Ref, Likely, /*bAllowHeuristic*/ false);
	if (!Chosen)
	{
		for (const FSoftObjectPath& Path : All) Path.TryLoad();
		Chosen = ChooseMapping(Ref, All, /*bAllowHeuristic*/ true);
	}
	if (Chosen)
	{
		ApplyMappingAsset(Chosen, /*bAlsoCaptureSignature=*/false);
		return true;
	}
	return false;
}

bool UVMCLiveLinkRemapper::StartAutoDetectMapping(bool bOnlyIfMapsEmpty)
{
	USkeletalMesh* Ref = ReferenceSkeleton.LoadSynchronous();
	if (!Ref) return false;

	TArray<FSoftObjectPath> Likely, All;
	FindMappingCandidates(UVMCLiveLinkMappingAsset::ComputeSignature(Ref), Likely, All);
	if (All.Num() == 0)
	{
		return true; // no mapping assets at all
	}
	if (!UAssetManager::IsInitialized())
	{
		// No streaming (a commandlet, say): do it now.
		if (!bOnlyIfMapsEmpty || (BoneNameMap.Num() == 0 && CurveNameMap.Num() == 0))
		{
			AutoDetectAndApplyMapping();
		}
		return true;
	}
	OnAutoDetectLoaded(MoveTemp(Likely), MoveTemp(All), /*bSecondPass*/ false, bOnlyIfMapsEmpty, /*bLoadsDone*/ false);
	return true;
}

void UVMCLiveLinkRemapper::OnAutoDetectLoaded(TArray<FSoftObjectPath> Likely, TArray<FSoftObjectPath> All, bool bSecondPass, bool bOnlyIfMapsEmpty, bool bLoadsDone)
{
	// Load the pass's candidates first (the likely ones, then everything), then choose on the game thread.
	TArray<FSoftObjectPath>& ToLoad = bSecondPass ? All : Likely;
	TArray<FSoftObjectPath> Pending;
	if (!bLoadsDone)
	{
		for (const FSoftObjectPath& Path : ToLoad)
		{
			if (!Path.ResolveObject()) Pending.Add(Path);
		}
	}
	if (Pending.Num() > 0)
	{
		TWeakObjectPtr<UVMCLiveLinkRemapper> WeakThis(this);
		UAssetManager::GetStreamableManager().RequestAsyncLoad(MoveTemp(Pending),
			FStreamableDelegate::CreateLambda([WeakThis, Likely, All, bSecondPass, bOnlyIfMapsEmpty]()
			{
				if (UVMCLiveLinkRemapper* This = WeakThis.Get())
				{
					// Loaded (or failed to load, which ChooseMapping skips): choose.
					This->OnAutoDetectLoaded(Likely, All, bSecondPass, bOnlyIfMapsEmpty, /*bLoadsDone*/ true);
				}
			}));
		return;
	}

	if (bOnlyIfMapsEmpty && (BoneNameMap.Num() > 0 || CurveNameMap.Num() > 0))
	{
		return; // filled in the meantime (by the user, or another pass)
	}
	USkeletalMesh* Ref = ReferenceSkeleton.Get();
	if (UVMCLiveLinkMappingAsset* Chosen = ChooseMapping(Ref, ToLoad, /*bAllowHeuristic*/ bSecondPass))
	{
		ApplyMappingAsset(Chosen, /*bAlsoCaptureSignature=*/false);
		UE_LOG(LogVMCLiveLink, Log, TEXT("VMC remapper: applied mapping asset '%s' for '%s'."), *Chosen->GetPathName(), Ref ? *Ref->GetName() : TEXT("?"));
	}
	else if (!bSecondPass)
	{
		OnAutoDetectLoaded(MoveTemp(Likely), MoveTemp(All), /*bSecondPass*/ true, bOnlyIfMapsEmpty, /*bLoadsDone*/ false);
	}
}

void UVMCLiveLinkRemapper::SaveCurrentMappingTo(UVMCLiveLinkMappingAsset* Asset, bool bCaptureSignatureFromReference)
{
	if (!Asset) return;

	Asset->Modify();
	Asset->BoneNameMap = BoneNameMap;
	Asset->CurveNameMap = CurveNameMap;
	if (bCaptureSignatureFromReference)
	{
		if (USkeletalMesh* Ref = ReferenceSkeleton.LoadSynchronous())
		{
			Asset->CaptureSignatureFrom(Ref);
		}
	}
}

ELLRemapPreset UVMCLiveLinkRemapper::GuessPreset(const TArray<FName>& BoneNames, const TArray<FName>& CurveNames) const
{
	
	TArray<FName> RefSkelBoneNames;
	GetBoneNames(ReferenceSkeleton, RefSkelBoneNames);

	for (const FName& N : RefSkelBoneNames)
	{
		const FString S = N.ToString();
		if (S.StartsWith(TEXT("J_Bip_"))) return ELLRemapPreset::VRoid;
	}

	int32 ARKitHits = 0;
	for (const FName& N : CurveNames)
	{
		const FString S = N.ToString();
		if (S.StartsWith(TEXT("eye")) || S.StartsWith(TEXT("mouth")) || S.StartsWith(TEXT("brow")) || S == TEXT("tongueOut") || S.StartsWith(TEXT("jaw")))
			++ARKitHits;
	}
	if (ARKitHits >= 20) return ELLRemapPreset::ARKit;

	bool HasVRM1Visemes = false, HasVRM1Blink = false, HasVRM1Emotes = false;
	for (const FName& N : CurveNames)
	{
		const FString S = N.ToString();
		if (S == TEXT("aa") || S == TEXT("ih") || S == TEXT("ou") || S == TEXT("ee") || S == TEXT("oh")) HasVRM1Visemes = true;
		if (S == TEXT("blinkLeft") || S == TEXT("blinkRight")) HasVRM1Blink = true;
		if (S == TEXT("happy") || S == TEXT("angry") || S == TEXT("sad") || S == TEXT("relaxed")) HasVRM1Emotes = true;
	}
	if ((HasVRM1Visemes && HasVRM1Blink) || (HasVRM1Visemes && HasVRM1Emotes)) return ELLRemapPreset::VMC_VRM1;

	bool HasVisemes = false, HasBlinkLR = false, HasEmotes = false;
	for (const FName& N : CurveNames)
	{
		const FString S = N.ToString();
		if (S == TEXT("A") || S == TEXT("I") || S == TEXT("U") || S == TEXT("E") || S == TEXT("O")) HasVisemes = true;
		if (S == TEXT("Blink_L") || S == TEXT("Blink_R")) HasBlinkLR = true;
		if (S == TEXT("Joy") || S == TEXT("Angry") || S == TEXT("Sorrow") || S == TEXT("Fun")) HasEmotes = true;
	}
	if ((HasVisemes && HasBlinkLR) || (HasVisemes && HasEmotes)) return ELLRemapPreset::VMC_VRM;

	return ELLRemapPreset::None;
}
