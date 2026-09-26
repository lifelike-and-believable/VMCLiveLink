// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "AnimNode_VRMSpringBones.h"
#include "Animation/AnimInstanceProxy.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

/* ============================================================================
 *  VRM Spring Bones Runtime - Core Simulation Node Implementation
 *  NOTE: All SpringData geometry and scalar lengths are expected to be in UE units (cm).
 *        The spring bone parser converts from glTF (metres, node-local offsets) to UE axes and cm, with
 *        collider offsets in the axis-aligned bone space of the imported skeleton (VRMCoordinateConversion.h).
 * ============================================================================ */

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
#define VRMSB_DRAW_SPHERE(Proxy, NodeXf, S) if (CVarVRMSB_DrawColliders.GetValueOnAnyThread() != 0) DrawCollisionSphere(Proxy, NodeXf, S)
#define VRMSB_DRAW_CAPSULE(Proxy, NodeXf, Cap) if (CVarVRMSB_DrawColliders.GetValueOnAnyThread() != 0) DrawCollisionCapsule(Proxy, NodeXf, Cap)
#define VRMSB_DRAW_PLANE(Proxy, NodeXf, P) if (CVarVRMSB_DrawColliders.GetValueOnAnyThread() != 0) DrawCollisionPlane(Proxy, NodeXf, P)

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
	TEXT("0 = off, 1 = head/tail, 2 = +velocity trail, 3 = +animated target"),
	ECVF_Default);

#define VRMSB_DRAW_SPRING(Proxy, ComponentTM, JointState, HeadCS, TailCS, JointRadius, RestTargetCS, Dt) \
    if (CVarVRMSB_DrawSprings.GetValueOnAnyThread() != 0) DrawSpringJoint(Proxy, ComponentTM, JointState, HeadCS, TailCS, JointRadius, RestTargetCS, Dt)
#else
#define VRMSB_DRAW_SPHERE(Proxy, NodeXf, S) ((void)0)
#define VRMSB_DRAW_CAPSULE(Proxy, NodeXf, Cap) ((void)0)
#define VRMSB_DRAW_PLANE(Proxy, NodeXf, P) ((void)0)
#define VRMSB_DRAW_SPRING(Proxy, ComponentTM, JointState, HeadCS, TailCS, JointRadius, RestTargetCS, Dt) ((void)0)
#endif

#define LOCTEXT_NAMESPACE "AnimNode_VRMSpringBones"

/* ---------------------------------------------------------------------------
 *  Static helpers
 * --------------------------------------------------------------------------- */

FVector FAnimNode_VRMSpringBones::ApplyLengthConstraint(const FVRMSimJointState& State, const FVector TailWS, const FVector HeadWS)
{
	const FVector Dir = (TailWS - HeadWS).GetSafeNormal();
	if (!Dir.IsNearlyZero())
	{
		return HeadWS + Dir * State.WorldBoneLength;
	}
	return TailWS;
}

/** One-time per joint state initialization (tail, previous frame data). */
void FAnimNode_VRMSpringBones::InitializeState(FVRMSimJointState& JointState, const FTransform& ComponentTM, const FTransform& JointBoneCS)
{
	if (!JointState.bInitialized)
	{
		const FQuat JointBoneRotCS = JointBoneCS.GetRotation();
		const FVector JointHeadPosCS = JointBoneCS.GetLocation();

		JointState.PrevHeadCS = JointHeadPosCS;

		const FVector InitialJointTailPosCS = JointHeadPosCS + JointBoneRotCS.RotateVector(FVector(JointState.InitialLocalChildPos));
		JointState.CurrentTail = InitialJointTailPosCS;
		JointState.PrevTail = InitialJointTailPosCS;

		JointState.bInitialized = 1;
	}
}

/* ---------------------------------------------------------------------------
 *  FAnimNode_VRMSpringBones overrides
 * --------------------------------------------------------------------------- */

void FAnimNode_VRMSpringBones::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	Super::Initialize_AnyThread(Context);
	JointBoneRefs.Reset();
	JointStates.Reset();
	SpringChainRanges.Reset();
	PendingBoneWrites.Reset();
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
		|| JointBoneRefs.Num() != Cfg.Joints.Num()
		|| SpringChainRanges.Num() != Cfg.Springs.Num();
}

bool FAnimNode_VRMSpringBones::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	return bEnable && SpringData && SpringData->SpringConfig.IsValid();
}

