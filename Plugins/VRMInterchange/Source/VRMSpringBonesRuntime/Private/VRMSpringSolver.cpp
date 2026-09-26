// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMSpringSolver.h"
#include "Templates/Function.h"

namespace
{
	// The reference solver's stiffness force is a distance in metres; the solver works in cm.
	constexpr float CmPerMetre = 100.f;

	FVector ConstrainLength(const FVector& Tail, const FVector& Head, float Length)
	{
		const FVector Dir = (Tail - Head).GetSafeNormal();
		return Dir.IsNearlyZero() ? Tail : Head + Dir * Length;
	}

	bool IsBone(int32 Bone, int32 NumBones)
	{
		return Bone >= 0 && Bone < NumBones;
	}
}

namespace
{
	FVRMSpringSolverSettings Sanitize(FVRMSpringSolverSettings Settings)
	{
		Settings.SubstepHz = FMath::Max(Settings.SubstepHz, 1.f);
		Settings.MaxDeltaTime = FMath::Max(Settings.MaxDeltaTime, 0.f);
		Settings.VirtualTailLength = FMath::Max(Settings.VirtualTailLength, 0.f);
		return Settings;
	}
}

void FVRMSpringSolver::SetSettings(const FVRMSpringSolverSettings& InSettings)
{
	const FVRMSpringSolverSettings New = Sanitize(InSettings);
	if (New == Settings)
	{
		return;
	}
	if (New.bWorldSpace != Settings.bWorldSpace || New.VirtualTailLength != Settings.VirtualTailLength)
	{
		bHasState = false; // tails are stored in the old space; bone lengths may change
	}
	Settings = New;
}

void FVRMSpringSolver::Init(const FVRMSpringSolverSetup& InSetup, const FVRMSpringSolverSettings& InSettings)
{
	Setup = InSetup;
	Settings = Sanitize(InSettings);

	// Drop anything that points outside the bone or collider lists, so Step never has to check.
	const int32 NumBones = Setup.NumBones;
	for (FVRMSpringSolverSetup::FCollider& Collider : Setup.Colliders)
	{
		if (!IsBone(Collider.Bone, NumBones)) Collider.Bone = INDEX_NONE;
	}
	for (FVRMSpringSolverSetup::FChain& Chain : Setup.Chains)
	{
		Chain.Joints.RemoveAll([NumBones](const FVRMSpringSolverSetup::FJoint& J) { return !IsBone(J.Bone, NumBones); });
		for (FVRMSpringSolverSetup::FJoint& J : Chain.Joints)
		{
			if (!IsBone(J.TailBone, NumBones)) J.TailBone = INDEX_NONE;
			if (!IsBone(J.ParentBone, NumBones)) J.ParentBone = INDEX_NONE;
			J.Drag = FMath::Clamp(J.Drag, 0.f, 1.f);
			J.HitRadius = FMath::Max(J.HitRadius, 0.f);
			J.GravityDir = J.GravityDir.GetSafeNormal();
		}
		if (!IsBone(Chain.CenterBone, NumBones)) Chain.CenterBone = INDEX_NONE;
		const int32 NumColliders = Setup.Colliders.Num();
		Chain.Colliders.RemoveAll([NumColliders](int32 C) { return C < 0 || C >= NumColliders; });
	}
	Setup.Chains.RemoveAll([](const FVRMSpringSolverSetup::FChain& Chain) { return Chain.Joints.Num() == 0; });

	// A chain whose first joint hangs off a joint of another chain (VRM 0.x branches, P1.12) is
	// simulated after that chain, so its first joint can follow the parent's simulated transform
	// (Pass). Otherwise the order is kept.
	{
		TArray<int32> ChainOfBone;
		ChainOfBone.Init(INDEX_NONE, NumBones);
		for (int32 C = 0; C < Setup.Chains.Num(); ++C)
		{
			for (const FVRMSpringSolverSetup::FJoint& J : Setup.Chains[C].Joints)
			{
				if (ChainOfBone[J.Bone] == INDEX_NONE) ChainOfBone[J.Bone] = C;
			}
		}
		TArray<int32> Order;
		TArray<uint8> Visited; // 0: not yet, 1: in progress (guards a cycle), 2: placed
		Visited.Init(0, Setup.Chains.Num());
		TFunction<void(int32)> Place = [&](int32 C)
		{
			if (Visited[C] != 0) return;
			Visited[C] = 1;
			const int32 Parent = Setup.Chains[C].Joints[0].ParentBone;
			const int32 ParentChain = Parent != INDEX_NONE ? ChainOfBone[Parent] : INDEX_NONE;
			if (ParentChain != INDEX_NONE && ParentChain != C)
			{
				Place(ParentChain);
			}
			Visited[C] = 2;
			Order.Add(C);
		};
		for (int32 C = 0; C < Setup.Chains.Num(); ++C)
		{
			Place(C);
		}
		TArray<FVRMSpringSolverSetup::FChain> Ordered;
		Ordered.Reserve(Order.Num());
		for (const int32 C : Order)
		{
			Ordered.Add(MoveTemp(Setup.Chains[C]));
		}
		Setup.Chains = MoveTemp(Ordered);
	}

	States.Reset();
	States.SetNum(Setup.Chains.Num());
	for (int32 C = 0; C < Setup.Chains.Num(); ++C)
	{
		States[C].SetNum(Setup.Chains[C].Joints.Num());
	}
	ColliderCS.Reset();
	OutJoints.Reset();
	OutBones.Reset();
	Debug.Reset();
	Accumulator = 0.0;
	bHasState = false;
}

