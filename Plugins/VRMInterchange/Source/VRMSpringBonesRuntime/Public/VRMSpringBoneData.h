// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VRMSpringBonesTypes.h"
#include "VRMSpringDataCustomVersion.h"
#include "VRMSpringBoneData.generated.h"

/**
 * A VRM avatar's spring bones (hair, clothes, accessories): the springs, their joints and the
 * colliders, parsed from the file at import and converted to UE units (cm) and axes. The spring bone
 * anim node (FAnimNode_VRMSpringBones) simulates it; see FVRMSpringConfig for each field's unit and
 * space.
 *
 * Edited on the game thread (details panel, editing tools); every edit bumps EditRevision, which
 * running anim nodes compare against to rebuild their setup. Saved data is versioned with
 * FVRMSpringDataCustomVersion.
 */
UCLASS(BlueprintType)
class VRMSPRINGBONESRUNTIME_API UVRMSpringBoneData : public UDataAsset
{
    GENERATED_BODY()
public:
    /** The springs, joints and colliders (UE units and axes). */
    UPROPERTY(EditAnywhere, Category="Spring Bones", meta=(ShowOnlyInnerProperties))
    FVRMSpringConfig SpringConfig;

    /** The source file's node hierarchy: each glTF node's parent node index. */
    UPROPERTY(VisibleAnywhere, Category="VRM|Hierarchy")
    TMap<int32, int32> NodeParent;

    /** The source file's node hierarchy: each glTF node's children. */
    UPROPERTY(VisibleAnywhere, Category="VRM|Hierarchy")
    TMap<int32, FVRMNodeChildren> NodeChildren;

    /** Per joint (index into SpringConfig.Joints), the glTF node its tail points at: the next joint
     *  of its spring when that is a child, else INDEX_NONE for a virtual tail. BuildResolvedChildren. */
    UPROPERTY(VisibleAnywhere, Category="VRM|Hierarchy")
    TArray<int32> ResolvedChildNodeIndexPerJoint;

    UPROPERTY(VisibleAnywhere, Category="Spring Bones", meta=(ToolTip="Mapping from VRM/glTF node indices to Unreal bone names"))
    TMap<int32, FName> NodeToBoneMap;

    /** MD5 of the source file's bytes at import, as text. */
    UPROPERTY(VisibleAnywhere, Category="Spring Bones")
    FString SourceHash;

    /** The source file's full path at import (Reimport from Source reads it again). */
    UPROPERTY(VisibleAnywhere, Category="Spring Bones")
    FString SourceFilename;

    /** Goes up with every edit, so running anim nodes rebuild their setup. */
    UPROPERTY(VisibleAnywhere, Category="Spring Bones")
    int32 EditRevision = 0;

    /**
     * Set on load when the asset was saved by an older plugin version whose data can't be upgraded
     * in place (see FVRMSpringDataCustomVersion). The spring bones still run, but may behave
     * differently from a fresh import. Reimport the source file (SourceFilename) to fix it.
     * Saved with the asset, so re-saving an old asset doesn't hide that its data is still old.
     */
    UPROPERTY(VisibleAnywhere, Category="Spring Bones")
    bool bNeedsReimport = false;

    /**
     * The joints and springs as the source file gave them, recorded at import (CaptureSourceValues),
     * for ResetSpringToSource. Empty in assets imported before P6.4: reimport them to fill it.
     */
    UPROPERTY()
    TArray<FVRMSpringJoint> SourceJoints;

    /** The springs as the source file gave them; see SourceJoints. */
    UPROPERTY()
    TArray<FVRMSpring> SourceSprings;

    /** Registers the custom version and, on load, records the version the asset was saved with. */
    virtual void Serialize(FArchive& Ar) override;
    /** Upgrades data saved by an older version (see FVRMSpringDataCustomVersion), or flags bNeedsReimport. */
    virtual void PostLoad() override;

    /** Records the current joints and springs as the file's values (the import calls it). */
    void CaptureSourceValues();

    /** Whether ResetSpringToSource can work: the file's values were recorded and still fit the springs. */
    bool HasSourceValues() const;

    /**
     * Multiplies every joint's and spring's stiffness, drag and gravity power. Only quantities whose
     * factor isn't 1 change: stiffness and gravity stay at least 0 (stiffness has no upper bound, as
     * in VRM 1.0), drag within 0..1. Running spring nodes pick the change up without recompiling
     * (EditRevision).
     */
    void ScaleParameters(float StiffnessScale, float DragScale, float GravityScale);

    /**
     * Puts spring SpringIndex and its joints' parameters (stiffness, drag, gravity, hit radius) back
     * to the file's values. False, changing nothing, without recorded values (HasSourceValues) or for
     * a spring index out of range.
     */
    bool ResetSpringToSource(int32 SpringIndex);

    /** The FVRMSpringDataCustomVersion this asset was loaded with (the latest for new assets). */
    int32 GetLoadedDataVersion() const { return LoadedDataVersion; }

    /** True when data saved at DataVersion has to be reimported to match the current plugin. */
    static bool RequiresReimport(int32 DataVersion, const FVRMSpringConfig& Config);

    /** Upgrade for data saved before PerJointParameters: copies each spring's parameters to its joints. */
    static void CopySpringParametersToJoints(FVRMSpringConfig& Config);

#if WITH_EDITOR
    /** Clamps edited values to their ranges and bumps EditRevision. */
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    /** Applies a spring-level value (FVRMSpring's editing helpers) to that spring's joints. */
    virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
    /** Fills ResolvedChildNodeIndexPerJoint from the springs and the node hierarchy. */
    void BuildResolvedChildren();
#endif

    /** SourceHash and EditRevision together: changes when the file or any edit changes. */
    FString GetEffectiveHash() const { return SourceHash + TEXT("_") + FString::FromInt(EditRevision); }

    /** The skeleton bone for a glTF node index, or None if the node isn't a bone. */
    FName GetBoneNameForNode(int32 NodeIndex) const
    {
        if (const FName* BoneName = NodeToBoneMap.Find(NodeIndex))
        {
            return *BoneName;
        }
        return NAME_None;
    }

    /** Replaces NodeToBoneMap (the import sets it). */
    void SetNodeToBoneMapping(const TMap<int32, FName>& InNodeToBoneMap)
    {
        NodeToBoneMap = InNodeToBoneMap;
    }

private:
    /** Clamps the joints' and springs' parameters and the colliders' sizes to their valid ranges. */
    void SanitizeParameters();

    int32 LoadedDataVersion = FVRMSpringDataCustomVersion::LatestVersion;
};