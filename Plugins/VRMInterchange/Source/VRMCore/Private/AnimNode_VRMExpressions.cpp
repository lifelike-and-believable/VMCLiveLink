// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "AnimNode_VRMExpressions.h"
#include "VRMAvatarDescription.h"
#include "VRMAvatarParser.h"

namespace
{
	// How much an expression's override reduces its group: block is all or nothing while the
	// expression is active, blend is its weight (three-vrm's overrideBlinkAmount and friends).
	float OverrideAmount(EVRMExpressionOverride Override, float Weight)
	{
		switch (Override)
		{
		case EVRMExpressionOverride::Block: return Weight > 0.f ? 1.f : 0.f;
		case EVRMExpressionOverride::Blend: return Weight;
		default: return 0.f;
		}
	}
}

void FAnimNode_VRMExpressions::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_Base::Initialize_AnyThread(Context);
	Source.Initialize(Context);
}

void FAnimNode_VRMExpressions::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	Source.CacheBones(Context);
}

void FAnimNode_VRMExpressions::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	GetEvaluateGraphExposedInputs().Execute(Context);
	Source.Update(Context);
}

void FAnimNode_VRMExpressions::Evaluate_AnyThread(FPoseContext& Output)
{
	Source.Evaluate(Output);
	ApplyExpressions(Output.Curve);
}

void FAnimNode_VRMExpressions::GatherDebugData(FNodeDebugData& DebugData)
{
	FString DebugLine = DebugData.GetNodeName(this);
	DebugLine += FString::Printf(TEXT("(Avatar: %s, active expressions: %d)"),
		AvatarDescription ? *AvatarDescription->GetName() : TEXT("none"), LastActiveCount);
	DebugData.AddDebugItem(DebugLine);
	Source.GatherDebugData(DebugData);
}

bool FAnimNode_VRMExpressions::IsStale() const
{
	const UVRMAvatarDescription* Description = AvatarDescription.Get();
	return Description != BuiltFor
		|| (Description && (Description->Avatar.Expressions.Num() != BuiltForExpressionCount || Description->SourceHash != BuiltForSourceHash));
}

void FAnimNode_VRMExpressions::Rebuild()
{
	Expressions.Reset();
	Binds.Reset();
	MorphNames.Reset();
	InputToExpression.Reset();

	const UVRMAvatarDescription* Description = AvatarDescription.Get();
	BuiltFor = Description;
	BuiltForSourceHash = Description ? Description->SourceHash : FString();
	BuiltForExpressionCount = Description ? Description->Avatar.Expressions.Num() : INDEX_NONE;
	if (!Description)
	{
		return;
	}

	const TArray<FVRMExpression>& FileExpressions = Description->Avatar.Expressions;
	TMap<FName, int32> MorphIndex;
	for (const FVRMExpression& Expression : FileExpressions)
	{
		FBuiltExpression& Built = Expressions.AddDefaulted_GetRef();
		Built.bIsBinary = Expression.bIsBinary;
		Built.OverrideBlink = Expression.OverrideBlink;
		Built.OverrideLookAt = Expression.OverrideLookAt;
		Built.OverrideMouth = Expression.OverrideMouth;
		switch (Expression.Preset)
		{
		case EVRMExpressionPreset::Blink: case EVRMExpressionPreset::BlinkLeft: case EVRMExpressionPreset::BlinkRight:
			Built.Group = EGroup::Blink; break;
		case EVRMExpressionPreset::LookUp: case EVRMExpressionPreset::LookDown: case EVRMExpressionPreset::LookLeft: case EVRMExpressionPreset::LookRight:
			Built.Group = EGroup::LookAt; break;
		case EVRMExpressionPreset::Aa: case EVRMExpressionPreset::Ih: case EVRMExpressionPreset::Ou: case EVRMExpressionPreset::Ee: case EVRMExpressionPreset::Oh:
			Built.Group = EGroup::Mouth; break;
		default:
			break;
		}

		Built.FirstBind = Binds.Num();
		for (const FVRMMorphBind& Bind : Expression.MorphBinds)
		{
			if (Bind.MorphTarget.IsNone())
			{
				continue;
			}
			int32& Morph = MorphIndex.FindOrAdd(Bind.MorphTarget, INDEX_NONE);
			if (Morph == INDEX_NONE)
			{
				Morph = MorphNames.Add(Bind.MorphTarget);
			}
			Binds.Add({ Morph, Bind.Weight });
		}
		Built.NumBinds = Binds.Num() - Built.FirstBind;
	}

	// Input names: every expression's own name first, so a custom expression named like a preset
	// keeps its name; then the presets' names in both versions.
	for (int32 i = 0; i < FileExpressions.Num(); ++i)
	{
		if (!FileExpressions[i].Name.IsNone() && !InputToExpression.Contains(FileExpressions[i].Name))
		{
			InputToExpression.Add(FileExpressions[i].Name, i);
		}
	}
	for (int32 i = 0; i < FileExpressions.Num(); ++i)
	{
		if (FileExpressions[i].Preset == EVRMExpressionPreset::Custom)
		{
			continue;
		}
		for (const EVRMAvatarVersion Version : { EVRMAvatarVersion::VRM1, EVRMAvatarVersion::VRM0 })
		{
			const FName Name(*VRM::ExpressionPresetName(FileExpressions[i].Preset, Version));
			if (!InputToExpression.Contains(Name))
			{
				InputToExpression.Add(Name, i);
			}
		}
	}
}

