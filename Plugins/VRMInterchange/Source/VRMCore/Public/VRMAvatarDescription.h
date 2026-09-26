// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VRMAvatarTypes.h"
#include "VRMAvatarDescription.generated.h"

class USkeletalMesh;

/**
 * A VRM avatar's description, made next to the skeletal mesh on import (P4.1): the humanoid bone
 * map, expressions, look-at, first person and meta (licence and usage permissions). Other tools
 * read it instead of guessing from bone and curve names: the expressions node (P4.2), the IK Rig
 * pipeline (P4.3) and the VMC remapper (P4.4).
 */
UCLASS(BlueprintType)
class VRMCORE_API UVRMAvatarDescription : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Everything the file says about the avatar. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM", meta = (ShowOnlyInnerProperties))
	FVRMAvatarData Avatar;

	/** The skeletal mesh this avatar was imported as. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM|Assets")
	TSoftObjectPtr<USkeletalMesh> Mesh;

	/** The spring bone data made by the same import, if any (a UVRMSpringBoneData). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM|Assets", meta = (AllowedClasses = "/Script/VRMSpringBonesRuntime.VRMSpringBoneData"))
	TSoftObjectPtr<UObject> SpringData;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM|Source")
	FString SourceFilename;

	/** MD5 of the source file, as text. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM|Source")
	FString SourceHash;

	/** The skeleton bone for a humanoid bone, or NAME_None if the avatar doesn't map it. */
	UFUNCTION(BlueprintPure, Category = "VRM")
	FName GetBone(EVRMHumanBone Bone) const;

	/** The first expression with this preset, or null. */
	const FVRMExpression* FindExpression(EVRMExpressionPreset Preset) const;

	/** The expression with this name (as in the file), or null. */
	const FVRMExpression* FindExpression(FName Name) const;
};
