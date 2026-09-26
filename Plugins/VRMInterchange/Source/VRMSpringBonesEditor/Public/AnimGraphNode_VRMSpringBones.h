// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// AnimGraphNode_VRMSpringBones.h
#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimBlueprint.h"
#include "AnimGraphNode_SkeletalControlBase.h"
#include "AnimNode_VRMSpringBones.h"
#include "AnimGraphNode_VRMSpringBones.generated.h"

UCLASS()
class UAnimGraphNode_VRMSpringBones : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category = Settings)
    FAnimNode_VRMSpringBones Node;

    virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0, 0.6f, 1.f); }
    virtual FText GetTooltipText() const override;
    virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
    virtual FString GetNodeCategory() const override { return TEXT("VRM"); }
	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
    virtual void ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog) override;

private:
    /** The node's spring data, or its pin's default. Null with bOutFromGraph when the pin is linked or
     *  bound, so the asset comes from the graph and can't be checked here; null without it when unset. */
    const UVRMSpringBoneData* GetSpringDataForValidation(bool& bOutFromGraph) const;
};