FTransform FVRMSpringSolver::BoneOrIdentity(TConstArrayView<FTransform> BonesCS, int32 Bone)
{
	return BonesCS.IsValidIndex(Bone) ? BonesCS[Bone] : FTransform::Identity;
}

FTransform FVRMSpringSolver::SimSpaceToComponent(const FVRMSpringSolverSetup::FChain& Chain, TConstArrayView<FTransform> BonesCS, const FTransform& ComponentToWorld) const
{
	// Center space: the center bone's frame, so its motion (and the character's) adds no inertia.
	// Otherwise world space, so the character's motion does, or component space if asked.
	if (Chain.CenterBone != INDEX_NONE)
	{
		return BonesCS[Chain.CenterBone];
	}
	return Settings.bWorldSpace ? ComponentToWorld.Inverse() : FTransform::Identity;
}

void FVRMSpringSolver::Reset(TConstArrayView<FTransform> BonesCS, const FTransform& ComponentToWorld)
{
	if (BonesCS.Num() < Setup.NumBones)
	{
		return;
	}
	for (int32 C = 0; C < Setup.Chains.Num(); ++C)
	{
		const FVRMSpringSolverSetup::FChain& Chain = Setup.Chains[C];
		const FTransform Space = SimSpaceToComponent(Chain, BonesCS, ComponentToWorld);
		for (int32 K = 0; K < Chain.Joints.Num(); ++K)
		{
			const FVRMSpringSolverSetup::FJoint& J = Chain.Joints[K];
			FJointState& S = States[C][K];
			const FTransform& JointCS = BonesCS[J.Bone];
			const FVector Head = JointCS.GetLocation();

			FVector Tail;
			if (J.TailBone != INDEX_NONE)
			{
				Tail = BonesCS[J.TailBone].GetLocation();
			}
			else
			{
				// Virtual tail (VRM 0.x): continue the direction from the parent to this joint.
				const int32 Parent = K > 0 ? Chain.Joints[K - 1].Bone : J.ParentBone;
				FVector Dir = Parent != INDEX_NONE ? (Head - BonesCS[Parent].GetLocation()).GetSafeNormal() : FVector::ZeroVector;
				if (Dir.IsNearlyZero())
				{
					Dir = JointCS.GetRotation().GetAxisX();
				}
				Tail = Head + Dir * Settings.VirtualTailLength;
			}

			S.BoneLength = FVector::Dist(Head, Tail);
			S.BoneAxisLocal = JointCS.GetRotation().UnrotateVector(Tail - Head).GetSafeNormal();
			if (S.BoneAxisLocal.IsNearlyZero())
			{
				S.BoneAxisLocal = FVector::ForwardVector;
			}
			S.CurrentTail = Space.InverseTransformPosition(Tail);
			S.PrevTail = S.CurrentTail;
		}
	}
	Accumulator = 0.0;
	bHasState = true;
}

