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

	/** Generate IK Rig asset next to the imported mesh (defaults to the project setting) */
	UPROPERTY(EditAnywhere, Category = "VRM IK Rig")
	bool bGenerateIKRig = true;

	/** If an IK Rig with this name exists, reuse it (its preview mesh is updated); otherwise the new one gets a unique name */
	UPROPERTY(EditAnywhere, Category = "VRM IK Rig")
	bool bOverwriteExisting = false;

	/** Build the chains from the VRM's humanoid map. Off: duplicate the template IK Rig, which only suits VRoid bone names. */
	UPROPERTY(EditAnywhere, Category = "VRM IK Rig")
	bool bBuildFromHumanoid = true;

	/** Subfolder under character folder to place the asset */
	UPROPERTY(EditAnywhere, Category = "VRM IK Rig")
	FString IKRigDefinitionSubFolder = TEXT("IKRigDefinition");

	/** Base name prefix for the IK Rig (actual name includes the character suffix) */
	UPROPERTY(EditAnywhere, Category = "VRM IK Rig")
	FString AssetBaseName = TEXT("IK_Rig_VRM");
#endif

	// UInterchangePipelineBase
	virtual void ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath) override;

#if WITH_EDITOR
	virtual void PostInitProperties() override;
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
