// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimNodeBase.h"
#include "VRMAvatarTypes.h"
#include "AnimNode_VRMExpressions.generated.h"

class UVRMAvatarDescription;

/**
 * VRM Expressions (P4.2). Reads expression weights from curves on the input pose (for example
 * "Joy" or "happy" from a VMC Live Link subject) and writes the morph target curves the avatar's
 * expressions bind, following VRM 1.0 (the same rules as three-vrm):
 *  - a curve drives an expression when its name is the expression's name, or the expression is a
 *    preset and the curve has the preset's VRM 1.0 or 0.x name (happy or joy, aa or a, ...), so
 *    either sender version drives either avatar version;
 *  - binary expressions round their weight to 0 or 1;
 *  - an active expression with overrideBlink, overrideLookAt or overrideMouth reduces the blink,
 *    look-at or mouth presets (block: to 0 while it is active; blend: by its weight);
 *  - each morph target gets the sum of weight x bind weight over the expressions that bind it.
 * Only morph targets whose expressions had an input curve this frame are written, so a morph the
 * input doesn't mention keeps whatever the input pose gave it. Other curves pass through.
 */
USTRUCT(BlueprintInternalUseOnly)
struct VRMCORE_API FAnimNode_VRMExpressions : public FAnimNode_Base
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Links")
	FPoseLink Source;

	/** The avatar whose expressions are expanded: the <Mesh>_Avatar asset its import made. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expressions", meta = (PinShownByDefault))
	TObjectPtr<UVRMAvatarDescription> AvatarDescription = nullptr;

	/** Scales every expression weight before binary rounding and overrides. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expressions", meta = (PinHiddenByDefault, ClampMin = "0.0", ClampMax = "1.0"))
	float Alpha = 1.f;

	// FAnimNode_Base
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;

	/** What Evaluate does to the input pose's curves, without the anim graph, so tests can run it. */
	void ApplyExpressions(FBlendedCurve& Curve);

	/** How many expressions had an input curve in the last ApplyExpressions. */
	int32 GetActiveExpressionCount() const { return LastActiveCount; }

private:
	/** Which preset group an override reduces. */
	enum class EGroup : uint8 { None, Blink, LookAt, Mouth };

	struct FBuiltExpression
	{
		EGroup Group = EGroup::None;
		bool bIsBinary = false;
		EVRMExpressionOverride OverrideBlink = EVRMExpressionOverride::None;
		EVRMExpressionOverride OverrideLookAt = EVRMExpressionOverride::None;
		EVRMExpressionOverride OverrideMouth = EVRMExpressionOverride::None;
		int32 FirstBind = 0;
		int32 NumBinds = 0;
	};

	struct FBuiltBind
	{
		int32 Morph = INDEX_NONE; // index into MorphNames
		float Weight = 0.f;
	};

	// Rebuild the lookups when the asset changed (another asset, or a reimport updated it).
	bool IsStale() const;
	void Rebuild();

	TArray<FBuiltExpression> Expressions;
	TArray<FBuiltBind> Binds;
	TArray<FName> MorphNames;
	TMap<FName, int32> InputToExpression;

	// Per-evaluation scratch
	TArray<float> Weights;
	TArray<bool> HasInput;
	TArray<float> MorphValues;
	TArray<bool> MorphDriven;

	const UVRMAvatarDescription* BuiltFor = nullptr;
	FString BuiltForSourceHash;
	int32 BuiltForExpressionCount = INDEX_NONE;
	int32 LastActiveCount = 0;
};
