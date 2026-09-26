// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "AnimGraphNode_VRMSpringBones.h"
#include "Animation/AnimBlueprint.h"

#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "ToolMenus.h"
#include "VRMSpringBoneData.h"

#define LOCTEXT_NAMESPACE "AnimGraphNode_VRMSpringBones"

FText UAnimGraphNode_VRMSpringBones::GetTooltipText() const
{
    return LOCTEXT("Tooltip", "Simulates VRM spring bones");
}

FText UAnimGraphNode_VRMSpringBones::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
    return LOCTEXT("Title", "VRM Spring Bones");
}

void UAnimGraphNode_VRMSpringBones::ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog)
{
    Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);

    // Spring data saved by an older plugin version (FVRMSpringDataCustomVersion) still runs, but
    // won't match a fresh import. Say so where the user wires the asset up.
    if (const UVRMSpringBoneData* SpringData = Node.SpringData.Get())
    {
        if (SpringData->bNeedsReimport)
        {
            const FString Message = FString::Printf(
                TEXT("@@: spring data '%s' is from an older VRMInterchange version and its colliders and gravity are in the old axes. Reimport '%s' to update it."),
                *SpringData->GetName(),
                SpringData->SourceFilename.IsEmpty() ? TEXT("the source VRM file") : *SpringData->SourceFilename);
            MessageLog.Warning(*Message, this);
        }
    }
}

#undef LOCTEXT_NAMESPACE
