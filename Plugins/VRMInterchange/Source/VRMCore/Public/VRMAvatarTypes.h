// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "VRMAvatarTypes.generated.h"

/**
 * What a VRM file says about its avatar beyond the mesh (P4.1, X-01): the humanoid bone map, the
 * expressions, look-at, first person and meta, for both VRM 0.x and 1.0. Names and enums follow
 * VRM 1.0; VRM 0.x data is mapped onto them (VRM::BuildAvatarData).
 */

/** The VRM version a file declares. */
UENUM(BlueprintType)
enum class EVRMAvatarVersion : uint8
{
	Unknown,
	VRM0 UMETA(DisplayName = "VRM 0.x"),
	VRM1 UMETA(DisplayName = "VRM 1.0"),
};

/** The VRM 1.0 humanoid bones. VRM 0.x names are mapped onto these (its thumb bones shift by one). */
UENUM(BlueprintType)
enum class EVRMHumanBone : uint8
{
	None,
	Hips, Spine, Chest, UpperChest, Neck, Head, LeftEye, RightEye, Jaw,
	LeftUpperLeg, LeftLowerLeg, LeftFoot, LeftToes,
	RightUpperLeg, RightLowerLeg, RightFoot, RightToes,
	LeftShoulder, LeftUpperArm, LeftLowerArm, LeftHand,
	RightShoulder, RightUpperArm, RightLowerArm, RightHand,
	LeftThumbMetacarpal, LeftThumbProximal, LeftThumbDistal,
	LeftIndexProximal, LeftIndexIntermediate, LeftIndexDistal,
	LeftMiddleProximal, LeftMiddleIntermediate, LeftMiddleDistal,
	LeftRingProximal, LeftRingIntermediate, LeftRingDistal,
	LeftLittleProximal, LeftLittleIntermediate, LeftLittleDistal,
	RightThumbMetacarpal, RightThumbProximal, RightThumbDistal,
	RightIndexProximal, RightIndexIntermediate, RightIndexDistal,
	RightMiddleProximal, RightMiddleIntermediate, RightMiddleDistal,
	RightRingProximal, RightRingIntermediate, RightRingDistal,
	RightLittleProximal, RightLittleIntermediate, RightLittleDistal,
	Count UMETA(Hidden),
};

/** The VRM 1.0 expression presets. VRM 0.x blend shape presets are mapped onto these (joy is happy, a is aa, ...). */
UENUM(BlueprintType)
enum class EVRMExpressionPreset : uint8
{
	Custom,
	Happy, Angry, Sad, Relaxed, Surprised,
	Aa, Ih, Ou, Ee, Oh,
	Blink, BlinkLeft, BlinkRight,
	LookUp, LookDown, LookLeft, LookRight,
	Neutral,
};

/** How an active expression affects blink, look-at or mouth expressions (VRM 1.0 override*). */
UENUM(BlueprintType)
enum class EVRMExpressionOverride : uint8
{
	None,
	Block,
	Blend,
};

/** One morph target an expression drives. */
USTRUCT(BlueprintType)
struct VRMCORE_API FVRMMorphBind
{
	GENERATED_BODY()

	/** The morph target on the imported mesh (the name the importer gave it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	FName MorphTarget;

	/** Weight at full expression, 0 to 1 (VRM 0.x's 0 to 100 is scaled). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	float Weight = 1.f;

	/** The glTF mesh and morph target index it came from. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	int32 MeshIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	int32 TargetIndex = INDEX_NONE;
};

/** A material colour an expression changes. Stored; applying it comes later (P4.5). */
USTRUCT(BlueprintType)
struct VRMCORE_API FVRMMaterialColorBind
{
	GENERATED_BODY()

	/** The glTF material's name. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	FString Material;

	/** VRM 1.0 type (color, emissionColor, shadeColor, matcapColor, rimColor, outlineColor), or the VRM 0.x shader property (e.g. _Color). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	FString Property;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	FLinearColor TargetValue = FLinearColor::White;
};

/** A texture transform an expression changes. Stored; applying it comes later (P4.5). */
USTRUCT(BlueprintType)
struct VRMCORE_API FVRMTextureTransformBind
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	FString Material;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	FVector2D Scale = FVector2D(1.0, 1.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	FVector2D Offset = FVector2D::ZeroVector;
};

/** One expression (VRM 1.0) or blend shape group (VRM 0.x). */
USTRUCT(BlueprintType)
struct VRMCORE_API FVRMExpression
{
	GENERATED_BODY()

