// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VMCHumanoid.h"

namespace VMCHumanoid
{
	const FName RootBoneName(TEXT("root"));

	namespace
	{
		struct FBoneDef
		{
			const TCHAR* Name;
			const TCHAR* Parent; // nullptr for Hips
		};

		// Keep in sync with HUMANOID in scripts/vmc_sender.py.
		const FBoneDef BoneDefs[] = {
			{ TEXT("Hips"), nullptr },
			{ TEXT("LeftUpperLeg"), TEXT("Hips") }, { TEXT("RightUpperLeg"), TEXT("Hips") },
			{ TEXT("LeftLowerLeg"), TEXT("LeftUpperLeg") }, { TEXT("RightLowerLeg"), TEXT("RightUpperLeg") },
			{ TEXT("LeftFoot"), TEXT("LeftLowerLeg") }, { TEXT("RightFoot"), TEXT("RightLowerLeg") },
			{ TEXT("LeftToes"), TEXT("LeftFoot") }, { TEXT("RightToes"), TEXT("RightFoot") },
			{ TEXT("Spine"), TEXT("Hips") }, { TEXT("Chest"), TEXT("Spine") },
			{ TEXT("UpperChest"), TEXT("Chest") }, { TEXT("Neck"), TEXT("UpperChest") },
			{ TEXT("Head"), TEXT("Neck") }, { TEXT("LeftEye"), TEXT("Head") },
			{ TEXT("RightEye"), TEXT("Head") }, { TEXT("Jaw"), TEXT("Head") },
			{ TEXT("LeftShoulder"), TEXT("UpperChest") }, { TEXT("RightShoulder"), TEXT("UpperChest") },
			{ TEXT("LeftUpperArm"), TEXT("LeftShoulder") }, { TEXT("RightUpperArm"), TEXT("RightShoulder") },
			{ TEXT("LeftLowerArm"), TEXT("LeftUpperArm") }, { TEXT("RightLowerArm"), TEXT("RightUpperArm") },
			{ TEXT("LeftHand"), TEXT("LeftLowerArm") }, { TEXT("RightHand"), TEXT("RightLowerArm") },
			// Fingers
			{ TEXT("LeftThumbProximal"), TEXT("LeftHand") }, { TEXT("LeftThumbIntermediate"), TEXT("LeftThumbProximal") }, { TEXT("LeftThumbDistal"), TEXT("LeftThumbIntermediate") },
			{ TEXT("LeftIndexProximal"), TEXT("LeftHand") }, { TEXT("LeftIndexIntermediate"), TEXT("LeftIndexProximal") }, { TEXT("LeftIndexDistal"), TEXT("LeftIndexIntermediate") },
			{ TEXT("LeftMiddleProximal"), TEXT("LeftHand") }, { TEXT("LeftMiddleIntermediate"), TEXT("LeftMiddleProximal") }, { TEXT("LeftMiddleDistal"), TEXT("LeftMiddleIntermediate") },
			{ TEXT("LeftRingProximal"), TEXT("LeftHand") }, { TEXT("LeftRingIntermediate"), TEXT("LeftRingProximal") }, { TEXT("LeftRingDistal"), TEXT("LeftRingIntermediate") },
			{ TEXT("LeftLittleProximal"), TEXT("LeftHand") }, { TEXT("LeftLittleIntermediate"), TEXT("LeftLittleProximal") }, { TEXT("LeftLittleDistal"), TEXT("LeftLittleIntermediate") },
			{ TEXT("RightThumbProximal"), TEXT("RightHand") }, { TEXT("RightThumbIntermediate"), TEXT("RightThumbProximal") }, { TEXT("RightThumbDistal"), TEXT("RightThumbIntermediate") },
			{ TEXT("RightIndexProximal"), TEXT("RightHand") }, { TEXT("RightIndexIntermediate"), TEXT("RightIndexProximal") }, { TEXT("RightIndexDistal"), TEXT("RightIndexIntermediate") },
			{ TEXT("RightMiddleProximal"), TEXT("RightHand") }, { TEXT("RightMiddleIntermediate"), TEXT("RightMiddleProximal") }, { TEXT("RightMiddleDistal"), TEXT("RightMiddleIntermediate") },
			{ TEXT("RightRingProximal"), TEXT("RightHand") }, { TEXT("RightRingIntermediate"), TEXT("RightRingProximal") }, { TEXT("RightRingDistal"), TEXT("RightRingIntermediate") },
			{ TEXT("RightLittleProximal"), TEXT("RightHand") }, { TEXT("RightLittleIntermediate"), TEXT("RightLittleProximal") }, { TEXT("RightLittleDistal"), TEXT("RightLittleIntermediate") },
		};

		TArray<FBone> MakeBones()
		{
			TArray<FBone> Bones;
			Bones.Reserve(UE_ARRAY_COUNT(BoneDefs));
			for (const FBoneDef& Def : BoneDefs)
			{
				FBone Bone;
				Bone.Name = FName(Def.Name);
				if (Def.Parent)
				{
					const FName ParentName(Def.Parent);
					Bone.Parent = Bones.IndexOfByPredicate([&ParentName](const FBone& B) { return B.Name == ParentName; });
					check(Bone.Parent != INDEX_NONE); // parents are listed before children
				}
				Bones.Add(Bone);
			}
			return Bones;
		}
	}

	const TArray<FBone>& GetBones()
	{
		static const TArray<FBone> Bones = MakeBones();
		return Bones;
	}

	int32 FindBone(FName Name)
	{
		// FName comparison ignores case, which matches senders that vary capitalization.
		return GetBones().IndexOfByPredicate([Name](const FBone& B) { return B.Name == Name; });
	}

	void BuildSkeleton(TArray<FName>& OutNames, TArray<int32>& OutParents)
	{
		const TArray<FBone>& Bones = GetBones();
		OutNames.Reset(Bones.Num() + 1);
		OutParents.Reset(Bones.Num() + 1);

		OutNames.Add(RootBoneName);
		OutParents.Add(INDEX_NONE);
		for (const FBone& Bone : Bones)
		{
			OutNames.Add(Bone.Name);
			// Skeleton index = humanoid index + 1 (root is 0); Hips hangs off the root.
			OutParents.Add(Bone.Parent == INDEX_NONE ? 0 : Bone.Parent + 1);
		}
	}
}
