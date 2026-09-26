// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VMCFrameAssembler.h"
#include "VMCHumanoid.h"
#include "VMCConnectionSettings.h"
#include "Roles/LiveLinkAnimationTypes.h"

FVMCFrameAssembler::FVMCFrameAssembler()
{
	VMCHumanoid::BuildSkeleton(BoneNames, BoneParents);
	for (int32 i = 0; i < BoneNames.Num(); ++i)
	{
		BoneIndexByName.Add(BoneNames[i], i);
	}
	Pose.SetNum(BoneNames.Num());
	PoseReceived.Init(false, BoneNames.Num());
}

FVMCFrameAssembler::FMessageResult FVMCFrameAssembler::ApplyMessage(VMCProtocol::EAddress Kind, TConstArrayView<VMCProtocol::FArg> Args, const FVMCConnectionSettings& Settings)
{
	using namespace VMCProtocol;
	FMessageResult Result;
	switch (Kind)
	{
	case EAddress::BonePos:
	{
		FPose Parsed;
		if (!ParseBonePos(Args, Parsed))
		{
			Result.bMalformed = true;
			break;
		}
		const FTransform Xf(
			ToUERotation(Parsed.Rotation, Settings.bUnityToUE),
			ToUEPosition(Parsed.Position, Settings.bUnityToUE, Settings.bMetersToCm),
			FVector::OneVector);
		if (SetBone(Parsed.Name, Xf))
		{
			Result.bStaticChanged = true;
			Result.NewBone = Parsed.Name;
		}
		break;
	}
	case EAddress::RootPos:
	{
		FPose Parsed;
		if (!ParseRootPos(Args, Parsed, Result.bLegacyRoot))
		{
			Result.bMalformed = true;
			break;
		}
		Result.bRootScaleOffset = Parsed.bHasScaleAndOffset;

		FVector Position = ToUEPosition(Parsed.Position, Settings.bUnityToUE, Settings.bMetersToCm);
		FQuat Rotation = ToUERotation(Parsed.Rotation, Settings.bUnityToUE);
		if (!FMath::IsNearlyZero(Settings.YawOffsetDeg))
		{
			// Extra yaw about UE Z
			const FQuat YawDelta(FVector::UpVector, FMath::DegreesToRadians(Settings.YawOffsetDeg));
			Rotation = YawDelta * Rotation;
			Position = YawDelta.RotateVector(Position);
		}
		SetRoot(FTransform(Rotation, Position, FVector::OneVector));
		break;
	}
	case EAddress::BlendVal:
	{
		FName Name;
		float Value = 0.f;
		if (!ParseBlendVal(Args, Name, Value))
		{
			Result.bMalformed = true;
			break;
		}
		Result.bStaticChanged = SetCurve(Name, Value);
		break;
	}
	case EAddress::Time:
	{
		float Seconds = 0.f;
		if (ParseTime(Args, Seconds))
		{
			SenderTime = Seconds;
		}
		else
		{
			Result.bMalformed = true;
		}
		break;
	}
	case EAddress::BlendApply:
		Result.bApply = true;
		break;
	default:
		break; // availability, devices, camera, ...: not used yet
	}
	return Result;
}

bool FVMCFrameAssembler::SetBone(FName Bone, const FTransform& Local)
{
	bool bNew = false;
	int32 Index = INDEX_NONE;
	if (const int32* Found = BoneIndexByName.Find(Bone))
	{
		Index = *Found;
	}
	else
	{
		// Not a Unity humanoid bone: append it (existing indices never move) under Hips.
		Index = BoneNames.Add(Bone);
		BoneParents.Add(VMCHumanoid::FallbackParentIndex);
		BoneIndexByName.Add(Bone, Index);
		Pose.AddDefaulted();
		PoseReceived.Add(false);
		bNew = true;
	}
	Pose[Index] = Local;
	PoseReceived[Index] = true;
	return bNew;
}

void FVMCFrameAssembler::SetRoot(const FTransform& InRoot)
{
	Root = InRoot;
}

