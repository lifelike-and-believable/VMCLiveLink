// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VRMSpringBonesTypes.h"
#include "VRMSpringDataCustomVersion.h"
#include "VRMSpringBoneData.generated.h"

// Runtime-available data asset storing parsed spring bone configuration.
UCLASS(BlueprintType)
class VRMSPRINGBONESRUNTIME_API UVRMSpringBoneData : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category="Spring Bones", meta=(ShowOnlyInnerProperties))
    FVRMSpringConfig SpringConfig;

    UPROPERTY(VisibleAnywhere, Category="VRM|Hierarchy")
    TMap<int32, int32> NodeParent;

    UPROPERTY(VisibleAnywhere, Category="VRM|Hierarchy")
    TMap<int32, FVRMNodeChildren> NodeChildren;

    UPROPERTY(VisibleAnywhere, Category="VRM|Hierarchy")
    TArray<int32> ResolvedChildNodeIndexPerJoint;

    UPROPERTY(VisibleAnywhere, Category="Spring Bones", meta=(ToolTip="Mapping from VRM/glTF node indices to Unreal bone names"))
    TMap<int32, FName> NodeToBoneMap;

    UPROPERTY(VisibleAnywhere, Category="Spring Bones")
    FString SourceHash;

    UPROPERTY(VisibleAnywhere, Category="Spring Bones")
    FString SourceFilename;

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

    virtual void Serialize(FArchive& Ar) override;
    virtual void PostLoad() override;

    /** The FVRMSpringDataCustomVersion this asset was loaded with (the latest for new assets). */
    int32 GetLoadedDataVersion() const { return LoadedDataVersion; }

    /** True when data saved at DataVersion has to be reimported to match the current plugin. */
    static bool RequiresReimport(int32 DataVersion, const FVRMSpringConfig& Config);

    /** Upgrade for data saved before PerJointParameters: copies each spring's parameters to its joints. */
    static void CopySpringParametersToJoints(FVRMSpringConfig& Config);

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
    void BuildResolvedChildren();
#endif

    FString GetEffectiveHash() const { return SourceHash + TEXT("_") + FString::FromInt(EditRevision); }

    FName GetBoneNameForNode(int32 NodeIndex) const
    {
        if (const FName* BoneName = NodeToBoneMap.Find(NodeIndex))
        {
            return *BoneName;
        }
        return NAME_None;
    }

    void SetNodeToBoneMapping(const TMap<int32, FName>& InNodeToBoneMap)
    {
        NodeToBoneMap = InNodeToBoneMap;
    }

private:
    int32 LoadedDataVersion = FVRMSpringDataCustomVersion::LatestVersion;
};