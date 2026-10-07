// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "VRMPipelineBase.h"
#include "VRMLiveLinkPostImportPipeline.generated.h"

class UInterchangeBaseNodeContainer;
class UInterchangeSourceData;
class USkeletalMesh;
class USkeleton;
class UBlueprint;
class UAnimBlueprint;
class UFactory;

/**
 * VRM Live Link (Post-Import)
 *
 * - Runs once the import's skeletal mesh exists (UVRMPipelineBase).
 * - Duplicates a character Actor Blueprint and an AnimBlueprint from templates.
 * - Wires up the imported SkeletalMesh to the actor's Skeletal Mesh component (its construction
 *   script template, VRMActorBlueprintWiring.h) and sets the preview mesh on the AnimBP.
 * - Does NOT save packages during import; marks packages dirty so Save All/SCC handle persistence.
 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced, ClassGroup=(Interchange), meta=(DisplayName="VRM Live Link (Post-Import)"))
class VRMINTERCHANGEEDITOR_API UVRMLiveLinkPostImportPipeline : public UVRMPipelineBase
{
	GENERATED_BODY()
public:
	UVRMLiveLinkPostImportPipeline() = default;

#if WITH_EDITOR
	/** The name of the pipeline that will be display in the import dialog. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (StandAlonePipelineProperty = "True", PipelineInternalEditionData = "True"))
	FString PipelineDisplayName = "Live Link Actor Set-up";

	/** Creates BP_LL_VRM_<Mesh>, an actor showing the character driven by Live Link, and its AnimBlueprint ABP_LL_VRM_<Mesh>, in <import folder>/<file name>/LiveLink. */
	UPROPERTY(EditAnywhere, Category = "VRM Import", meta = (DisplayName = "Live Link Actor"))
	bool bGenerateLiveLinkEnabledActor = true;

	/** Creates BP_LL_VRM_To_UE5_<Mesh> in <import folder>/<file name>/LiveLink: an actor that retargets the character's Live Link pose to the UE5 mannequin. */
	UPROPERTY(EditAnywhere, Category = "VRM Import", meta = (DisplayName = "Retarget Actor"))
	bool bGenerateLiveLinkRetargetActor = true;

	/** If the actors and AnimBlueprint exist, reuse them: they are pointed at the new mesh and your edits are kept. Otherwise the new ones get unique names. */
	UPROPERTY(EditAnywhere, Category = "VRM Import", meta = (DisplayName = "Actors: Update Existing"))
	bool bOverwriteExisting = false;

	/** Folder under <import folder>/<file name>/LiveLink for the AnimBlueprint. */
	UPROPERTY(EditAnywhere, Category = "VRM Import", meta = (DisplayName = "Actors: AnimBlueprint Folder"))
	FString AnimationSubFolder = TEXT("Animation");
#endif

	// UInterchangePipelineBase
	virtual void ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath) override;

#if WITH_EDITOR
	virtual void ApplyProjectSettings() override;
#endif

protected:
	virtual void OnSkeletalMeshImported(USkeletalMesh* Mesh, bool bIsAReimport) override;
};
