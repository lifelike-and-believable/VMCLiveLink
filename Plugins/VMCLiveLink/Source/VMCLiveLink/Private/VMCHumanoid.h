// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/**
 * The Unity humanoid skeleton that VMC senders stream. /VMC/Ext/Bone/Pos names are Unity
 * HumanBodyBones names, and their transforms are local to the humanoid parent listed here.
 */
namespace VMCHumanoid
{
	/** Name of the bone that carries /VMC/Ext/Root/Pos. Always skeleton index 0. */
	extern const FName RootBoneName;

	struct FBone
	{
		FName Name;
		/** Index into GetBones() of the humanoid parent, or INDEX_NONE for Hips (whose parent is the root bone). */
		int32 Parent = INDEX_NONE;
	};

	/** The 55 humanoid bones, parents before children, Hips first. */
	const TArray<FBone>& GetBones();

	/** Index into GetBones() for a bone name (case-insensitive), or INDEX_NONE. */
	int32 FindBone(FName Name);

	/**
	 * Builds the published Live Link skeleton: "root" at index 0, then the humanoid bones in table
	 * order, parented per the humanoid hierarchy (Hips under root).
	 */
	void BuildSkeleton(TArray<FName>& OutNames, TArray<int32>& OutParents);

	/** Index of Hips in the skeleton from BuildSkeleton(). */
	constexpr int32 HipsSkeletonIndex = 1;

	/** Index in the skeleton from BuildSkeleton() where non-humanoid bones are parented: Hips. */
	constexpr int32 FallbackParentIndex = HipsSkeletonIndex;
}
