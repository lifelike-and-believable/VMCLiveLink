// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "VRMAvatarTypes.h"

class UIKRigDefinition;
class USkeletalMesh;

/** One retarget chain: a name and the bones it runs between (inclusive). */
struct FVRMIKRigChain
{
	FName Name;
	FName Start;
	FName End;
};

/**
 * Builds an IK Rig from a VRM avatar's humanoid map (P4.3), so it works whatever the skeleton's
 * bones are called. The chains use the names of the UE5 mannequin's IK Rig (Spine, Neck, Head,
 * LeftArm, LeftLeg, LeftIndex, ...), so an IK retargeter to the mannequin maps them automatically.
 */
namespace VRMIKRig
{
	/** The retarget root: the hips bone, or NAME_None if the avatar doesn't map it. */
	FName RetargetRoot(const FVRMAvatarData& Avatar);

	/**
	 * The chains the avatar's humanoid map can fill, in a fixed order. A chain the avatar lacks
	 * the bones for (no fingers, say) is left out; optional bones fall back to their neighbours
	 * (the spine ends at the upper chest, else the chest, else the spine).
	 */
	TArray<FVRMIKRigChain> MakeChains(const FVRMAvatarData& Avatar);

	/**
	 * Points IKRig at Mesh and replaces its retarget root and chains with the ones above. Chains
	 * whose bones aren't in the mesh's skeleton are skipped and listed in OutProblems. False if
	 * the rig couldn't be set up at all, with the reason in OutProblems: IKRig or Mesh is null,
	 * the avatar has no hips, the hips aren't in the skeleton, or the rig won't take the mesh.
	 */
	bool Build(UIKRigDefinition* IKRig, USkeletalMesh* Mesh, const FVRMAvatarData& Avatar, TArray<FString>* OutProblems = nullptr);
}
