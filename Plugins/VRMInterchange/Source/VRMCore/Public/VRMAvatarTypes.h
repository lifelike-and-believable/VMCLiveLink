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
	/** No VRM extension (a plain glTF file). */
	Unknown,
	VRM0 UMETA(DisplayName = "VRM 0.x"),
	VRM1 UMETA(DisplayName = "VRM 1.0"),
};

/** The VRM 1.0 humanoid bones. VRM 0.x names are mapped onto these (its thumb bones shift by one). */
UENUM(BlueprintType)
enum class EVRMHumanBone : uint8
{
	/** Not a humanoid bone. */
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
	/** Not a preset: the expression is known by its name. */
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
	/** No effect. */
	None,
	/** Those expressions are off while this one is above 0. */
	Block,
	/** Those expressions are reduced by this one's weight. */
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

	/** The morph target's index within that glTF mesh. */
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

	/** The colour at full expression (linear). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	FLinearColor TargetValue = FLinearColor::White;
};

/** A texture transform an expression changes. Stored; applying it comes later (P4.5). */
USTRUCT(BlueprintType)
struct VRMCORE_API FVRMTextureTransformBind
{
	GENERATED_BODY()

	/** The glTF material's name. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	FString Material;

	/** UV scale at full expression. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	FVector2D Scale = FVector2D(1.0, 1.0);

	/** UV offset at full expression. */
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

	/** The preset it is, or Custom. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	EVRMExpressionPreset Preset = EVRMExpressionPreset::Custom;

	/** Rounds the weight to 0 or 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	bool bIsBinary = false;

	/** How this expression affects the blink expressions while active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	EVRMExpressionOverride OverrideBlink = EVRMExpressionOverride::None;

	/** How this expression affects the look-at expressions while active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	EVRMExpressionOverride OverrideLookAt = EVRMExpressionOverride::None;

	/** How this expression affects the mouth (viseme) expressions while active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	EVRMExpressionOverride OverrideMouth = EVRMExpressionOverride::None;

	/** The morph targets it drives. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	TArray<FVRMMorphBind> MorphBinds;

	/** The material colours it changes (stored, not applied). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	TArray<FVRMMaterialColorBind> MaterialColorBinds;

	/** The texture transforms it changes (stored, not applied). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	TArray<FVRMTextureTransformBind> TextureTransformBinds;
};

/** One look-at range map: input angle (degrees) up to InputMaxValue maps to OutputScale. */
USTRUCT(BlueprintType)
struct VRMCORE_API FVRMLookAtRange
{
	GENERATED_BODY()

	/** The largest input angle, in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	float InputMaxValue = 90.f;

	/** The output at InputMaxValue: degrees of eye bone rotation (Bone look-at), or an expression weight (Expression look-at). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	float OutputScale = 10.f;
};

/** How the eyes look at a target. */
UENUM(BlueprintType)
enum class EVRMLookAtType : uint8
{
	/** Rotate the eye bones. */
	Bone,
	/** Drive the lookUp/Down/Left/Right expressions. */
	Expression,
};

/** The avatar's look-at settings (VRM 1.0 lookAt, VRM 0.x firstPerson look-at fields). */
USTRUCT(BlueprintType)
struct VRMCORE_API FVRMLookAt
{
	GENERATED_BODY()

	/** Whether look-at rotates bones or drives expressions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	EVRMLookAtType Type = EVRMLookAtType::Bone;

	/** The eyes' position relative to the head bone, in Unreal axes and centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	FVector OffsetFromHeadBone = FVector::ZeroVector;

	/** Looking toward the nose (each eye's inner side). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	FVRMLookAtRange HorizontalInner;

	/** Looking away from the nose. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	FVRMLookAtRange HorizontalOuter;

	/** Looking down. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	FVRMLookAtRange VerticalDown;

	/** Looking up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	FVRMLookAtRange VerticalUp;
};

/** Which views a mesh is drawn in (VRM firstPerson meshAnnotations). */
UENUM(BlueprintType)
enum class EVRMFirstPersonType : uint8
{
	/** Decided by the application (typically: hidden in first person if it is weighted to the head). */
	Auto,
	/** Always drawn. */
	Both,
	/** Hidden in first person. */
	ThirdPersonOnly,
	/** Drawn in first person only. */
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

	/** Which views it is drawn in. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	EVRMFirstPersonType Type = EVRMFirstPersonType::Auto;
};

/** Who made the avatar and how it may be used. VRM 0.x fields are mapped onto the VRM 1.0 ones. */
USTRUCT(BlueprintType)
struct VRMCORE_API FVRMMeta
{
	GENERATED_BODY()

	/** The avatar's name. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	FString Name;

	/** The avatar's own version (the author's, not the VRM version). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	FString Version;

	/** The avatar's authors. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	TArray<FString> Authors;

	/** Copyright notice. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	FString CopyrightInformation;

	/** How to contact the authors. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	FString ContactInformation;

	/** What the avatar is based on (URLs or text). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	TArray<FString> References;

	/** Licences of third-party material in the avatar. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	FString ThirdPartyLicenses;

	/** VRM 1.0 licence document URL. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	FString LicenseUrl;

	/** VRM 0.x licence name (e.g. CC_BY, Redistribution_Prohibited, Other). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	FString LicenseName;

	/** URL of any further licence terms. */
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

	/** Whether the avatar may be used in excessively violent content. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	bool bAllowExcessivelyViolentUsage = false;

	/** Whether the avatar may be used in excessively sexual content. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	bool bAllowExcessivelySexualUsage = false;

	/** Whether the avatar may be used for political or religious purposes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	bool bAllowPoliticalOrReligiousUsage = false;

	/** Whether the avatar may be used for antisocial or hateful purposes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	bool bAllowAntisocialOrHateUsage = false;

	/** Whether the avatar file may be redistributed. */
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

	/** The VRM version the file declares. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	EVRMAvatarVersion Version = EVRMAvatarVersion::Unknown;

	/** Authorship and licence. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM")
	FVRMMeta Meta;

	/** Humanoid bone to skeleton bone (the name the importer gave it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	TMap<EVRMHumanBone, FName> HumanoidToBone;

	/** The expressions (VRM 1.0) or blend shape groups (VRM 0.x). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	TArray<FVRMExpression> Expressions;

	/** Look-at settings. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	FVRMLookAt LookAt;

	/** Per mesh, which views it is drawn in. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRM")
	TArray<FVRMFirstPersonAnnotation> FirstPerson;
};