void FVRMSpringSolver::Step(float DeltaTime, TConstArrayView<FTransform> BonesCS, const FTransform& ComponentToWorld, const FVector& ExternalVelocity)
{
	if (BonesCS.Num() < Setup.NumBones)
	{
		OutJoints.Reset();
		OutBones.Reset();
		Debug.Reset();
		return;
	}
	if (!bHasState)
	{
		Reset(BonesCS, ComponentToWorld);
	}

	const double StepDt = 1.0 / Settings.SubstepHz;
	Accumulator += FMath::Clamp(DeltaTime, 0.f, Settings.MaxDeltaTime);
	// A little slack, so frames that add up to a whole step (two at 120 Hz) take it despite rounding.
	while (Accumulator >= StepDt * (1.0 - 1.0e-4))
	{
		Pass(static_cast<float>(StepDt), BonesCS, ComponentToWorld, ExternalVelocity, /*bOutput*/ false);
		Accumulator -= StepDt;
	}
	Accumulator = FMath::Max(Accumulator, 0.0);

	// Rotations for this frame's pose from the current tails, whether or not a step was taken.
	Pass(0.f, BonesCS, ComponentToWorld, ExternalVelocity, /*bOutput*/ true);
}

void FVRMSpringSolver::Pass(float Dt, TConstArrayView<FTransform> BonesCS, const FTransform& ComponentToWorld, const FVector& ExternalVelocity, bool bOutput)
{
	// Collider transforms once per pass, not per joint.
	ColliderCS.SetNum(Setup.Colliders.Num(), EAllowShrinking::No);
	for (int32 I = 0; I < Setup.Colliders.Num(); ++I)
	{
		ColliderCS[I] = BoneOrIdentity(BonesCS, Setup.Colliders[I].Bone);
	}
	if (bOutput)
	{
		OutJoints.Reset();
		OutBones.Reset();
		Debug.Reset();
	}

	const FVector ExternalCS = ComponentToWorld.InverseTransformVector(ExternalVelocity) * Dt;

	// Joints simulated so far this pass, for chains that hang off another chain's joint.
	SimulatedCS.SetNum(Setup.NumBones, EAllowShrinking::No);
	BoneSimulated.Init(false, Setup.NumBones);

	for (int32 C = 0; C < Setup.Chains.Num(); ++C)
	{
		const FVRMSpringSolverSetup::FChain& Chain = Setup.Chains[C];
		const FTransform Space = SimSpaceToComponent(Chain, BonesCS, ComponentToWorld);

		FTransform PrevUpdated = FTransform::Identity;
		for (int32 K = 0; K < Chain.Joints.Num(); ++K)
		{
			const FVRMSpringSolverSetup::FJoint& J = Chain.Joints[K];
			FJointState& S = States[C][K];

			// The joint's rest frame: its animated local transform under its parent's simulated
			// transform. A chain's first joint has no simulated parent unless it hangs off a joint of
			// an earlier chain (a VRM 0.x branch); otherwise it keeps the animated pose.
			const FTransform& Input = BonesCS[J.Bone];
			FTransform Rest = Input;
			if (K > 0)
			{
				Rest = Input.GetRelativeTransform(BonesCS[Chain.Joints[K - 1].Bone]) * PrevUpdated;
			}
			else if (J.ParentBone != INDEX_NONE && BoneSimulated[J.ParentBone])
			{
				Rest = Input.GetRelativeTransform(BonesCS[J.ParentBone]) * SimulatedCS[J.ParentBone];
			}
			const FVector Head = Rest.GetLocation();
			const FQuat RestRot = Rest.GetRotation();
			const FVector Axis = RestRot.RotateVector(S.BoneAxisLocal);

			FVector Tail = Space.TransformPosition(S.CurrentTail);
			if (Dt > 0.f)
			{
				const FVector PrevTail = Space.TransformPosition(S.PrevTail);
				const FVector Gravity = ComponentToWorld.InverseTransformVector(J.GravityDir * J.GravityPower) * Dt;
				FVector Next = Tail
					+ (Tail - PrevTail) * (1.f - J.Drag)
					+ Axis * (J.Stiffness * Dt * CmPerMetre)
					+ Gravity
					+ ExternalCS;
				Next = ConstrainLength(Next, Head, S.BoneLength);
				if (Chain.Colliders.Num() > 0)
				{
					ResolveCollisions(Chain, Next, J.HitRadius);
					Next = ConstrainLength(Next, Head, S.BoneLength);
				}
				S.PrevTail = S.CurrentTail;
				S.CurrentTail = Space.InverseTransformPosition(Next);
				Tail = Next;
			}

			const FVector Dir = (Tail - Head).GetSafeNormal();
			const FQuat Rot = Dir.IsNearlyZero() ? RestRot : (FQuat::FindBetweenNormals(Axis, Dir) * RestRot).GetNormalized();
			PrevUpdated = FTransform(Rot, Head, Rest.GetScale3D());
			SimulatedCS[J.Bone] = PrevUpdated;
			BoneSimulated[J.Bone] = true;

			if (bOutput)
			{
				OutJoints.Add(PrevUpdated);
				OutBones.Add(J.Bone);
				FVRMSpringSolverJointDebug& D = Debug.AddDefaulted_GetRef();
				D.Head = Head;
				D.Tail = Tail;
				D.PrevTail = Space.TransformPosition(S.PrevTail);
				D.RestTail = Head + Axis * S.BoneLength;
				D.HitRadius = J.HitRadius;
			}
		}
	}
}

