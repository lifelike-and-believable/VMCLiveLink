// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "AnimNode_VRMSpringBones.h"
#include "Animation/AnimInstanceProxy.h"

/* ============================================================================
 *  VRM Spring Bones anim node: maps SpringData onto the pose and runs FVRMSpringSolver.
 *  NOTE: All SpringData geometry and scalar lengths are expected to be in UE units (cm).
 *        The spring bone parser converts from glTF (metres, node-local offsets) to UE axes and cm, with
 *        collider offsets in the axis-aligned bone space of the imported skeleton (VRMCoordinateConversion.h).
 * ============================================================================ */

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
static TAutoConsoleVariable<int32> CVarVRMSB_DrawColliders(
	TEXT("vrm.SpringBones.DrawColliders"),
	0,
	TEXT("Draw debug spring bone colliders.\n")
	TEXT("0 = off, 1 = on."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarVRMSB_DrawSprings(
	TEXT("vrm.SpringBones.DrawSprings"),
	0,
	TEXT("Draw debug spring joints.\n")
	TEXT("0 = off, 1 = head/tail, 2 = +velocity trail, 3 = +rest target"),
	ECVF_Default);
#endif

#define LOCTEXT_NAMESPACE "AnimNode_VRMSpringBones"

/* ---------------------------------------------------------------------------
 *  FAnimNode_VRMSpringBones overrides
 * --------------------------------------------------------------------------- */

void FAnimNode_VRMSpringBones::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	Super::Initialize_AnyThread(Context);
	Solver.Init(FVRMSpringSolverSetup());
	SolverBones.Reset();
	LastOutBoneTransforms.Reset();
	BuiltBoneValid.Reset();
	BuiltForData = nullptr;
	bEvalCalledThisFrame = false;
	GetEvaluateGraphExposedInputs().Execute(Context);
}

void FAnimNode_VRMSpringBones::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	Super::CacheBones_AnyThread(Context);
	RebuildForBones(Context.AnimInstanceProxy->GetRequiredBones());
}

void FAnimNode_VRMSpringBones::UpdateInternal(const FAnimationUpdateContext& Context)
{
	Super::UpdateInternal(Context);
	GetEvaluateGraphExposedInputs().Execute(Context);
	BeginFrame(Context.GetDeltaTime());
}

void FAnimNode_VRMSpringBones::BeginFrame(float DeltaTime)
{
	CurrentDeltaTime = DeltaTime;
	bEvalCalledThisFrame = false;
}

void FAnimNode_VRMSpringBones::RebuildForBones(const FBoneContainer& BoneContainer)
{
	if (!SpringData || !SpringData->SpringConfig.IsValid())
	{
		return;
	}
	BuildMappings(BoneContainer);
}

void FAnimNode_VRMSpringBones::ResetDynamics(ETeleportType InTeleportType)
{
	// Applied on the next evaluation, on the anim thread: tails restart from the current pose, so a
	// teleport or a cut doesn't whip or stretch the chains.
	bResetRequested = true;
}

bool FAnimNode_VRMSpringBones::MappingsAreStale() const
{
	if (!SpringData)
	{
		return false;
	}
	const FVRMSpringConfig& Cfg = SpringData->SpringConfig;
	return SpringData != BuiltForData
		|| SpringData->EditRevision != BuiltForEditRevision
		|| SpringData->SourceHash != BuiltForSourceHash
		|| Cfg.Joints.Num() != BuiltForJointCount
		|| Cfg.Springs.Num() != BuiltForSpringCount;
}

bool FAnimNode_VRMSpringBones::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	return bEnable && SpringData && SpringData->SpringConfig.IsValid();
}

void FAnimNode_VRMSpringBones::GatherDebugData(FNodeDebugData& DebugData)
{
	Super::GatherDebugData(DebugData);
	DebugData.AddDebugItem(FString::Printf(TEXT("VRMSpringBones: %d joints"), LastOutBoneTransforms.Num()));
}

/* ---------------------------------------------------------------------------
 *  Mapping
 * --------------------------------------------------------------------------- */