	/** The name in the file: the preset or custom key (VRM 1.0), or the group name (VRM 0.x). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	EVRMExpressionPreset Preset = EVRMExpressionPreset::Custom;

	/** Rounds the weight to 0 or 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	bool bIsBinary = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	EVRMExpressionOverride OverrideBlink = EVRMExpressionOverride::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	EVRMExpressionOverride OverrideLookAt = EVRMExpressionOverride::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	EVRMExpressionOverride OverrideMouth = EVRMExpressionOverride::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	TArray<FVRMMorphBind> MorphBinds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	TArray<FVRMMaterialColorBind> MaterialColorBinds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	TArray<FVRMTextureTransformBind> TextureTransformBinds;
};

/** One look-at range map: input angle (degrees) up to InputMaxValue maps to OutputScale. */
USTRUCT(BlueprintType)
struct VRMCORE_API FVRMLookAtRange
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	float InputMaxValue = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	float OutputScale = 10.f;
};

UENUM(BlueprintType)
enum class EVRMLookAtType : uint8
{
	Bone,
	Expression,
};

USTRUCT(BlueprintType)
struct VRMCORE_API FVRMLookAt
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	EVRMLookAtType Type = EVRMLookAtType::Bone;

	/** The eyes' position relative to the head bone, in Unreal axes and centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	FVector OffsetFromHeadBone = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	FVRMLookAtRange HorizontalInner;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	FVRMLookAtRange HorizontalOuter;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	FVRMLookAtRange VerticalDown;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	FVRMLookAtRange VerticalUp;
};

UENUM(BlueprintType)
enum class EVRMFirstPersonType : uint8
{
	Auto,
	Both,
	ThirdPersonOnly,
	FirstPersonOnly,
};

/** Whether a mesh shows in first person, third person or both. */
USTRUCT(BlueprintType)
struct VRMCORE_API FVRMFirstPersonAnnotation
{
	GENERATED_BODY()

	/** The glTF mesh's name (or its node's, VRM 1.0). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	FString Mesh;

	/** The node (VRM 1.0) or mesh (VRM 0.x) index in the file. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	int32 Index = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	EVRMFirstPersonType Type = EVRMFirstPersonType::Auto;
};

/** Who made the avatar and how it may be used. VRM 0.x fields are mapped onto the VRM 1.0 ones. */
USTRUCT(BlueprintType)
struct VRMCORE_API FVRMMeta
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	FString Name;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	FString Version;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	TArray<FString> Authors;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	FString CopyrightInformation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	FString ContactInformation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	TArray<FString> References;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	FString ThirdPartyLicenses;

	/** VRM 1.0 licence document URL. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	FString LicenseUrl;

	/** VRM 0.x licence name (e.g. CC_BY, Redistribution_Prohibited, Other). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	FString LicenseName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	FString OtherLicenseUrl;

	/** Who may perform as the avatar: onlyAuthor, onlySeparatelyLicensedPerson or everyone. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	FString AvatarPermission;

	/** personalNonProfit, personalProfit or corporation (VRM 0.x Allow is corporation, Disallow personalNonProfit). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	FString CommercialUsage;

	/** required or unnecessary. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	FString CreditNotation;

	/** prohibited, allowModification or allowModificationRedistribution. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	FString Modification;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	bool bAllowExcessivelyViolentUsage = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	bool bAllowExcessivelySexualUsage = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	bool bAllowPoliticalOrReligiousUsage = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	bool bAllowAntisocialOrHateUsage = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	bool bAllowRedistribution = false;

	/** The thumbnail's glTF image index, or INDEX_NONE. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	int32 ThumbnailImage = INDEX_NONE;
};

/** Everything VRM::BuildAvatarData reads. */
USTRUCT(BlueprintType)
struct VRMCORE_API FVRMAvatarData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	EVRMAvatarVersion Version = EVRMAvatarVersion::Unknown;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	FVRMMeta Meta;

	/** Humanoid bone to skeleton bone (the name the importer gave it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	TMap<EVRMHumanBone, FName> HumanoidToBone;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	TArray<FVRMExpression> Expressions;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	FVRMLookAt LookAt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	TArray<FVRMFirstPersonAnnotation> FirstPerson;
};