bool FVMCFrameAssembler::SetCurve(FName Curve, float Value)
{
	if (const int32* Found = CurveIndexByName.Find(Curve))
	{
		CurveValues[*Found] = Value;
		return false;
	}
	const int32 Index = CurveNames.Add(Curve);
	CurveIndexByName.Add(Curve, Index);
	CurveValues.Add(Value);
	return true;
}

void FVMCFrameAssembler::EndFrame(bool bZeroMissingCurves)
{
	if (bZeroMissingCurves)
	{
		for (float& Value : CurveValues)
		{
			Value = 0.f;
		}
	}
}

FLiveLinkStaticDataStruct FVMCFrameAssembler::MakeStaticData(const TMap<FName, FName>* BoneMap, const TMap<FName, FName>* CurveMap) const
{
	// Mapping keeps the order, so indices stay valid.
	TArray<FName> OutBoneNames = BoneNames;
	TArray<FName> OutCurveNames = CurveNames;
	if (BoneMap)
	{
		for (FName& N : OutBoneNames) if (const FName* M = BoneMap->Find(N)) N = *M;
	}
	if (CurveMap)
	{
		for (FName& C : OutCurveNames) if (const FName* M = CurveMap->Find(C)) C = *M;
	}

	FLiveLinkStaticDataStruct StaticData(FLiveLinkSkeletonStaticData::StaticStruct());
	FLiveLinkSkeletonStaticData& Skel = *StaticData.Cast<FLiveLinkSkeletonStaticData>();
	Skel.SetBoneNames(OutBoneNames);
	Skel.SetBoneParents(BoneParents);
	Skel.PropertyNames = MoveTemp(OutCurveNames); // UE 5.6: curve names live on the base static data
	return StaticData;
}

FLiveLinkFrameDataStruct FVMCFrameAssembler::MakeFrameData(const FFrameOptions& Options) const
{
	FLiveLinkFrameDataStruct Frame(FLiveLinkAnimationFrameData::StaticStruct());
	FLiveLinkAnimationFrameData& Anim = *Frame.Cast<FLiveLinkAnimationFrameData>();

	const int32 NumBones = BoneNames.Num();
	Anim.Transforms.SetNum(NumBones);
	Anim.PropertyValues = CurveValues;

	const bool bRefOffsets = Options.bUseRefOffsets && Options.RefOffsets && Options.RefOffsets->Num() > 0;

	// Transforms are LOCAL (parent space), per the Live Link animation role.
	for (int32 i = 0; i < NumBones; ++i)
	{
		const bool bReceived = PoseReceived[i];
		FTransform X = FTransform::Identity;

		if (i == 0)
		{
			// Root: a Bone/Pos entry named "root" if the sender provides one, otherwise the
			// dedicated /VMC/Ext/Root/Pos stream.
			X = bReceived ? Pose[i] : Root;
		}
		else
		{
			if (bReceived)
			{
				X.SetRotation(Pose[i].GetRotation());
			}

			// Hips always uses the streamed translation: it carries the body's height and
			// movement relative to the root. Other bones use it only when the stream is trusted
			// to send proper local translations; otherwise the reference skeleton's offsets.
			const bool bIsHips = (i == VMCHumanoid::HipsSkeletonIndex);
			bool bHaveTranslation = false;
			if (bReceived && (bIsHips || Options.bPreferIncomingTranslations))
			{
				X.SetTranslation(Pose[i].GetTranslation());
				bHaveTranslation = bIsHips || !X.GetTranslation().IsNearlyZero();
			}
			if (!bHaveTranslation && bRefOffsets)
			{
				FName Published = BoneNames[i];
				if (Options.BoneMap)
				{
					if (const FName* Mapped = Options.BoneMap->Find(Published))
					{
						Published = *Mapped;
					}
				}
				if (const FVector* Offset = Options.RefOffsets->Find(Published))
				{
					X.SetTranslation(*Offset);
				}
			}
		}
		Anim.Transforms[i] = X;
	}
	return Frame;
}