void FAnimNode_VRMSpringBones::BuildMappings(const FBoneContainer& BoneContainer)
{
	const FVRMSpringConfig& Cfg = SpringData->SpringConfig;

	FVRMSpringSolverSetup Setup;
	TArray<FCompactPoseBoneIndex> NewSolverBones;
	TMap<int32, int32> PoseToSolver; // compact pose index -> solver bone
	TArray<bool> BoneValid;          // for each named bone looked up, in order: does this LOD have it

	auto SolverBoneFor = [&](FCompactPoseBoneIndex PoseBone) -> int32
	{
		if (const int32* Found = PoseToSolver.Find(PoseBone.GetInt()))
		{
			return *Found;
		}
		const int32 Index = NewSolverBones.Add(PoseBone);
		PoseToSolver.Add(PoseBone.GetInt(), Index);
		return Index;
	};
	// A named bone's solver index, or INDEX_NONE when the name is empty or this LOD doesn't have it.
	auto NamedBone = [&](FName Name) -> int32
	{
		if (Name.IsNone())
		{
			return INDEX_NONE;
		}
		FBoneReference Ref;
		Ref.BoneName = Name;
		Ref.Initialize(BoneContainer);
		// IsValidToEvaluate, not HasValidSetup: a bone this LOD strips still has a skeleton index,
		// but no compact pose index, so it must not be simulated.
		const bool bValid = Ref.IsValidToEvaluate(BoneContainer);
		BoneValid.Add(bValid);
		return bValid ? SolverBoneFor(Ref.GetCompactPoseIndex(BoneContainer)) : INDEX_NONE;
	};
	auto JointBoneName = [&](int32 JointIndex) -> FName
	{
		const FVRMSpringJoint& Joint = Cfg.Joints[JointIndex];
		return (Joint.BoneName.IsNone() && Joint.NodeIndex != INDEX_NONE) ? SpringData->GetBoneNameForNode(Joint.NodeIndex) : Joint.BoneName;
	};

	// Colliders are created when a spring first uses them. One whose bone this skeleton doesn't have
	// is left out rather than placed at the origin; one with no bone sits at the component origin.
	TMap<int32, int32> ConfigToSetupCollider;
	auto SetupColliderFor = [&](int32 ColliderIndex) -> int32
	{
		if (const int32* Found = ConfigToSetupCollider.Find(ColliderIndex))
		{
			return *Found;
		}
		const FVRMSpringCollider& Col = Cfg.Colliders[ColliderIndex];
		const int32 Bone = NamedBone(Col.BoneName);
		int32 Result = INDEX_NONE;
		if (Col.BoneName.IsNone() || Bone != INDEX_NONE)
		{
			FVRMSpringSolverSetup::FCollider& Out = Setup.Colliders.AddDefaulted_GetRef();
			Out.Bone = Bone;
			Out.Spheres = Col.Spheres;
			Out.Capsules = Col.Capsules;
			Out.Planes = Col.Planes;
			Result = Setup.Colliders.Num() - 1;
		}
		ConfigToSetupCollider.Add(ColliderIndex, Result);
		return Result;
	};

	TSet<int32> SimulatedBones; // a bone is simulated by one joint only
	for (const FVRMSpring& Spring : Cfg.Springs)
	{
		FVRMSpringSolverSetup::FChain Chain;
		const FName CenterName = (Spring.CenterBoneName.IsNone() && Spring.CenterNodeIndex != INDEX_NONE)
			? SpringData->GetBoneNameForNode(Spring.CenterNodeIndex) : Spring.CenterBoneName;
		Chain.CenterBone = NamedBone(CenterName);
		for (int32 GroupIndex : Spring.ColliderGroupIndices)
		{
			if (!Cfg.ColliderGroups.IsValidIndex(GroupIndex)) continue;
			for (int32 ColliderIndex : Cfg.ColliderGroups[GroupIndex].ColliderIndices)
			{
				if (!Cfg.Colliders.IsValidIndex(ColliderIndex)) continue;
				const int32 SetupCollider = SetupColliderFor(ColliderIndex);
				if (SetupCollider != INDEX_NONE) Chain.Colliders.AddUnique(SetupCollider);
			}
		}

		// Joints whose bone is missing (bad index, not in this LOD, already simulated) or that have no
		// tail split the spring; each run of good joints becomes a chain.
		const FVRMSpringSolverSetup::FChain Empty = Chain;
		auto Flush = [&]()
		{
			if (Chain.Joints.Num() > 0)
			{
				Setup.Chains.Add(MoveTemp(Chain));
			}
			Chain = Empty;
		};

		const TArray<int32>& Indices = Spring.JointIndices;
		for (int32 K = 0; K < Indices.Num(); ++K)
		{
			const int32 JointIndex = Indices[K];
			const int32 Bone = Cfg.Joints.IsValidIndex(JointIndex) ? NamedBone(JointBoneName(JointIndex)) : INDEX_NONE;
			if (Bone == INDEX_NONE || SimulatedBones.Contains(Bone))
			{
				Flush();
				continue;
			}

			// The tail is the next joint's bone. At the end of a chain, VRM 0.x adds a virtual tail;
			// in VRM 1.0 the last joint only marks the tail and isn't simulated.
			int32 TailBone = INDEX_NONE;
			if (Indices.IsValidIndex(K + 1) && Cfg.Joints.IsValidIndex(Indices[K + 1]))
			{
				TailBone = NamedBone(JointBoneName(Indices[K + 1]));
			}
			if (TailBone == INDEX_NONE && Cfg.Spec != EVRMSpringSpec::VRM0)
			{
				Flush();
				continue;
			}

			FVRMSpringSolverSetup::FJoint& J = Chain.Joints.AddDefaulted_GetRef();
			J.Bone = Bone;
			J.TailBone = TailBone;
			if (TailBone == INDEX_NONE && Chain.Joints.Num() == 1)
			{
				// A virtual tail on a chain's first joint is aimed away from its skeleton parent.
				const FCompactPoseBoneIndex Parent = BoneContainer.GetParentBoneIndex(NewSolverBones[Bone]);
				J.ParentBone = Parent.IsValid() ? SolverBoneFor(Parent) : INDEX_NONE;
			}
			const FVRMSpringJoint& Params = Cfg.Joints[JointIndex];
			J.Stiffness = Params.Stiffness;
			J.Drag = Params.Drag;
			J.GravityDir = Params.GravityDir;
			J.GravityPower = Params.GravityPower;
			J.HitRadius = Params.HitRadius;
			SimulatedBones.Add(Bone);
		}
		Flush();
	}
	Setup.NumBones = NewSolverBones.Num();

	// Keep the simulation running if nothing it depends on changed (CacheBones runs again for reasons
	// other than an LOD change). Otherwise restart it from the next pose: the chains are different.
	const bool bSameData = SpringData == BuiltForData && SpringData->EditRevision == BuiltForEditRevision && SpringData->SourceHash == BuiltForSourceHash
		&& Cfg.Joints.Num() == BuiltForJointCount && Cfg.Springs.Num() == BuiltForSpringCount;
	if (!bSameData || BoneValid != BuiltBoneValid || Solver.GetSetup().NumBones != Setup.NumBones)
	{
		Solver.Init(Setup);
	}
	SolverBones = MoveTemp(NewSolverBones);
	BuiltBoneValid = MoveTemp(BoneValid);
	BuiltForData = SpringData;
	BuiltForEditRevision = SpringData->EditRevision;
	BuiltForSourceHash = SpringData->SourceHash;
	BuiltForJointCount = Cfg.Joints.Num();
	BuiltForSpringCount = Cfg.Springs.Num();
	LastOutBoneTransforms.Reset(); // compact pose indices may have changed
	bEvalCalledThisFrame = false;
}

