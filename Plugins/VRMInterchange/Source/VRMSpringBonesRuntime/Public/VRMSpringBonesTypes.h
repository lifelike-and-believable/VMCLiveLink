// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "VRMSpringBonesTypes.generated.h"

/** Which VRM spring bone format the data was parsed from. */
UENUM(BlueprintType)
enum class EVRMSpringSpec : uint8
{
    /** No spring data. */
    None,
    /** VRM 0.x `secondaryAnimation` (bone groups and collider groups). */
    VRM0,
    /** VRM 1.0 `VRMC_springBone`. */
    VRM1
};

// Units and spaces. The parser converts the file's values (glTF: metres, Y up) to what the runtime
// uses: centimetres and UE axes. Imported bones have identity rest rotations, so a bone's space has
// the model's axes at rest. Collider shapes are in the space of the bone they are attached to
// (FVRMSpringCollider::BoneName); gravity directions are in world space.

/** A sphere collider shape. */
USTRUCT(BlueprintType)
struct VRMSPRINGBONESRUNTIME_API FVRMSpringColliderSphere
{
    GENERATED_BODY()

    /** Center of the sphere, in the collider bone's space, in cm. */
    UPROPERTY(EditAnywhere, Category="VRM|Collider|Sphere") FVector Offset = FVector::ZeroVector;
    /** Radius, in cm. */
    UPROPERTY(EditAnywhere, Category="VRM|Collider|Sphere", meta=(ClampMin="0.0")) float Radius = 0.f;
    /** Keep joints inside the sphere instead of outside (VRM extended collider). */
    UPROPERTY(EditAnywhere, Category="VRM|Collider|Sphere") bool bInside = false;
};

/** A capsule collider shape: a segment from Offset to TailOffset, swept by Radius. */
USTRUCT(BlueprintType)
struct VRMSPRINGBONESRUNTIME_API FVRMSpringColliderCapsule
{
    GENERATED_BODY()

    /** One end of the capsule's segment, in the collider bone's space, in cm. */
    UPROPERTY(EditAnywhere, Category="VRM|Collider|Capsule") FVector Offset = FVector::ZeroVector;
    /** Radius, in cm. */
    UPROPERTY(EditAnywhere, Category="VRM|Collider|Capsule", meta=(ClampMin="0.0")) float Radius = 0.f;
    /** The other end of the segment, in the collider bone's space, in cm. */
    UPROPERTY(EditAnywhere, Category="VRM|Collider|Capsule") FVector TailOffset = FVector::ZeroVector;
    /** Keep joints inside the capsule instead of outside (VRM extended collider). */
    UPROPERTY(EditAnywhere, Category="VRM|Collider|Capsule") bool bInside = false;
};

/** A plane collider shape (VRM extended collider): joints are kept on the side Normal points to. */
USTRUCT(BlueprintType)
struct VRMSPRINGBONESRUNTIME_API FVRMSpringColliderPlane
{
    GENERATED_BODY()

    /** A point on the plane, in the collider bone's space, in cm. */
    UPROPERTY(EditAnywhere, Category="VRM|Collider|Plane") FVector Offset = FVector::ZeroVector;
    /** The plane's normal, a unit vector in the collider bone's space. */
    UPROPERTY(EditAnywhere, Category="VRM|Collider|Plane") FVector Normal = FVector(0,0,1);
};

/** A collider: shapes attached to one bone, which move with it. */
USTRUCT(BlueprintType)
struct VRMSPRINGBONESRUNTIME_API FVRMSpringCollider
{
    GENERATED_BODY()

