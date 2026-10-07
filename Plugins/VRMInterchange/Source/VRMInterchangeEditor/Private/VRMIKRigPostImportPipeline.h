// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "VRMAvatarTypes.h"
#include "VRMPipelineBase.h"
#include "VRMIKRigPostImportPipeline.generated.h"

class UInterchangeBaseNodeContainer;
class UInterchangeSourceData;
class USkeletalMesh;
class USkeleton;
class UIKRigDefinition;
class UFactory;

/**
 * VRM IK Rig (Post-Import)
 *
 * - Runs once the import's skeletal mesh exists (UVRMPipelineBase).
 * - Builds the IK Rig from the VRM's humanoid map (P4.3): retarget root at the hips and the UE5
 *   mannequin's chain names, whatever the bones are called. Without a humanoid map (or with
 *   bBuildFromHumanoid off) it duplicates the template IK Rig instead.
 * - Sets the preview mesh on the IK Rig when possible.
 * - Does NOT save packages during import; marks packages dirty so Save All/SCC handle persistence.
 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced, ClassGroup=(Interchange), meta=(DisplayName="VRM IK Rig (Post-Import)"))
class VRMINTERCHANGEEDITOR_API UVRMIKRigPostImportPipeline : public UVRMPipelineBase
{
	GENERATED_BODY()
public:
	UVRMIKRigPostImportPipeline() = default;

#if WITH_EDITOR
	/** The name of the pipeline that will be display in the import dialog. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (StandAlonePipelineProperty = "True", PipelineInternalEditionData = "True"))
	FString PipelineDisplayName = "VRM IK Rig Set-up";

	/** Creates IK_Rig_VRM_<Mesh> in <import folder>/<file name>/IKRigDefinition, with retargeting chains from the humanoid map, to retarget the character to and from other skeletons (defaults to the project setting). */
	UPROPERTY(EditAnywhere, Category = "VRM Import", meta = (DisplayName = "IK Rig"))
	bool bGenerateIKRig = true;

	/** If the IK Rig exists, rebuild it in place (what refers to it keeps working; edits to it are replaced). Otherwise the new one gets a unique name. */
	UPROPERTY(EditAnywhere, Category = "VRM Import", meta = (DisplayName = "IK Rig: Update Existing"))
	bool bOverwriteExisting = true;

	/** Build the chains from the VRM's humanoid map. Off: duplicate the template IK Rig, which only suits VRoid bone names. */
	UPROPERTY(EditAnywhere, Category = "VRM Import", meta = (DisplayName = "IK Rig: Build From Humanoid Map"))
	bool bBuildFromHumanoid = true;

	/** Folder under <import folder>/<file name> for the IK Rig. */
	UPROPERTY(EditAnywhere, Category = "VRM Import", meta = (DisplayName = "IK Rig: Folder"))
	FString IKRigDefinitionSubFolder = TEXT("IKRigDefinition");

	/** The IK Rig is named <this>_<Mesh>. */
	UPROPERTY(EditAnywhere, Category = "VRM Import", meta = (DisplayName = "IK Rig: Name Prefix"))
	FString AssetBaseName = TEXT("IK_Rig_VRM");
#endif

	// UInterchangePipelineBase
	virtual void ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath) override;

#if WITH_EDITOR
	virtual void ApplyProjectSettings() override;
#endif

	/** The IK Rig made by the last import (for tests). */
	UIKRigDefinition* GetLastIKRig() const { return LastIKRig.Get(); }

protected:
	virtual void OnSkeletalMeshImported(USkeletalMesh* Mesh, bool bIsAReimport) override;

private:
	FVRMAvatarData StagedAvatar;
	bool bHaveHumanoid = false;
	TWeakObjectPtr<UIKRigDefinition> LastIKRig;
};
