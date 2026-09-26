// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "AnimGraphNode_VRMExpressions.h"
#include "Animation/Skeleton.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/CompilerResultsLog.h"
#include "VRMAvatarDescription.h"

#define LOCTEXT_NAMESPACE "AnimGraphNode_VRMExpressions"

FText UAnimGraphNode_VRMExpressions::GetTooltipText() const
{
	return LOCTEXT("Tooltip", "Drives a VRM avatar's morph targets from expression curves (happy, aa, blink, ... or their VRM 0.x names)");
}

FText UAnimGraphNode_VRMExpressions::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("Title", "VRM Expressions");
}

void UAnimGraphNode_VRMExpressions::ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog)
{
	Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);

	// With the pin shown, the asset is the pin's default, or comes from the graph through a link or
	// a property binding, which can't be checked here. A shown pin with none of these is unset.
	const FName PinName = GET_MEMBER_NAME_CHECKED(FAnimNode_VRMExpressions, AvatarDescription);
	const UVRMAvatarDescription* Description = Node.AvatarDescription.Get();
	bool bFromGraph = false;
	if (!Description)
	{
		if (const UEdGraphPin* Pin = FindPin(PinName))
		{
			Description = Cast<UVRMAvatarDescription>(Pin->DefaultObject);
			bFromGraph = !Description && (Pin->LinkedTo.Num() > 0 || HasBinding(PinName));
		}
	}
	if (!Description)
	{
		if (!bFromGraph)
		{
			MessageLog.Warning(TEXT("@@: no Avatar Description is set, so no expressions are applied. Assign the <Mesh>_Avatar asset imported with the character."), this);
		}
		return;
	}
	if (Description->Avatar.Expressions.Num() == 0)
	{
		MessageLog.Warning(*FString::Printf(TEXT("@@: avatar description '%s' has no expressions."), *Description->GetName()), this);
	}
}

#undef LOCTEXT_NAMESPACE
