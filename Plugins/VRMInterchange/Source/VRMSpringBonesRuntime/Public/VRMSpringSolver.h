// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "VRMSpringBonesTypes.h"

/**
 * The VRM spring bone solver without anim graph types, so it can be tested on its own (P2.1).
 *
 * The solver works on its own list of bones, numbered 0..NumBones-1. The caller maps those to the
 * skeleton, fills their component-space transforms from the animated pose each frame, and writes the
 * joint rotations back. Lengths are in UE units (cm).
 *
 * The algorithm is the VRM 1.0 reference (UniVRM, three-vrm): per joint, root to tip,
 *   inertia  = (currentTail - prevTail) * (1 - drag)
 *   stiff    = restDirection * stiffness * dt    (in metres in the reference, so x100 here)
 *   external = gravityDir * gravityPower * dt  (+ external velocity * dt)
 *   tail     = constrained to the bone length, pushed out of colliders, constrained again
 *   rotation = the rest rotation turned so the bone points at the tail
 * and each joint's head follows its parent's simulated rotation. Tails live in world space, or in the
 * spring's center bone space, so moving the character adds inertia. Steps are a fixed length
 * (SubstepHz), so the result doesn't depend on the frame rate.
 */
struct VRMSPRINGBONESRUNTIME_API FVRMSpringSolverSetup
{
	struct FJoint
	{
		int32 Bone = INDEX_NONE;
		/** The bone at the joint's tail (the next joint of the chain), or INDEX_NONE for a virtual tail. */
		int32 TailBone = INDEX_NONE;
		/** The joint's skeleton parent, used to aim a virtual tail. May be INDEX_NONE. For a chain's
		 *  first joint, if the parent is a joint of another chain (a VRM 0.x branch), the joint
		 *  follows that joint's simulated transform rather than the animated pose. */
		int32 ParentBone = INDEX_NONE;
		float Stiffness = 1.f;
		float Drag = 0.5f;
		FVector GravityDir = FVector(0, 0, -1);
		float GravityPower = 0.f;
		float HitRadius = 0.f;
	};

	struct FChain
	{
		/** Root to tip. Each joint after the first is the previous joint's tail. */
		TArray<FJoint> Joints;
		/** Bone whose space the tails live in, or INDEX_NONE for world space. */
		int32 CenterBone = INDEX_NONE;
		/** Indices into Colliders. */
		TArray<int32> Colliders;
	};

	struct FCollider
	{
		/** Bone the shapes are attached to, or INDEX_NONE for the component's origin. */
		int32 Bone = INDEX_NONE;
		TArray<FVRMSpringColliderSphere> Spheres;
		TArray<FVRMSpringColliderCapsule> Capsules;
		TArray<FVRMSpringColliderPlane> Planes;
	};

	int32 NumBones = 0;
	TArray<FChain> Chains;
	TArray<FCollider> Colliders;
};

struct VRMSPRINGBONESRUNTIME_API FVRMSpringSolverSettings
{
	/** Simulation steps per second. */
	float SubstepHz = 60.f;
	/** Longest frame simulated; longer frames (hitches) are cut to this. Bounds the steps per frame. */
	float MaxDeltaTime = 0.1f;
	/** Length of a virtual tail, for a VRM 0.x joint at the end of a chain. */
	float VirtualTailLength = 7.f;
	/** Tails of springs without a center bone live in world space, so moving the character adds
	 *  inertia. When false they live in component space and only the animation moves them. */
	bool bWorldSpace = true;

	bool operator==(const FVRMSpringSolverSettings& Other) const
	{
		return SubstepHz == Other.SubstepHz && MaxDeltaTime == Other.MaxDeltaTime
			&& VirtualTailLength == Other.VirtualTailLength && bWorldSpace == Other.bWorldSpace;
	}
};

/** One joint after a frame, for debug drawing. Component space. */
struct FVRMSpringSolverJointDebug
{
	FVector Head = FVector::ZeroVector;
	FVector Tail = FVector::ZeroVector;
	FVector PrevTail = FVector::ZeroVector;
	FVector RestTail = FVector::ZeroVector;
	float HitRadius = 0.f;
};