    // You can use either NodeIndex (parsed from VRM) or BoneName (override) to bind this collider in runtime.
    UPROPERTY(EditAnywhere, Category="VRM|Collider", meta=(ToolTip="Original VRM/glTF node index (informational / optional when BoneName is set)"))
    int32 NodeIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, Category="VRM|Collider", meta=(ToolTip="Unreal bone to which this collider is attached"))
    FName BoneName;

    /** Sphere shapes, in BoneName's space. */
    UPROPERTY(EditAnywhere, Category="VRM|Collider") TArray<FVRMSpringColliderSphere> Spheres;
    /** Capsule shapes, in BoneName's space. */
    UPROPERTY(EditAnywhere, Category="VRM|Collider") TArray<FVRMSpringColliderCapsule> Capsules;
    /** Plane shapes, in BoneName's space. */
    UPROPERTY(EditAnywhere, Category="VRM|Collider") TArray<FVRMSpringColliderPlane> Planes;
};

/** A named set of colliders that springs refer to (FVRMSpring::ColliderGroupIndices). */
USTRUCT(BlueprintType)
struct VRMSPRINGBONESRUNTIME_API FVRMSpringColliderGroup
{
    GENERATED_BODY()

    /** The group's name in the file, if it has one. */
    UPROPERTY(EditAnywhere, Category="VRM|Collider Group") FString Name;
    /** Indices into FVRMSpringConfig::Colliders. */
    UPROPERTY(EditAnywhere, Category="VRM|Collider Group") TArray<int32> ColliderIndices;
};

/** One simulated bone of a spring, with its simulation parameters. */
USTRUCT(BlueprintType)
struct VRMSPRINGBONESRUNTIME_API FVRMSpringJoint
{
    GENERATED_BODY()

    /** The joint's glTF node index in the source file. */
    UPROPERTY(VisibleAnywhere, Category="VRM") int32 NodeIndex = INDEX_NONE;
    /** The skeleton bone the joint simulates. */
    UPROPERTY(VisibleAnywhere, Category="VRM") FName BoneName;

    // Simulation parameters. VRM 1.0 sets them per joint; VRM 0.x copies its bone group's values
    // to every joint. Defaults are the VRM 1.0 spec defaults. Lengths and gravity are in UE units.

    /** Radius of the joint's tail for collisions, in cm (the file's metres x 100). */
    UPROPERTY(EditAnywhere, Category="VRM|Joint", meta=(ClampMin="0.0")) float HitRadius = 0.f;
    /** How strongly the joint returns to its animated direction. The file's value, no unit. */
    UPROPERTY(EditAnywhere, Category="VRM|Joint", meta=(ClampMin="0.0", UIMax="4.0")) float Stiffness = 1.f; // no upper bound in VRM 1.0; UniVRM's slider goes to 4
    /** Fraction of the tail's velocity lost each step, 0 to 1. */
    UPROPERTY(EditAnywhere, Category="VRM|Joint", meta=(ClampMin="0.0", ClampMax="1.0")) float Drag = 0.5f;
    /** Direction gravity pulls the tail, a unit vector in world space (UE axes). */
    UPROPERTY(EditAnywhere, Category="VRM|Joint") FVector GravityDir = FVector(0, 0, -1);
    /** Speed gravity moves the tail at, in cm/s (the file's value x 100). */
    UPROPERTY(EditAnywhere, Category="VRM|Joint", meta=(ClampMin="0.0")) float GravityPower = 0.f;
};

/** A chain of joints simulated together, with the colliders it collides with. */
USTRUCT(BlueprintType)
struct VRMSPRINGBONESRUNTIME_API FVRMSpring
{
    GENERATED_BODY()

    /** The spring's name in the file (VRM 0.x: the bone group's comment). */
    UPROPERTY(VisibleAnywhere, Category="VRM") FString Name;
    /** Indices into FVRMSpringConfig::Joints, root to tip. */
    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<int32> JointIndices;
    /** Indices into FVRMSpringConfig::ColliderGroups. */
    UPROPERTY(VisibleAnywhere, Category="VRM") TArray<int32> ColliderGroupIndices;
    /** glTF node of the spring's center: tails are simulated in its space instead of world space. */
    UPROPERTY(VisibleAnywhere, Category="VRM") int32 CenterNodeIndex = INDEX_NONE;
    /** The skeleton bone for CenterNodeIndex, or None for world space. */
    UPROPERTY(VisibleAnywhere, Category="VRM") FName CenterBoneName;