void FAnimNode_VRMSpringBones::GatherDebugData(FNodeDebugData& DebugData)
{
	Super::GatherDebugData(DebugData);
	DebugData.AddDebugItem(FString::Printf(TEXT("VRMSpringBones: %d writes"), PendingBoneWrites.Num()));
}

/* ---------------------------------------------------------------------------
 *  Mapping / joint state preparation
 * --------------------------------------------------------------------------- */

void FAnimNode_VRMSpringBones::BuildMappings(const FBoneContainer& BoneContainer)
{
	JointBoneRefs.Reset();
	SpringChainRanges.Reset();

	const FVRMSpringConfig& SpringCfg = SpringData->SpringConfig;

	JointBoneRefs.SetNum(SpringCfg.Joints.Num());
	for (int32 J = 0; J < SpringCfg.Joints.Num(); ++J)
	{
		const FName BoneName = (SpringCfg.Joints[J].BoneName.IsNone() && SpringCfg.Joints[J].NodeIndex != INDEX_NONE)
			? SpringData->GetBoneNameForNode(SpringCfg.Joints[J].NodeIndex)
			: SpringCfg.Joints[J].BoneName;

		FBoneReference Ref; Ref.BoneName = BoneName; Ref.Initialize(BoneContainer);
		JointBoneRefs[J] = Ref;
	}

	SpringChainRanges.SetNum(SpringCfg.Springs.Num());
	int32 Cursor = 0;
	for (int32 SIdx = 0; SIdx < SpringCfg.Springs.Num(); ++SIdx)
	{
		const FVRMSpring& Spring = SpringCfg.Springs[SIdx];
		FSpringChainRange SpringChainRng;
		SpringChainRng.First = Cursor;
		SpringChainRng.Num = Spring.JointIndices.Num();
		SpringChainRanges[SIdx] = SpringChainRng;
		Cursor += SpringChainRng.Num;
	}

	// States hold rest data (bone axis, length, tail) derived from which joints have bones. If that
	// changed (another asset, an edit, or an LOD change), rebuild every state from the current pose
	// before simulating, rather than simulate a joint whose state was never set up.
	TArray<bool> BoneValid;
	BoneValid.SetNum(JointBoneRefs.Num());
	for (int32 J = 0; J < JointBoneRefs.Num(); ++J)
	{
		BoneValid[J] = JointBoneRefs[J].HasValidSetup();
	}
	const bool bSameData = SpringData == BuiltForData && SpringData->EditRevision == BuiltForEditRevision && SpringData->SourceHash == BuiltForSourceHash;
	if (!bSameData || BoneValid != BuiltBoneValid || JointStates.Num() != JointBoneRefs.Num())
	{
		JointStates.Reset();
	}
	BuiltBoneValid = MoveTemp(BoneValid);
	BuiltForData = SpringData;
	BuiltForEditRevision = SpringData->EditRevision;
	BuiltForSourceHash = SpringData->SourceHash;
	LastOutBoneTransforms.Reset(); // compact pose indices may have changed
	bEvalCalledThisFrame = false;
}

