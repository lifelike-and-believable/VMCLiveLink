// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "AnimGraphNode_VRMSpringBones.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/Skeleton.h"

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

const UVRMSpringBoneData* UAnimGraphNode_VRMSpringBones::GetSpringDataForValidation(bool& bOutFromGraph) const
{
    bOutFromGraph = false;
    if (Node.SpringData)
    {
        return Node.SpringData.Get();
    }
    // With the pin shown, the asset is the pin's default, or comes from the graph: a linked pin, or a
    // binding to a variable (the plugin's template AnimBlueprint binds it to SpringConfig, which the
    // import pipeline sets). Only the default can be checked here.
    if (const UEdGraphPin* Pin = FindPin(GET_MEMBER_NAME_CHECKED(FAnimNode_VRMSpringBones, SpringData)))
    {
        if (const UVRMSpringBoneData* Default = Cast<UVRMSpringBoneData>(Pin->DefaultObject))
        {
            return Default;
        }
        bOutFromGraph = true;
    }
    return nullptr;
}

void UAnimGraphNode_VRMSpringBones::ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog)
{
    Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);

    bool bFromGraph = false;
    const UVRMSpringBoneData* SpringData = GetSpringDataForValidation(bFromGraph);
    if (!SpringData)
    {
        if (!bFromGraph)
        {
            MessageLog.Warning(TEXT("@@: no Spring Data asset is set, so nothing is simulated. Assign the spring data asset imported with the character."), this);
        }
        return;
    }

    // Spring data saved by an older plugin version (FVRMSpringDataCustomVersion) still runs, but
    // won't match a fresh import. Say so where the user wires the asset up.
    if (SpringData->bNeedsReimport)
    {
        const FString Message = FString::Printf(
            TEXT("@@: spring data '%s' is from an older VRMInterchange version and won't match a fresh import. Reimport '%s' to update it."),
            *SpringData->GetName(),
            SpringData->SourceFilename.IsEmpty() ? TEXT("the source VRM file") : *SpringData->SourceFilename);
        MessageLog.Warning(*Message, this);
    }

    const FVRMSpringConfig& Cfg = SpringData->SpringConfig;
    if (!Cfg.IsValid())
    {
        MessageLog.Warning(*FString::Printf(TEXT("@@: spring data '%s' has no springs. Reimport the VRM file with Generate Spring Bone Data on."), *SpringData->GetName()), this);
        return;
    }

    // Bones the data names that this skeleton doesn't have: their joints and colliders are ignored.
    if (ForSkeleton)
    {
        const FReferenceSkeleton& RefSkeleton = ForSkeleton->GetReferenceSkeleton();
        TArray<FName> Missing;
        auto Check = [&](FName Name, int32 NodeIndex)
        {
            if (Name.IsNone() && NodeIndex != INDEX_NONE)
            {
                Name = SpringData->GetBoneNameForNode(NodeIndex);
            }
            if (!Name.IsNone() && RefSkeleton.FindBoneIndex(Name) == INDEX_NONE)
            {
                Missing.AddUnique(Name);
            }
        };
        for (const FVRMSpringJoint& Joint : Cfg.Joints) Check(Joint.BoneName, Joint.NodeIndex);
        for (const FVRMSpringCollider& Collider : Cfg.Colliders) Check(Collider.BoneName, Collider.NodeIndex);
        for (const FVRMSpring& Spring : Cfg.Springs) Check(Spring.CenterBoneName, Spring.CenterNodeIndex);

        if (Missing.Num() > 0)
        {
            constexpr int32 MaxListed = 5;
            TArray<FString> Names;
            for (int32 I = 0; I < Missing.Num() && I < MaxListed; ++I)
            {
                Names.Add(Missing[I].ToString());
            }
            const FString Message = FString::Printf(
                TEXT("@@: %d bone(s) in spring data '%s' are not in skeleton '%s': %s%s. Their joints and colliders are ignored. Is this the spring data for this character?"),
                Missing.Num(), *SpringData->GetName(), *ForSkeleton->GetName(),
                *FString::Join(Names, TEXT(", ")), Missing.Num() > MaxListed ? TEXT(", ...") : TEXT(""));
            MessageLog.Warning(*Message, this);
        }
    }
}

#undef LOCTEXT_NAMESPACE