/* ---------------------------------------------------------------------------
 *  Evaluation
 * --------------------------------------------------------------------------- */

void FAnimNode_VRMSpringBones::EvaluateSkeletalControl_AnyThread(
	FComponentSpacePoseContext& Context,
	TArray<FBoneTransform>& OutBoneTransforms)
{
	EvaluateInternal(Context.AnimInstanceProxy, Context.Pose, Context.AnimInstanceProxy->GetComponentTransform(), OutBoneTransforms);
}

void FAnimNode_VRMSpringBones::EvaluateInternal(FAnimInstanceProxy* Proxy, FCSPose<FCompactPose>& CSPose, const FTransform& ComponentTM, TArray<FBoneTransform>& OutBoneTransforms)
{
	// A second evaluation in the same frame (several evaluations per update) re-emits the first
	// result instead of simulating again or returning nothing (SR-04).
	if (bEvalCalledThisFrame)
	{
		OutBoneTransforms = LastOutBoneTransforms;
		return;
	}
	if (!bEnable || !SpringData || !SpringData->SpringConfig.IsValid()) return;

	const FBoneContainer& BoneContainer = CSPose.GetPose().GetBoneContainer();

	// SpringData can be swapped or edited while running; rebuild before touching any array (SR-05).
	if (MappingsAreStale())
	{
		BuildMappings(BoneContainer);
	}

	SolverBonesCS.SetNum(SolverBones.Num(), EAllowShrinking::No);
	for (int32 I = 0; I < SolverBones.Num(); ++I)
	{
		SolverBonesCS[I] = CSPose.GetComponentSpaceTransform(SolverBones[I]);
	}

	if (bResetRequested)
	{
		bResetRequested = false;
		Solver.Reset(SolverBonesCS, ComponentTM);
	}

	// Paused: the tails stay where they are and the joints keep pointing at them.
	const float Dt = bPauseSimulation ? 0.f : CurrentDeltaTime;
	Solver.Step(Dt, SolverBonesCS, ComponentTM, ExternalVelocity * ExternalVelocityScale);

	OutBoneTransforms.Reset();
	const TConstArrayView<FTransform> Joints = Solver.JointTransforms();
	const TConstArrayView<int32> Bones = Solver.JointBones();
	OutBoneTransforms.Reserve(Joints.Num());
	for (int32 I = 0; I < Joints.Num(); ++I)
	{
		OutBoneTransforms.Add(FBoneTransform(SolverBones[Bones[I]], Joints[I]));
	}
	OutBoneTransforms.Sort(FCompareBoneTransformIndex());

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	DrawDebug(Proxy, ComponentTM, Dt);
#endif

	bEvalCalledThisFrame = true;
	LastOutBoneTransforms = OutBoneTransforms;
}