void FAnimNode_VRMSpringBones::EnsureStatesInitialized(const FBoneContainer& BoneContainer, FCSPose<FCompactPose>& CSPose)
{
	if (JointStates.Num() == JointBoneRefs.Num()) return;

	const FVRMSpringConfig& SpringCfg = SpringData->SpringConfig;

	TMap<int32,int32> JointToSpring;
	for (int32 SpringIdx = 0; SpringIdx < SpringChainRanges.Num() && SpringIdx < SpringCfg.Springs.Num(); ++SpringIdx)
	{
		const TArray<int32>& Indices = SpringCfg.Springs[SpringIdx].JointIndices;
		for (int32 i = 0; i < SpringChainRanges[SpringIdx].Num && i < Indices.Num(); ++i)
		{
			if (JointBoneRefs.IsValidIndex(Indices[i]))
			{
				JointToSpring.Add(Indices[i], SpringIdx);
			}
		}
	}

	JointStates.SetNum(JointBoneRefs.Num());

	for (int32 JointIdx = 0; JointIdx < JointBoneRefs.Num(); ++JointIdx)
	{
		const FBoneReference& BoneRef = JointBoneRefs[JointIdx];
		if (!BoneRef.HasValidSetup()) continue;

		FVRMSimJointState& JointState = JointStates[JointIdx];
		const FCompactPoseBoneIndex BoneIdx = BoneRef.GetCompactPoseIndex(BoneContainer);

		const FTransform BoneCS = CSPose.GetComponentSpaceTransform(BoneIdx);

		// Parent
		FTransform ParentCS = FTransform::Identity;
		const FCompactPoseBoneIndex ParentIdx = BoneContainer.GetParentBoneIndex(BoneIdx);
		if (ParentIdx.IsValid())
		{
			ParentCS = CSPose.GetComponentSpaceTransform(ParentIdx);
		}

		const FTransform LocalRest = BoneCS.GetRelativeTransform(ParentCS);

		const FVector HeadCS = BoneCS.GetLocation();

		FVector ChildCS;
		bool bHasRealChild = false;
		if (const int32* SpringIdxPtr = JointToSpring.Find(JointIdx))
		{
			const FVRMSpring& Spring = SpringCfg.Springs[*SpringIdxPtr];
			const int32 ChainNum = FMath::Min(SpringChainRanges[*SpringIdxPtr].Num, Spring.JointIndices.Num());
			for (int32 i=0;i<ChainNum-1;++i)
			{
				if (Spring.JointIndices[i] == JointIdx)
				{
					const int32 ChildJoint = Spring.JointIndices[i+1];
					if (JointBoneRefs.IsValidIndex(ChildJoint))
					{
						const FBoneReference& ChildBoneRef = JointBoneRefs[ChildJoint];
						if (ChildBoneRef.HasValidSetup())
						{
							const FCompactPoseBoneIndex ChildIdx = ChildBoneRef.GetCompactPoseIndex(BoneContainer);
							ChildCS = CSPose.GetComponentSpaceTransform(ChildIdx).GetLocation();
							bHasRealChild = true;
						}
					}
					break;
				}
			}
		}

		if (bHasRealChild)
		{
			const FVector AxisCS = (ChildCS - HeadCS).GetSafeNormal();
			const FQuat LocalInv = LocalRest.GetRotation().Inverse();
			JointState.BoneAxisLocal = LocalInv.RotateVector(AxisCS).GetSafeNormal();
			JointState.WorldBoneLength = (ChildCS - HeadCS).Length();
			JointState.InitialLocalChildPos = LocalRest.InverseTransformPosition(ChildCS - ParentCS.GetLocation());
		}
		else
		{
			// Derive a reasonable axis from the parent->this direction if possible; fallback to this joint's rest forward.
			const float VirtualTailLengthCm = 7.0f; // 7 cm in UE units (centimeters)

			FVector AxisCS = FVector::ZeroVector;
			// If we have a valid parent bone transform, use the incoming chain segment direction
			if (ParentIdx.IsValid())
			{
				const FVector ParentHeadCS = ParentCS.GetLocation();
				AxisCS = (HeadCS - ParentHeadCS).GetSafeNormal();
			}
			// Fallback: use this bone's rest rotation forward (+X in local space rotated into component space)
			if (AxisCS.IsNearlyZero())
			{
				AxisCS = BoneCS.GetRotation().RotateVector(FVector(1,0,0)).GetSafeNormal();
			}
			if (AxisCS.IsNearlyZero())
			{
				AxisCS = FVector(1,0,0); // Final safety fallback
			}

			// Convert component-space axis into the joint's local space (same pattern as real-child branch)
			const FQuat LocalInv = LocalRest.GetRotation().Inverse();
			JointState.BoneAxisLocal = LocalInv.RotateVector(AxisCS).GetSafeNormal();
			if (!JointState.BoneAxisLocal.IsNormalized())
			{
				JointState.BoneAxisLocal = FVector(1,0,0);
			}

			JointState.WorldBoneLength = VirtualTailLengthCm;

			// Build a virtual child position in component space to derive InitialLocalChildPos consistently.
			const FVector VirtualChildCS = HeadCS + AxisCS * VirtualTailLengthCm;
			// Child (or virtual child) expressed relative to parent CS then into this bone's local space:
			JointState.InitialLocalChildPos = LocalRest.InverseTransformPosition(VirtualChildCS - ParentCS.GetLocation());
		}

		JointState.CurrentTail = HeadCS + BoneCS.GetRotation().RotateVector(FVector(JointState.InitialLocalChildPos));
		JointState.PrevTail    = JointState.CurrentTail;
	}
}

/* ---------------------------------------------------------------------------
 *  Simulation
 * --------------------------------------------------------------------------- */

