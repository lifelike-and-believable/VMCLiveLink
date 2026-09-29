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

	/**
	 * The Unity HumanBodyBones name of a humanoid bone (Hips, LeftUpperArm, ...), which is what VMC
	 * senders stream. VRM 1.0's thumb is one joint off from Unity's: leftThumbMetacarpal is
	 * LeftThumbProximal, leftThumbProximal is LeftThumbIntermediate. Empty for None.
	 */
	VRMCORE_API FString UnityHumanBoneName(EVRMHumanBone Bone);

	/**
	 * The humanoid map as metadata for the imported skeletal mesh (P4.4, decision D-4): the
	 * convention VMCLiveLink reads to map a VMC stream onto the mesh without depending on this
	 * plugin. One key per mapped bone, "VRM.Humanoid.<UnityBoneName>" = the skeleton bone's name,
	 * plus "VRM.HumanoidVersion" = "1" when at least one bone is mapped. With no humanoid map the
	 * result is empty (no version key either). Documented in both plugins' READMEs; keep them in step.
	 */
	VRMCORE_API TMap<FName, FString> MakeHumanoidMetadata(const FVRMAvatarData& Avatar);

	/** The metadata keys' prefix (see MakeHumanoidMetadata). */
	inline const TCHAR* const HumanoidMetadataPrefix = TEXT("VRM.Humanoid.");
	/** The metadata version key (see MakeHumanoidMetadata). */
	inline const TCHAR* const HumanoidMetadataVersionKey = TEXT("VRM.HumanoidVersion");

	/** An expression preset from its name in a file of the given version (VRM 0.x joy is happy, a is aa, ...). */
	VRMCORE_API EVRMExpressionPreset ExpressionPresetFromName(const FString& Name, EVRMAvatarVersion Version);

	/**
	 * A preset's name in a file of the given version: happy, aa, blinkLeft, ... in VRM 1.0; joy, a,
	 * blink_l, ... in VRM 0.x. Empty for Custom.
	 */
	VRMCORE_API FString ExpressionPresetName(EVRMExpressionPreset Preset, EVRMAvatarVersion Version = EVRMAvatarVersion::VRM1);

	/** One or two lines about the licence and usage permissions, for the import summary. */
	VRMCORE_API FString DescribeLicense(const FVRMMeta& Meta);
}
