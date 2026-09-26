// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMAvatarDescription.h"
#include "Engine/SkeletalMesh.h"

FName UVRMAvatarDescription::GetBone(EVRMHumanBone Bone) const
{
	const FName* Found = Avatar.HumanoidToBone.Find(Bone);
	return Found ? *Found : NAME_None;
}

const FVRMExpression* UVRMAvatarDescription::FindExpression(EVRMExpressionPreset Preset) const
{
	return Preset == EVRMExpressionPreset::Custom ? nullptr
		: Avatar.Expressions.FindByPredicate([Preset](const FVRMExpression& E) { return E.Preset == Preset; });
}

const FVRMExpression* UVRMAvatarDescription::FindExpression(FName Name) const
{
	return Avatar.Expressions.FindByPredicate([Name](const FVRMExpression& E) { return E.Name == Name; });
}