void FAnimNode_VRMSpringBones::SimulateSpringsOnce(FAnimInstanceProxy* Proxy,
                                                   FCSPose<FCompactPose>& CSPose,
                                                   const FTransform& ComponentTM,
                                                   const float DeltaTime)
{
	const FVRMSpringConfig& SpringCfg = SpringData->SpringConfig;
	PendingBoneWrites.Reset();

	const FBoneContainer& BoneContainer = CSPose.GetPose().GetBoneContainer();

	for (int32 SpringIdx = 0; SpringIdx < SpringChainRanges.Num() && SpringIdx < SpringCfg.Springs.Num(); ++SpringIdx)
	{
		const FVRMSpring& Spring = SpringCfg.Springs[SpringIdx];
		const int32 ChainNum = FMath::Min(SpringChainRanges[SpringIdx].Num, Spring.JointIndices.Num());
		if (ChainNum <= 0) continue;

		const bool  bHasColliders  = Spring.ColliderGroupIndices.Num() > 0;
		const FVector ExternalVelCS = ComponentTM.InverseTransformVector(ExternalVelocity) * ExternalVelocityScale;

		int32 PrevSimulatedJoint = INDEX_NONE; // previous joint of this chain that was simulated this pass
		for (int32 ChainPos = 0; ChainPos < ChainNum; ++ChainPos)
		{
			const int32 JointIndex = Spring.JointIndices[ChainPos];
			if (!JointBoneRefs.IsValidIndex(JointIndex) || !JointStates.IsValidIndex(JointIndex) || !SpringCfg.Joints.IsValidIndex(JointIndex)) continue;

			// Parameters are per joint (VRM 1.0); VRM 0.x joints carry their bone group's values.
			const FVRMSpringJoint& Joint = SpringCfg.Joints[JointIndex];
			const float Stiffness        = Joint.Stiffness;
			const float Drag             = FMath::Clamp(Joint.Drag, 0.f, 1.f);
			const float DefaultHitRadius = FMath::Max(0.f, Joint.HitRadius); // cm
			const FVector ExternalVel    = ExternalVelCS * DeltaTime * (1.f - Drag);
			const FVector Gravity        = Joint.GravityDir * Joint.GravityPower * DeltaTime; // cm scale

			const FBoneReference& JointBoneRef = JointBoneRefs[JointIndex];
			if (!JointBoneRef.HasValidSetup()) continue;

			// A joint follows the previous joint of its chain only if that one was simulated; after a
			// skipped joint (no bone at this LOD, bad index) it starts from its own animated head.
			const bool bIsSpringRoot = (PrevSimulatedJoint == INDEX_NONE) || (ChainPos > 0 && Spring.JointIndices[ChainPos - 1] != PrevSimulatedJoint);
			FVRMSimJointState& JointState = JointStates[JointIndex];
			FVRMSimJointState& ParentState = bIsSpringRoot ? JointState : JointStates[PrevSimulatedJoint];
			PrevSimulatedJoint = JointIndex;
			const FCompactPoseBoneIndex JointBoneIdx = JointBoneRef.GetCompactPoseIndex(BoneContainer);

			FTransform ParentBoneCS = FTransform::Identity;
			const FCompactPoseBoneIndex ParentBoneIdx = BoneContainer.GetParentBoneIndex(JointBoneIdx);
			if (ParentBoneIdx.IsValid())
			{
				ParentBoneCS = CSPose.GetComponentSpaceTransform(ParentBoneIdx);
			}

			const FTransform JointBoneCS = CSPose.GetComponentSpaceTransform(JointBoneIdx);

			InitializeState(JointState, ComponentTM, JointBoneCS);

			const FVector CurrentHeadPosCS = bIsSpringRoot ? JointBoneCS.GetTranslation() : ParentState.PrevTail;

			FVector Inertia = (JointState.CurrentTail - JointState.PrevTail) * (1.f - Drag);
			const FQuat AnimatedBoneRotCS = JointBoneCS.GetRotation();
			const FVector RestTargetCS = CurrentHeadPosCS + AnimatedBoneRotCS.RotateVector(JointState.InitialLocalChildPos);

			FVector SimTailPos = JointState.CurrentTail + Inertia + Gravity + ExternalVel;
			
			if (!RestTargetCS.ContainsNaN())
			{
				const float StiffScale = Stiffness; // * DeltaTime; // NOTE: Removed DeltaTime scaling to match typical spring behavior
				SimTailPos += (RestTargetCS - SimTailPos) * StiffScale;
			}

			JointState.PrevHeadCS = CurrentHeadPosCS;

			const FVector PostSimHeadPos = JointState.PrevHeadCS;
			const FQuat   BoneRotCS = JointBoneCS.GetRotation();

			FVector PostSimTailPositionFixed = ApplyLengthConstraint(JointState, SimTailPos, PostSimHeadPos);

			if (bHasColliders)
			{
				const float JointHitRadius = (JointState.WorldBoneLength <= KINDA_SMALL_NUMBER)
					? DefaultHitRadius
					: FMath::Min(DefaultHitRadius, JointState.WorldBoneLength * 0.5f);
				FVector NextTailWS = ComponentTM.TransformPosition(PostSimTailPositionFixed);
				ResolveCollisions(Proxy, NextTailWS, JointHitRadius, SpringCfg, CSPose, ComponentTM, Spring.ColliderGroupIndices);
				PostSimTailPositionFixed = ComponentTM.InverseTransformPosition(NextTailWS);
			}

			const FVector PostCollideHeadPos = JointState.PrevHeadCS;
			FVector PostCollideTailPositionFixed = ApplyLengthConstraint(JointState, PostSimTailPositionFixed, PostCollideHeadPos);

			JointState.PrevTail = JointState.CurrentTail;
			JointState.CurrentTail = PostCollideTailPositionFixed;

			const FVector AxisCS = BoneRotCS.RotateVector(JointState.BoneAxisLocal);
			const FVector TargetDir = (PostCollideTailPositionFixed - PostCollideHeadPos).GetSafeNormal();
			const bool bLenNearlyZero = (PostCollideTailPositionFixed - PostCollideHeadPos).IsNearlyZero();
			const FQuat PostCollideBoneRotCS = bLenNearlyZero ? BoneRotCS : FQuat::FindBetweenVectors(AxisCS, TargetDir) * BoneRotCS;

			// Compute joint radius for drawing (matches collision calculation)
			const float JointHitRadiusForDraw = (JointState.WorldBoneLength <= KINDA_SMALL_NUMBER)
				? DefaultHitRadius
				: FMath::Min(DefaultHitRadius, JointState.WorldBoneLength * 0.5f);

			// Debug draw per-joint
			VRMSB_DRAW_SPRING(Proxy, ComponentTM, JointState, PostCollideHeadPos, PostCollideTailPositionFixed, JointHitRadiusForDraw, RestTargetCS, DeltaTime);

			PendingBoneWrites.Add({ JointBoneIdx, PostCollideHeadPos, PostCollideBoneRotCS });
		}
	}
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
	if (FMath::IsNearlyZero(CurrentDeltaTime)) return;

	const FBoneContainer& BoneContainer = CSPose.GetPose().GetBoneContainer();

	// SpringData can be swapped or edited while running; rebuild before touching any array (SR-05).
	if (MappingsAreStale())
	{
		BuildMappings(BoneContainer);
	}
	if (bResetRequested)
	{
		bResetRequested = false;
		JointStates.Reset();
	}

	EnsureStatesInitialized(BoneContainer, CSPose);
	PendingBoneWrites.Reset();

	const float Dt = bPauseSimulation ? 0.f : CurrentDeltaTime;
	SimulateSpringsOnce(Proxy, CSPose, ComponentTM, Dt);

	OutBoneTransforms.Reset();
	OutBoneTransforms.Reserve(PendingBoneWrites.Num());
	bEvalCalledThisFrame = true;
	LastOutBoneTransforms.Reset();
	if (PendingBoneWrites.Num() == 0) return;

	PendingBoneWrites.Sort([](const FBoneWrite& A, const FBoneWrite& B)
	{
		return A.BoneIndex.GetInt() < B.BoneIndex.GetInt();
	});

	for (const FBoneWrite& BW : PendingBoneWrites)
	{
		FTransform NewCS = FTransform(BW.NewRotation, BW.NewPosition, FVector(1, 1, 1));
		OutBoneTransforms.Add(FBoneTransform(BW.BoneIndex, NewCS));
	}

	if (OutBoneTransforms.Num() > 1)
	{
		OutBoneTransforms.Sort(FCompareBoneTransformIndex());
	}
	LastOutBoneTransforms = OutBoneTransforms;
}