/* ---------------------------------------------------------------------------
 *  Debug drawing
 * --------------------------------------------------------------------------- */
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
namespace
{
	void DrawCollisionSphere(FAnimInstanceProxy* Proxy, const FTransform& NodeXf, const FVRMSpringColliderSphere& S)
	{
		const FVector Center = NodeXf.TransformPosition(S.Offset);
		if (S.Radius <= 0.f)
		{
			Proxy->AnimDrawDebugSphere(Center, 1.f, 8, FColor::Yellow, false, -1.f, 0.25f, SDPG_World);
			return;
		}
		Proxy->AnimDrawDebugSphere(Center, S.Radius, 12, FColor::Green, false, -1.f, 0.25f, SDPG_World);
	}

	void DrawCollisionCapsule(FAnimInstanceProxy* Proxy, const FTransform& NodeXf, const FVRMSpringColliderCapsule& Cap)
	{
		const FVector P0 = NodeXf.TransformPosition(Cap.Offset);
		const FVector P1 = NodeXf.TransformPosition(Cap.TailOffset);
		const FVector Center = (P0 + P1) * 0.5f;
		const FVector Dir = (P1 - P0).GetSafeNormal();
		if (Dir.IsNearlyZero())
		{
			Proxy->AnimDrawDebugSphere(Center, Cap.Radius, 12, FColor::Green, false, -1.f, 0.25f, SDPG_World);
			return;
		}
		// AnimDrawDebugCapsule's half height includes the hemispheres.
		const float HalfHeight = (P1 - P0).Size() * 0.5f + Cap.Radius;
		const FRotator Rotation = FRotator(FRotationMatrix::MakeFromZ(Dir).ToQuat());
		Proxy->AnimDrawDebugCapsule(Center, HalfHeight, Cap.Radius, Rotation, FColor::Green, false, -1.f, 0.25f, SDPG_World);
	}

