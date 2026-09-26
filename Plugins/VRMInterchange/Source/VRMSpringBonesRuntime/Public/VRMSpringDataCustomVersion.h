// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"

/**
 * Version of the data saved in UVRMSpringBoneData assets. Add an entry whenever the meaning or
 * layout of saved spring data changes, and handle older versions in UVRMSpringBoneData::PostLoad:
 * upgrade the data when that's possible, or flag the asset for reimport when it isn't.
 */
struct VRMSPRINGBONESRUNTIME_API FVRMSpringDataCustomVersion
{
	enum Type
	{
		// Saved before this version existed.
		BeforeCustomVersionWasAdded = 0,

		// The parser returns Unreal axes and centimetres: collider offsets go through the collider
		// node's glTF world transform, gravity follows the mesh axes, and VRM 0.x data gets the same
		// 180-degree yaw as the mesh (P1.11, P1.9). Older data can't be converted without the source
		// file, so it needs a reimport.
		ConvertedColliderAxes,

		// Stiffness, drag, gravity and hit radius are per joint (VRM 1.0 spec, P1.13). Older data
		// had them per spring; PostLoad copies each spring's values to its joints.
		PerJointParameters,

		// VRM 0.x bone groups are expanded to every descendant of each listed root, one chain per
		// branch (P1.12). Older VRM 0.x data only has the listed roots and no node hierarchy, so it
		// needs a reimport.
		ExpandedVRM0Chains,

		// -----<new versions go above this line>-----
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	static const FGuid GUID;

private:
	FVRMSpringDataCustomVersion() = delete;
};