/* ---------------------------------------------------------------------------
 *  Debug drawing
 * --------------------------------------------------------------------------- */
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
void FAnimNode_VRMSpringBones::DrawCollisionSphere(FAnimInstanceProxy* Proxy, const FTransform& NodeXf, const FVRMSpringColliderSphere& S) const
{
	if (!Proxy) return;

	const FVector Center = NodeXf.TransformPosition(S.Offset);
	const float Radius = S.Radius;

	if (Radius <= 0.f)
	{
		Proxy->AnimDrawDebugSphere(Center, 1.f, 8, FColor::Yellow, false, -1.f, 0.25f, SDPG_World);
		return;
	}
	Proxy->AnimDrawDebugSphere(Center, Radius, 12, FColor::Green, false, -1.f, 0.25f, SDPG_World);
}

void FAnimNode_VRMSpringBones::DrawCollisionCapsule(FAnimInstanceProxy* Proxy, const FTransform& NodeXf, const FVRMSpringColliderCapsule& Cap) const
{
	if (!Proxy) return;

	const FVector P0 = NodeXf.TransformPosition(Cap.Offset);
	const FVector P1 = NodeXf.TransformPosition(Cap.TailOffset);
	const float Radius = Cap.Radius;

	const float SegmentLen = (P1 - P0).Size();
	const float CylinderLen = FMath::Max(0.f, SegmentLen - 2.f * Radius);
	const float HalfHeight = CylinderLen * 0.5f;
	const FVector Center = (P0 + P1) * 0.5f;

	FVector Dir = (P1 - P0).GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		Proxy->AnimDrawDebugSphere(Center, Radius, 12, FColor::Green, false, -1.f, 0.25f, SDPG_World);
		return;
	}

	const FRotator Rotation = FRotator(FRotationMatrix::MakeFromZ(Dir).ToQuat());
	Proxy->AnimDrawDebugCapsule(Center, HalfHeight, Radius, Rotation, FColor::Green, false, -1.f, 0.25f, SDPG_World);
}