	void DrawCollisionPlane(FAnimInstanceProxy* Proxy, const FTransform& NodeXf, const FVRMSpringColliderPlane& P)
	{
		const FVector Center = NodeXf.TransformPosition(P.Offset);
		FVector NormalWS = NodeXf.TransformVectorNoScale(P.Normal).GetSafeNormal();
		if (NormalWS.IsNearlyZero()) NormalWS = FVector(0, 0, 1);

		const float HalfSize = 50.f; // debug only, in cm

		FVector Tangent = FVector::CrossProduct(NormalWS, FVector(0, 1, 0));
		if (Tangent.IsNearlyZero()) Tangent = FVector::CrossProduct(NormalWS, FVector(1, 0, 0));
		Tangent.Normalize();
		const FVector Bitangent = FVector::CrossProduct(NormalWS, Tangent).GetSafeNormal();

		const FVector C0 = Center + (Tangent * HalfSize) + (Bitangent * HalfSize);
		const FVector C1 = Center + (Tangent * HalfSize) - (Bitangent * HalfSize);
		const FVector C2 = Center - (Tangent * HalfSize) - (Bitangent * HalfSize);
		const FVector C3 = Center - (Tangent * HalfSize) + (Bitangent * HalfSize);

		const FColor PlaneColor = FColor::Blue;
		Proxy->AnimDrawDebugLine(C0, C1, PlaneColor, false, 0.f, 1.f, SDPG_World);
		Proxy->AnimDrawDebugLine(C1, C2, PlaneColor, false, 0.f, 1.f, SDPG_World);
		Proxy->AnimDrawDebugLine(C2, C3, PlaneColor, false, 0.f, 1.f, SDPG_World);
		Proxy->AnimDrawDebugLine(C3, C0, PlaneColor, false, 0.f, 1.f, SDPG_World);
		Proxy->AnimDrawDebugDirectionalArrow(Center, Center + NormalWS * 20.f, 5.f, PlaneColor, false, 0.f, 1.f, SDPG_World);
	}
}

void FAnimNode_VRMSpringBones::DrawDebug(FAnimInstanceProxy* Proxy, const FTransform& ComponentTM, float DeltaTime) const
{
	if (!Proxy)
	{
		return;
	}

	if (CVarVRMSB_DrawColliders.GetValueOnAnyThread() != 0)
	{
		const FVRMSpringSolverSetup& Setup = Solver.GetSetup();
		const TConstArrayView<FTransform> ColliderCS = Solver.ColliderTransforms();
		for (int32 I = 0; I < Setup.Colliders.Num() && I < ColliderCS.Num(); ++I)
		{
			const FTransform NodeXf = ColliderCS[I] * ComponentTM;
			for (const FVRMSpringColliderSphere& S : Setup.Colliders[I].Spheres) DrawCollisionSphere(Proxy, NodeXf, S);
			for (const FVRMSpringColliderCapsule& C : Setup.Colliders[I].Capsules) DrawCollisionCapsule(Proxy, NodeXf, C);
			for (const FVRMSpringColliderPlane& P : Setup.Colliders[I].Planes) DrawCollisionPlane(Proxy, NodeXf, P);
		}
	}

	const int32 Mode = CVarVRMSB_DrawSprings.GetValueOnAnyThread();
	if (Mode == 0)
	{
		return;
	}
	for (const FVRMSpringSolverJointDebug& J : Solver.JointDebug())
	{
		const FVector HeadWS = ComponentTM.TransformPosition(J.Head);
		const FVector TailWS = ComponentTM.TransformPosition(J.Tail);
		Proxy->AnimDrawDebugSphere(HeadWS, FMath::Max(1.f, J.HitRadius * 0.2f), 8, FColor::Red, false, -1.f, 0.25f, SDPG_World);
		Proxy->AnimDrawDebugSphere(TailWS, FMath::Max(1.f, J.HitRadius), 12, FColor::Yellow, false, -1.f, 0.25f, SDPG_World);
		Proxy->AnimDrawDebugLine(HeadWS, TailWS, FColor::Red, false, -1.f, 0.5f, SDPG_World);

		if (Mode >= 2 && DeltaTime > KINDA_SMALL_NUMBER)
		{
			// Motion over the last step, scaled so the line isn't excessively long; kept briefly as a trail.
			const FVector PrevTailWS = ComponentTM.TransformPosition(J.PrevTail);
			Proxy->AnimDrawDebugLine(TailWS, TailWS + (TailWS - PrevTailWS) * 3.f, FColor::Magenta, false, 1.f, 0.f, SDPG_World);
		}
		if (Mode == 3)
		{
			const FVector RestWS = ComponentTM.TransformPosition(J.RestTail);
			Proxy->AnimDrawDebugSphere(RestWS, FMath::Max(1.f, J.HitRadius * 0.25f), 8, FColor::Cyan, false, -1.f, 0.15f, SDPG_World);
		}
	}
}
#endif

#undef LOCTEXT_NAMESPACE
