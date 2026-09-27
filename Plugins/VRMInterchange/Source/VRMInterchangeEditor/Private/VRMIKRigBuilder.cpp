// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMIKRigBuilder.h"

#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "Rig/IKRigDefinition.h"
#include "RigEditor/IKRigController.h"

namespace
{
	using B = EVRMHumanBone;

	/** A chain as the mannequin names it, with the humanoid bones that can start and end it, best
	 *  first. Unused entries are None. */
	struct FChainSpec
	{
		const TCHAR* Name;
		B Start[2];
		B End[3];
	};

	// The UE5 mannequin IK Rig's retarget chains (IK_Mannequin), in its order.
	const FChainSpec ChainSpecs[] = {
		{ TEXT("Spine"), { B::Spine }, { B::UpperChest, B::Chest, B::Spine } },
		{ TEXT("Neck"), { B::Neck }, { B::Neck } },
		{ TEXT("Head"), { B::Head }, { B::Head } },
		{ TEXT("LeftClavicle"), { B::LeftShoulder }, { B::LeftShoulder } },
		{ TEXT("RightClavicle"), { B::RightShoulder }, { B::RightShoulder } },
		{ TEXT("LeftArm"), { B::LeftUpperArm }, { B::LeftHand } },
		{ TEXT("RightArm"), { B::RightUpperArm }, { B::RightHand } },
		{ TEXT("LeftLeg"), { B::LeftUpperLeg }, { B::LeftToes, B::LeftFoot } },
		{ TEXT("RightLeg"), { B::RightUpperLeg }, { B::RightToes, B::RightFoot } },
		{ TEXT("LeftThumb"), { B::LeftThumbMetacarpal, B::LeftThumbProximal }, { B::LeftThumbDistal, B::LeftThumbProximal } },
		{ TEXT("LeftIndex"), { B::LeftIndexProximal }, { B::LeftIndexDistal, B::LeftIndexIntermediate, B::LeftIndexProximal } },
		{ TEXT("LeftMiddle"), { B::LeftMiddleProximal }, { B::LeftMiddleDistal, B::LeftMiddleIntermediate, B::LeftMiddleProximal } },
		{ TEXT("LeftRing"), { B::LeftRingProximal }, { B::LeftRingDistal, B::LeftRingIntermediate, B::LeftRingProximal } },
		{ TEXT("LeftPinky"), { B::LeftLittleProximal }, { B::LeftLittleDistal, B::LeftLittleIntermediate, B::LeftLittleProximal } },
		{ TEXT("RightThumb"), { B::RightThumbMetacarpal, B::RightThumbProximal }, { B::RightThumbDistal, B::RightThumbProximal } },
		{ TEXT("RightIndex"), { B::RightIndexProximal }, { B::RightIndexDistal, B::RightIndexIntermediate, B::RightIndexProximal } },
		{ TEXT("RightMiddle"), { B::RightMiddleProximal }, { B::RightMiddleDistal, B::RightMiddleIntermediate, B::RightMiddleProximal } },
		{ TEXT("RightRing"), { B::RightRingProximal }, { B::RightRingDistal, B::RightRingIntermediate, B::RightRingProximal } },
		{ TEXT("RightPinky"), { B::RightLittleProximal }, { B::RightLittleDistal, B::RightLittleIntermediate, B::RightLittleProximal } },
	};

	FName FirstMapped(const FVRMAvatarData& Avatar, TConstArrayView<B> Candidates)
	{
		for (const B Bone : Candidates)
		{
			if (Bone == B::None)
			{
				continue;
			}
			if (const FName* Name = Avatar.HumanoidToBone.Find(Bone))
			{
				if (!Name->IsNone())
				{
					return *Name;
				}
			}
		}
		return NAME_None;
	}
}

namespace VRMIKRig
{
	FName RetargetRoot(const FVRMAvatarData& Avatar)
	{
		const B Hips[] = { B::Hips };
		return FirstMapped(Avatar, Hips);
	}

	TArray<FVRMIKRigChain> MakeChains(const FVRMAvatarData& Avatar)
	{
		TArray<FVRMIKRigChain> Chains;
		for (const FChainSpec& Spec : ChainSpecs)
		{
			const FName Start = FirstMapped(Avatar, Spec.Start);
			const FName End = FirstMapped(Avatar, Spec.End);
			if (!Start.IsNone() && !End.IsNone())
			{
				Chains.Add({ FName(Spec.Name), Start, End });
			}
		}
		return Chains;
	}

	bool Build(UIKRigDefinition* IKRig, USkeletalMesh* Mesh, const FVRMAvatarData& Avatar, TArray<FString>* OutProblems)
	{
		auto Problem = [OutProblems](const FString& Text)
		{
			if (OutProblems)
			{
				OutProblems->Add(Text);
			}
		};

		const FName Root = RetargetRoot(Avatar);
		if (!IKRig || !Mesh || Root.IsNone())
		{
			Problem(TEXT("The avatar has no hips bone, so no IK Rig can be built from its humanoid map."));
			return false;
		}
		const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
		auto InSkeleton = [&RefSkeleton](FName Bone) { return RefSkeleton.FindBoneIndex(Bone) != INDEX_NONE; };
		if (!InSkeleton(Root))
		{
			Problem(FString::Printf(TEXT("The hips bone '%s' is not in the skeleton of '%s'."), *Root.ToString(), *Mesh->GetName()));
			return false;
		}

		UIKRigController* Controller = UIKRigController::GetController(IKRig);
		if (!Controller || !Controller->SetSkeletalMesh(Mesh))
		{
			Problem(FString::Printf(TEXT("The IK Rig '%s' could not use the mesh '%s'."), *IKRig->GetName(), *Mesh->GetName()));
			return false;
		}

		// Replace what a reused rig had, so a reimport matches the file.
		TArray<FName> OldChains;
		for (const FBoneChain& Chain : IKRig->GetRetargetChains())
		{
			OldChains.Add(Chain.ChainName);
		}
		for (const FName& Chain : OldChains)
		{
			Controller->RemoveRetargetChain(Chain);
		}
		Controller->SetRetargetRoot(Root);

		for (const FVRMIKRigChain& Chain : MakeChains(Avatar))
		{
			if (!InSkeleton(Chain.Start) || !InSkeleton(Chain.End))
			{
				Problem(FString::Printf(TEXT("Chain %s skipped: '%s' or '%s' is not in the skeleton."),
					*Chain.Name.ToString(), *Chain.Start.ToString(), *Chain.End.ToString()));
				continue;
			}
			Controller->AddRetargetChain(Chain.Name, Chain.Start, Chain.End, NAME_None);
		}
		return true;
	}
}