void FAnimNode_VRMSpringBones::DrawCollisionPlane(FAnimInstanceProxy* Proxy, const FTransform& NodeXf, const FVRMSpringColliderPlane& P) const
{
	if (!Proxy) return;

	const FVector Center = NodeXf.TransformPosition(P.Offset);
	FVector NormalWS = NodeXf.TransformVectorNoScale(P.Normal).GetSafeNormal();
	if (NormalWS.IsNearlyZero()) NormalWS = FVector(0, 0, 1);

	const float HalfSize = 2500.f; // purely for debug viz (25m square), in cm

	FVector Tangent = FVector::CrossProduct(NormalWS, FVector(0, 1, 0));
	if (Tangent.IsNearlyZero()) Tangent = FVector::CrossProduct(NormalWS, FVector(1, 0, 0));
	Tangent.Normalize();
	const FVector Bitangent = FVector::CrossProduct(NormalWS, Tangent).GetSafeNormal();

	const FVector C0 = Center + (Tangent * HalfSize) + (Bitangent * HalfSize);
	const FVector C1 = Center + (Tangent * HalfSize) - (Bitangent * HalfSize);
	const FVector C2 = Center - (Tangent * HalfSize) - (Bitangent * HalfSize);
	const FVector C3 = Center - (Tangent * HalfSize) + (Bitangent * HalfSize);

	const FColor PlaneColor = FColor::Blue;
	const float LifeTime = 0.f;
	const uint8 DepthPriority = 0;
	const float Thickness = 2.f;
	Proxy->AnimDrawDebugLine(C0, C1, PlaneColor, false, LifeTime, Thickness, SDPG_World);
	Proxy->AnimDrawDebugLine(C1, C2, PlaneColor, false, LifeTime, Thickness, SDPG_World);
	Proxy->AnimDrawDebugLine(C2, C3, PlaneColor, false, LifeTime, Thickness, SDPG_World);
	Proxy->AnimDrawDebugLine(C3, C0, PlaneColor, false, LifeTime, Thickness, SDPG_World);

	const float ArrowSize = FMath::Max(50.f, HalfSize * 0.25f);
	Proxy->AnimDrawDebugDirectionalArrow(Center, Center + NormalWS * ArrowSize, ArrowSize * 0.25f, PlaneColor, false, LifeTime, 2.f, SDPG_World);
}