void FVRMSpringSolver::ResolveCollisions(const FVRMSpringSolverSetup::FChain& Chain, FVector& Tail, float HitRadius) const
{
	FVector PushDir;
	for (int32 CIdx : Chain.Colliders)
	{
		const FVRMSpringSolverSetup::FCollider& Collider = Setup.Colliders[CIdx];
		const FTransform& Xf = ColliderCS[CIdx];
		for (const FVRMSpringColliderSphere& Sphere : Collider.Spheres)
		{
			const float Distance = CollideSphere(Xf, Sphere, Tail, HitRadius, PushDir);
			if (Distance < 0.f) Tail -= PushDir * Distance;
		}
		for (const FVRMSpringColliderCapsule& Capsule : Collider.Capsules)
		{
			const float Distance = CollideCapsule(Xf, Capsule, Tail, HitRadius, PushDir);
			if (Distance < 0.f) Tail -= PushDir * Distance;
		}
		for (const FVRMSpringColliderPlane& Plane : Collider.Planes)
		{
			const float Distance = CollidePlane(Xf, Plane, Tail, HitRadius, PushDir);
			if (Distance < 0.f) Tail -= PushDir * Distance;
		}
	}
}

float FVRMSpringSolver::CollideSphere(const FTransform& ColliderCS, const FVRMSpringColliderSphere& Sphere, const FVector& Point, float HitRadius, FVector& OutPushDir)
{
	const FVector Delta = Point - ColliderCS.TransformPosition(Sphere.Offset);
	if (Sphere.bInside)
	{
		// Keeps the point inside the sphere.
		OutPushDir = -Delta.GetSafeNormal();
		return (Sphere.Radius - HitRadius) - Delta.Length();
	}
	// A point exactly at the center has no direction to leave by; push it up rather than not at all.
	OutPushDir = Delta.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	return Delta.Length() - (Sphere.Radius + HitRadius);
}

float FVRMSpringSolver::CollideCapsule(const FTransform& ColliderCS, const FVRMSpringColliderCapsule& Capsule, const FVector& Point, float HitRadius, FVector& OutPushDir)
{
	const FVector Head = ColliderCS.TransformPosition(Capsule.Offset);
	const FVector Segment = ColliderCS.TransformPosition(Capsule.TailOffset) - Head;
	FVector Delta = Point - Head;
	const float Dot = FVector::DotProduct(Segment, Delta);
	if (Dot > 0.f)
	{
		const float SegLenSq = Segment.SizeSquared();
		Delta -= Dot > SegLenSq ? Segment : Segment * (Dot / SegLenSq);
	}
	if (Capsule.bInside)
	{
		OutPushDir = -Delta.GetSafeNormal();
		return (Capsule.Radius - HitRadius) - Delta.Length();
	}
	OutPushDir = Delta.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector); // as for a sphere
	return Delta.Length() - (Capsule.Radius + HitRadius);
}

float FVRMSpringSolver::CollidePlane(const FTransform& ColliderCS, const FVRMSpringColliderPlane& Plane, const FVector& Point, float HitRadius, FVector& OutPushDir)
{
	FVector Normal = ColliderCS.TransformVectorNoScale(Plane.Normal).GetSafeNormal();
	if (Normal.IsNearlyZero())
	{
		Normal = FVector::UpVector;
	}
	OutPushDir = Normal;
	return FVector::DotProduct(Point - ColliderCS.TransformPosition(Plane.Offset), Normal) - HitRadius;
}