    // Editing helpers: the solver reads each joint's own parameters (FVRMSpringJoint). Changing one of
    // these in the editor applies it to every joint of the spring. After import they hold the first
    // joint's values (VRM 1.0) or the bone group's values (VRM 0.x).
    // Units as on FVRMSpringJoint.

    /** Sets every joint's stiffness (no unit). */
    UPROPERTY(EditAnywhere, Category="VRM|Spring", meta=(ClampMin="0.0", UIMax="4.0")) float Stiffness = 0.f;
    /** Sets every joint's drag, 0 to 1. */
    UPROPERTY(EditAnywhere, Category="VRM|Spring", meta=(ClampMin="0.0", ClampMax="1.0")) float Drag = 0.f;
    /** Sets every joint's gravity direction, a unit vector in world space. */
    UPROPERTY(EditAnywhere, Category="VRM|Spring") FVector GravityDir = FVector(0, 0, -1);
    /** Sets every joint's gravity power, in cm/s. */
    UPROPERTY(EditAnywhere, Category="VRM|Spring", meta=(ClampMin="0.0")) float GravityPower = 0.f;
    /** Sets every joint's hit radius, in cm. */
    UPROPERTY(EditAnywhere, Category="VRM|Spring", meta=(ClampMin="0.0")) float HitRadius = 0.f;
};

/** Everything a VRM file says about its spring bones, converted to UE units and axes. */
USTRUCT(BlueprintType)
struct VRMSPRINGBONESRUNTIME_API FVRMSpringConfig
{
    GENERATED_BODY()

    /** The format it was parsed from. */
    UPROPERTY(VisibleAnywhere, Category="VRM") EVRMSpringSpec Spec = EVRMSpringSpec::None;

    /** Every collider. Collider groups refer to them by index. */
    UPROPERTY(EditAnywhere, Category="VRM|Colliders", meta=(TitleProperty="BoneName"))
    TArray<FVRMSpringCollider> Colliders;

    /** Every collider group. Springs refer to them by index. */
    UPROPERTY(EditAnywhere, Category="VRM|Colliders", meta=(TitleProperty="Name"))
    TArray<FVRMSpringColliderGroup> ColliderGroups;

    // Editable so per-joint parameters can be tuned; the joint list itself is fixed by the import.
    UPROPERTY(EditAnywhere, Category="VRM", meta=(EditFixedSize, TitleProperty="BoneName")) TArray<FVRMSpringJoint> Joints;

    /** Every spring. The list is fixed by the import; each spring's values can be edited. */
    UPROPERTY(EditAnywhere, Category="VRM", meta=(EditFixedSize, TitleProperty="Name"))
    TArray<FVRMSpring> Springs;

    // RawJson (the whole top-level JSON) was removed in P3.3: it made every spring data asset
    // carry a copy of the file's JSON. Assets saved with it load; the value is skipped.

    /** Whether there is spring data: a format, and at least one spring, joint, collider or group. */
    bool IsValid() const
    {
        return Spec != EVRMSpringSpec::None && (Springs.Num() > 0 || ColliderGroups.Num() > 0 || Colliders.Num() > 0 || Joints.Num() > 0);
    }
};

/** A glTF node's children (UVRMSpringBoneData::NodeChildren); a struct so it can be a map value. */
USTRUCT(BlueprintType)
struct VRMSPRINGBONESRUNTIME_API FVRMNodeChildren
{
    GENERATED_BODY()

    /** Child glTF node indices, in the file's order. */
    UPROPERTY(VisibleAnywhere, Category = "VRM|Hierarchy")
    TArray<int32> Children;
};