void FAnimNode_VRMExpressions::ApplyExpressions(FBlendedCurve& Curve)
{
	if (IsStale())
	{
		Rebuild();
	}
	LastActiveCount = 0;
	if (Expressions.Num() == 0)
	{
		return;
	}

	// Input weights. FName comparison ignores case, so "Joy", "joy" and "JOY" are the same curve.
	Weights.Init(0.f, Expressions.Num());
	HasInput.Init(false, Expressions.Num());
	Curve.ForEachElement([this](const auto& Element)
	{
		if (const int32* Index = InputToExpression.Find(Element.Name))
		{
			Weights[*Index] = HasInput[*Index] ? FMath::Max(Weights[*Index], Element.Value) : Element.Value;
			HasInput[*Index] = true;
		}
	});

	float BlinkMultiplier = 1.f;
	float LookAtMultiplier = 1.f;
	float MouthMultiplier = 1.f;
	const float ClampedAlpha = FMath::Clamp(Alpha, 0.f, 1.f);
	for (int32 i = 0; i < Expressions.Num(); ++i)
	{
		if (!HasInput[i])
		{
			continue;
		}
		++LastActiveCount;
		float& Weight = Weights[i];
		Weight = FMath::Clamp(Weight * ClampedAlpha, 0.f, 1.f);
		if (Expressions[i].bIsBinary)
		{
			Weight = Weight > 0.5f ? 1.f : 0.f;
		}
		BlinkMultiplier -= OverrideAmount(Expressions[i].OverrideBlink, Weight);
		LookAtMultiplier -= OverrideAmount(Expressions[i].OverrideLookAt, Weight);
		MouthMultiplier -= OverrideAmount(Expressions[i].OverrideMouth, Weight);
	}
	if (LastActiveCount == 0)
	{
		return;
	}
	BlinkMultiplier = FMath::Max(BlinkMultiplier, 0.f);
	LookAtMultiplier = FMath::Max(LookAtMultiplier, 0.f);
	MouthMultiplier = FMath::Max(MouthMultiplier, 0.f);

	MorphValues.Init(0.f, MorphNames.Num());
	MorphDriven.Init(false, MorphNames.Num());
	for (int32 i = 0; i < Expressions.Num(); ++i)
	{
		if (!HasInput[i])
		{
			continue;
		}
		const FBuiltExpression& Expression = Expressions[i];
		float Weight = Weights[i];
		switch (Expression.Group)
		{
		case EGroup::Blink: Weight *= BlinkMultiplier; break;
		case EGroup::LookAt: Weight *= LookAtMultiplier; break;
		case EGroup::Mouth: Weight *= MouthMultiplier; break;
		default: break;
		}
		for (int32 b = Expression.FirstBind; b < Expression.FirstBind + Expression.NumBinds; ++b)
		{
			MorphValues[Binds[b].Morph] += Weight * Binds[b].Weight;
			MorphDriven[Binds[b].Morph] = true;
		}
	}

	for (int32 m = 0; m < MorphNames.Num(); ++m)
	{
		if (MorphDriven[m])
		{
			Curve.Set(MorphNames[m], MorphValues[m]);
		}
	}
}