class VRMSPRINGBONESRUNTIME_API FVRMSpringSolver
{
public:
	/** Takes the chains and colliders. Rest data is captured by the next Reset. */
	void Init(const FVRMSpringSolverSetup& InSetup, const FVRMSpringSolverSettings& InSettings = FVRMSpringSolverSettings());

	/** Changes the settings without restarting, except that a change of space restarts the tails
	 *  from the next pose. */
	void SetSettings(const FVRMSpringSolverSettings& InSettings);
	const FVRMSpringSolverSettings& GetSettings() const { return Settings; }

	/** Restarts every tail from this pose, with no motion. Also captures bone axes and lengths. */
	void Reset(TConstArrayView<FTransform> BonesCS, const FTransform& ComponentToWorld);

	/**
	 * Advances by DeltaTime (in fixed steps; a frame may take zero or several) and computes the joint
	 * rotations for this pose. Resets first if the solver hasn't been reset since Init.
	 * @param BonesCS          animated component-space transform of every solver bone
	 * @param ExternalVelocity world space, cm/s, added to every tail
	 */
	void Step(float DeltaTime, TConstArrayView<FTransform> BonesCS, const FTransform& ComponentToWorld, const FVector& ExternalVelocity = FVector::ZeroVector);

	/** Component-space transform of each simulated joint after the last Step, in the order of JointBones(). */
	TConstArrayView<FTransform> JointTransforms() const { return OutJoints; }
	/** The solver bone of each entry of JointTransforms(). */
	TConstArrayView<int32> JointBones() const { return OutBones; }
	TConstArrayView<FVRMSpringSolverJointDebug> JointDebug() const { return Debug; }

	const FVRMSpringSolverSetup& GetSetup() const { return Setup; }
	/** Component-space transform of each collider's bone during the last step. */
	TConstArrayView<FTransform> ColliderTransforms() const { return ColliderCS; }

	bool IsInitialized() const { return bHasState; }

	/** Signed distance of a point outside (inside, for an inside shape) a collider shape, less HitRadius;
	 *  negative means penetrating, and OutPushDir is the direction that pushes it out. */
	static float CollideSphere(const FTransform& ColliderCS, const FVRMSpringColliderSphere& Sphere, const FVector& Point, float HitRadius, FVector& OutPushDir);
	static float CollideCapsule(const FTransform& ColliderCS, const FVRMSpringColliderCapsule& Capsule, const FVector& Point, float HitRadius, FVector& OutPushDir);
	static float CollidePlane(const FTransform& ColliderCS, const FVRMSpringColliderPlane& Plane, const FVector& Point, float HitRadius, FVector& OutPushDir);

private:
	struct FJointState
	{
		FVector BoneAxisLocal = FVector::ForwardVector;
		float BoneLength = 0.f;
		// In the chain's simulation space (world or center bone)
		FVector CurrentTail = FVector::ZeroVector;
		FVector PrevTail = FVector::ZeroVector;
	};

	/** Maps positions between component space and a chain's simulation space. */
	FTransform SimSpaceToComponent(const FVRMSpringSolverSetup::FChain& Chain, TConstArrayView<FTransform> BonesCS, const FTransform& ComponentToWorld) const;

	/** One pass over the chains. With Dt > 0 the tails advance; with Dt == 0 only the rotations are
	 *  computed from the current tails (the output pass). */
	void Pass(float Dt, TConstArrayView<FTransform> BonesCS, const FTransform& ComponentToWorld, const FVector& ExternalVelocity, bool bOutput);

	void ResolveCollisions(const FVRMSpringSolverSetup::FChain& Chain, FVector& Tail, float HitRadius) const;

	static FTransform BoneOrIdentity(TConstArrayView<FTransform> BonesCS, int32 Bone);

	FVRMSpringSolverSetup Setup;
	FVRMSpringSolverSettings Settings;
	TArray<TArray<FJointState>> States; // per chain, per joint
	TArray<FTransform> ColliderCS;
	TArray<FTransform> SimulatedCS;     // per bone: its simulated transform this pass, where BoneSimulated
	TArray<bool> BoneSimulated;
	TArray<FTransform> OutJoints;
	TArray<int32> OutBones;
	TArray<FVRMSpringSolverJointDebug> Debug;
	double Accumulator = 0.0;
	bool bHasState = false;
};