// Draw debug visuals for a single spring joint: head (red), tail (yellow sized by joint radius), optional velocity line and animated-rest target (cyan)
void FAnimNode_VRMSpringBones::DrawSpringJoint(FAnimInstanceProxy* Proxy, const FTransform& ComponentTM, const FVRMSimJointState& JointState, const FVector& HeadCS, const FVector& TailCS, float JointRadius, const FVector& RestTargetCS, float DeltaTime) const
{
	if (!Proxy) return;

	const int32 Mode = CVarVRMSB_DrawSprings.GetValueOnAnyThread();
	if (Mode == 0) return;

	const FVector HeadWS = ComponentTM.TransformPosition(HeadCS);
	const FVector TailWS = ComponentTM.TransformPosition(TailCS);
	Proxy->AnimDrawDebugSphere(HeadWS, FMath::Max(1.f, JointRadius * 0.2f), 8, FColor::Red, false, -1.f, 0.25f, SDPG_World);
	// Head: red small sphere

	// Tail: yellow sphere sized to joint radius
	Proxy->AnimDrawDebugSphere(TailWS, FMath::Max(1.f, JointRadius), 12, FColor::Yellow, false, -1.f, 0.25f, SDPG_World);

	// Red line from head to tail
	Proxy->AnimDrawDebugLine(HeadWS, TailWS, FColor::Red, false, -1.f, 0.5f, SDPG_World);

	// Velocity trail when mode >= 2
	if (Mode >= 2 && DeltaTime > KINDA_SMALL_NUMBER)
	{
		// Approximate velocity in world space using CS delta transformed by component TM
		FVector PrevTailCS = JointState.PrevTail;
		FVector PrevTailWS = ComponentTM.TransformPosition(PrevTailCS);
		FVector VelocityWS = (TailWS - PrevTailWS) / DeltaTime; // world units per second
		const float VelScale = 0.05f; // scale so line isn't excessively long
		const FVector End = TailWS + VelocityWS * VelScale;
		// Keep this line around for a short while to create a trail
		const float LifeTime = 1.f; // seconds
		Proxy->AnimDrawDebugLine(TailWS, End, FColor::Magenta, false, LifeTime, 0.f, SDPG_World);
	}

	// Animated target when mode == 3
	if (Mode == 3)
	{
		const FVector TargetWS = ComponentTM.TransformPosition(RestTargetCS);
		Proxy->AnimDrawDebugSphere(TargetWS, FMath::Max(1.f, JointRadius * 0.25f), 8, FColor::Cyan, false, -1.f, 0.15f, SDPG_World);
	}
}
#endif

/* ---------------------------------------------------------------------------
 *  Collision resolution
 * --------------------------------------------------------------------------- */

void FAnimNode_VRMSpringBones::ResolveCollisions(
	FAnimInstanceProxy* Proxy,
	FVector& NextTailWS,
	float JointRadius,
	const FVRMSpringConfig& SpringCfg,
	FCSPose<FCompactPose>& CSPose,
	const FTransform& ComponentTM,
	const TArray<int32>& GroupIndices) const
{
	for (int32 GIdx : GroupIndices)
	{
		if (!SpringCfg.ColliderGroups.IsValidIndex(GIdx)) continue;
		const FVRMSpringColliderGroup& Group = SpringCfg.ColliderGroups[GIdx];

		for (int32 CIdx : Group.ColliderIndices)
		{
			if (!SpringCfg.Colliders.IsValidIndex(CIdx)) continue;
			const FVRMSpringCollider& Col = SpringCfg.Colliders[CIdx];

			FTransform NodeXf = ComponentTM;
			if (!Col.BoneName.IsNone())
			{
				FBoneReference BR; BR.BoneName = Col.BoneName; BR.Initialize(CSPose.GetPose().GetBoneContainer());
				if (BR.HasValidSetup())
				{
					NodeXf = CSPose.GetComponentSpaceTransform(BR.GetCompactPoseIndex(CSPose.GetPose().GetBoneContainer())) * ComponentTM;
				}
			}

			FVector PushDir; float Pen;

			for (const auto& S : Col.Spheres)
			{
				Pen = S.bInside
					? CollideInsideSphere(NodeXf, S, NextTailWS, JointRadius, PushDir)
					: CollideSphere(NodeXf, S, NextTailWS, JointRadius, PushDir);
				if (Pen < 0.f) NextTailWS -= PushDir * Pen;
				VRMSB_DRAW_SPHERE(Proxy, NodeXf, S);
			}
			for (const auto& Cap : Col.Capsules)
			{
				Pen = Cap.bInside
					? CollideInsideCapsule(NodeXf, Cap, NextTailWS, JointRadius, PushDir)
					: CollideCapsule(NodeXf, Cap, NextTailWS, JointRadius, PushDir);
				if (Pen < 0.f) NextTailWS -= PushDir * Pen;
				VRMSB_DRAW_CAPSULE(Proxy, NodeXf, Cap);
			}
			for (const auto& Pl : Col.Planes)
			{
				Pen = CollidePlane(NodeXf, Pl, NextTailWS, JointRadius, PushDir);
				if (Pen < 0.f) NextTailWS -= PushDir * Pen;
				VRMSB_DRAW_PLANE(Proxy, NodeXf, Pl);
			}
		}
	}
}

