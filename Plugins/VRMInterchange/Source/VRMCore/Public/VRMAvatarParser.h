// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "VRMAvatarTypes.h"

class FVRMDocument;
struct FVRMParsedModel;

namespace VRM
{
	/**
	 * Reads the avatar description from a document: humanoid map, expressions, look-at, first person
	 * and meta (P4.1). Model is the document's parsed model: it names the skeleton bones and morph
	 * targets that the humanoid map and the expression binds point at. Anything that can't be
	 * resolved (a bone or morph target the import doesn't have) is left out and reported in
	 * OutWarnings. False if the file is not a VRM (no VRM or VRMC_vrm extension).
	 */
	VRMCORE_API bool BuildAvatarData(const FVRMDocument& Document, const FVRMParsedModel& Model, FVRMAvatarData& Out, TArray<FString>* OutWarnings = nullptr);

	/** A humanoid bone from its name in a file of the given version (VRM 0.x thumbs map one bone down). */
	VRMCORE_API EVRMHumanBone HumanBoneFromName(const FString& Name, EVRMAvatarVersion Version);

	/** The VRM 1.0 name of a humanoid bone (hips, leftThumbMetacarpal, ...). */
	VRMCORE_API FString HumanBoneName(EVRMHumanBone Bone);

	/** An expression preset from its name in a file of the given version (VRM 0.x joy is happy, a is aa, ...). */
	VRMCORE_API EVRMExpressionPreset ExpressionPresetFromName(const FString& Name, EVRMAvatarVersion Version);

	/** The VRM 1.0 name of a preset (happy, aa, blinkLeft, ...); empty for Custom. */
	VRMCORE_API FString ExpressionPresetName(EVRMExpressionPreset Preset);

	/** One or two lines about the licence and usage permissions, for the import summary. */
	VRMCORE_API FString DescribeLicense(const FVRMMeta& Meta);
}
