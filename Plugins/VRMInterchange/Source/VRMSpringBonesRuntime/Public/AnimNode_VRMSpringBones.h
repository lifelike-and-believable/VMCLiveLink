// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "VRMSpringBoneData.h"
#include "VRMSpringSolver.h"
#include "AnimNode_VRMSpringBones.generated.h"

/** Where spring tails are simulated, for springs without a center bone. */
UENUM(BlueprintType)
enum class EVRMSpringSimulationSpace : uint8
{
	/** Moving or turning the character swings the springs (the VRM reference behaviour). */
	World,
	/** Only the animation moves the springs; the character's motion adds nothing. */
	Component,
};

/**
 * Spring bone anim node (VRM multi-chain). An adapter over FVRMSpringSolver: it maps the spring data
 * to pose bones, feeds the solver the animated pose, and writes back the joint rotations.
 */
USTRUCT(BlueprintInternalUseOnly)
struct VRMSPRINGBONESRUNTIME_API FAnimNode_VRMSpringBones : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()

public:
	/** Master enable switch */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring", meta=(PinShownByDefault))
	bool bEnable = true;

	/** Optional runtime pause (debug) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring|Debug", meta=(PinShownByDefault))
	bool bPauseSimulation = false;

	/** Spring configuration asset (contains joints, springs, colliders) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring", meta=(PinShownByDefault))
	TObjectPtr<UVRMSpringBoneData> SpringData = nullptr;

	/** Extra velocity (world space, cm/s) applied to every spring tail, e.g. to approximate character movement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring", meta = (PinShownByDefault))
	FVector ExternalVelocity = FVector(0.f, 0.f, 0.f);

	/** Multiplier applied to ExternalVelocity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring", meta = (PinShownByDefault))
	float ExternalVelocityScale = 1.f;

	/** Where tails are simulated. Springs with a center bone always use that bone's space. */
	UPROPERTY(EditAnywhere, Category = "Spring|Simulation")
	EVRMSpringSimulationSpace SimulationSpace = EVRMSpringSimulationSpace::World;

	/** Simulation steps per second. The result doesn't depend on the frame rate; higher is smoother and costs more. */
	UPROPERTY(EditAnywhere, Category = "Spring|Simulation", meta = (ClampMin = "10", ClampMax = "480", UIMin = "30", UIMax = "240", Units = "Hz"))
	float SubstepHz = 60.f;

	/** Longest frame simulated. A longer frame (a hitch) is simulated as this long, which caps the steps per frame. */
	UPROPERTY(EditAnywhere, Category = "Spring|Simulation", meta = (ClampMin = "0.0", ClampMax = "1.0", Units = "s"))
	float MaxDeltaTime = 0.1f;

	/** Draw this node's colliders (also vrm.SpringBones.DrawColliders). Not in shipping builds. */
	UPROPERTY(EditAnywhere, Category = "Spring|Debug")
	bool bDrawColliders = false;

	/** Draw this node's joints, head to tail (also vrm.SpringBones.DrawSprings). Not in shipping builds. */
	UPROPERTY(EditAnywhere, Category = "Spring|Debug")
	bool bDrawSprings = false;


	// FAnimNode_Base / SkeletalControl overrides
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	virtual bool NeedsDynamicReset() const override { return true; }
	virtual void ResetDynamics(ETeleportType InTeleportType) override;

	/**
	 * The per-frame work, without the anim graph plumbing, so tests can run the node on a pose they
	 * build themselves. BeginFrame is what UpdateInternal does (new delta time, new frame);
	 * RebuildForBones is what CacheBones does; EvaluateInternal is one evaluation. Proxy is only used
	 * for debug drawing and may be null.
	 */
	void BeginFrame(float DeltaTime);
	void RebuildForBones(const FBoneContainer& BoneContainer);
	void EvaluateInternal(FAnimInstanceProxy* Proxy, FCSPose<FCompactPose>& CSPose, const FTransform& ComponentTM, TArray<FBoneTransform>& OutBoneTransforms);

private:
	// Build the solver's bones, chains and colliders from SpringData for these bones
	void BuildMappings(const FBoneContainer& BoneContainer);

	// True when the mappings no longer describe SpringData (another asset, or the asset was edited)
	bool MappingsAreStale() const;

	FVRMSpringSolverSettings MakeSolverSettings() const;

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	void DrawDebug(FAnimInstanceProxy* Proxy, const FTransform& ComponentTM, float DeltaTime) const;
#endif

	/* ---- Runtime data ---- */
	FVRMSpringSolver Solver;
	TArray<FCompactPoseBoneIndex> SolverBones; // pose bone of each solver bone
	TArray<FTransform> SolverBonesCS;           // per evaluation scratch

	// What the mappings were built from, so a new asset or an edit rebuilds them (SR-05)
	const UVRMSpringBoneData* BuiltForData = nullptr;
	FString BuiltForSourceHash;
	int32   BuiltForEditRevision = INDEX_NONE;
	int32   BuiltForJointCount = 0;
	int32   BuiltForSpringCount = 0;
	TArray<bool> BuiltBoneValid; // which named bones this LOD has, for spotting LOD changes
	const UVRMSpringBoneData* WarnedMissingBonesFor = nullptr; // warn once per asset

	// Output of the last evaluation this frame, re-emitted if the node is evaluated again (SR-04)
	TArray<FBoneTransform> LastOutBoneTransforms;

	float CurrentDeltaTime = 0.f;
	bool  bEvalCalledThisFrame = false;
	bool  bResetRequested = false; // set by ResetDynamics, consumed by the next evaluation
};
