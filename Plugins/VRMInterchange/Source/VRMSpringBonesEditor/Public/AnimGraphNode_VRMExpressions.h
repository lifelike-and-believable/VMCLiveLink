// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "AnimGraphNode_Base.h"
#include "AnimNode_VRMExpressions.h"
#include "AnimGraphNode_VRMExpressions.generated.h"

/** The "VRM Expressions" AnimGraph node (P4.2): expression curves in, the avatar's morph target curves out. */
UCLASS()
class UAnimGraphNode_VRMExpressions : public UAnimGraphNode_Base
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_VRMExpressions Node;

	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0, 0.6f, 1.f); }
	virtual FText GetTooltipText() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FString GetNodeCategory() const override { return TEXT("VRM"); }
	virtual void ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog) override;
};