/* ---------------------------------------------------------------------------
 *  Collision primitive helpers
 * --------------------------------------------------------------------------- */

float FAnimNode_VRMSpringBones::CollideSphere(const FTransform& NodeXf, const FVRMSpringColliderSphere& Sph, const FVector& TailWS, float JointRadius, FVector& OutPushDir) const
{
	const FVector CenterWS = NodeXf.TransformPosition(Sph.Offset);
	const FVector Delta = TailWS - CenterWS;
	const float Distance = Delta.Length() - (Sph.Radius + JointRadius);
	OutPushDir = Delta.GetSafeNormal();
	return Distance;
}

float FAnimNode_VRMSpringBones::CollideInsideSphere(const FTransform& NodeXf, const FVRMSpringColliderSphere& Sph, const FVector& TailWS, float JointRadius, FVector& OutPushDir) const
{
	const FVector CenterWS = NodeXf.TransformPosition(Sph.Offset);
	const FVector Delta = TailWS - CenterWS;
	const float Distance = (Sph.Radius - JointRadius) - Delta.Length();
	OutPushDir = -Delta.GetSafeNormal();
	return Distance;
}

float FAnimNode_VRMSpringBones::CollideCapsule(const FTransform& NodeXf, const FVRMSpringColliderCapsule& Cap, const FVector& TailWS, float JointRadius, FVector& OutPushDir) const
{
	const FVector HeadWS = NodeXf.TransformPosition(Cap.Offset);
	const FVector TailC  = NodeXf.TransformPosition(Cap.TailOffset);
	const FVector AtoB   = TailC - HeadWS;
	FVector Delta = TailWS - HeadWS;
	const float Dot = FVector::DotProduct(AtoB, Delta);
	if (Dot > 0.f)
	{
		const float SegLenSq = AtoB.SizeSquared();
		if (Dot > SegLenSq) { Delta -= AtoB; }
		else { Delta -= AtoB * (Dot / SegLenSq); }
	}
	const float Distance = Delta.Length() - (Cap.Radius + JointRadius);
	OutPushDir = Delta.GetSafeNormal();
	return Distance;
}

float FAnimNode_VRMSpringBones::CollideInsideCapsule(const FTransform& NodeXf, const FVRMSpringColliderCapsule& Cap, const FVector& TailWS, float JointRadius, FVector& OutPushDir) const
{
	const FVector HeadWS = NodeXf.TransformPosition(Cap.Offset);
	const FVector TailC  = NodeXf.TransformPosition(Cap.TailOffset);
	const FVector AtoB   = TailC - HeadWS;
	FVector Delta = TailWS - HeadWS;
	const float Dot = FVector::DotProduct(AtoB, Delta);
	if (Dot > 0.f)
	{
		const float SegLenSq = AtoB.SizeSquared();
		if (Dot > SegLenSq) { Delta -= AtoB; }
		else { Delta -= AtoB * (Dot / SegLenSq); }
	}
	const float Distance = (Cap.Radius - JointRadius) - Delta.Length();
	OutPushDir = -Delta.GetSafeNormal();
	return Distance;
}

float FAnimNode_VRMSpringBones::CollidePlane(const FTransform& NodeXf, const FVRMSpringColliderPlane& P, const FVector& TailWS, float JointRadius, FVector& OutPushDir) const
{
	const FVector OffsetWS = NodeXf.TransformPosition(P.Offset);
	FVector NormalWS = NodeXf.TransformVectorNoScale(P.Normal).GetSafeNormal();
	if (NormalWS.IsNearlyZero()) NormalWS = FVector(0,0,1);
	const FVector Delta = TailWS - OffsetWS;
	const float Distance = FVector::DotProduct(Delta, NormalWS) - JointRadius;
	OutPushDir = NormalWS;
	return Distance;
}

#undef LOCTEXT_NAMESPACE
