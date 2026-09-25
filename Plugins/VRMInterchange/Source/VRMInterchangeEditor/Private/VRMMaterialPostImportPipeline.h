// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "InterchangePipelineBase.h"
#include "VRMMaterialPostImportPipeline.generated.h"

class UMaterialInstanceConstant;

/**
 * VRM Materials (Post-Import)
 *
 * The VRM translator creates one character material instance (MI_VRM_<Character>) and one
 * instance per VRM material (MI_VRM_<Character>_<Material>), all parented to the master material.
 * This pipeline reparents each per-material instance to the character instance, so shared
 * parameters can be tuned in one place.
 *
 * It only touches material instances created by the current import.
 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced, ClassGroup=(Interchange), meta=(DisplayName="VRM Materials (Post-Import)"))
class VRMINTERCHANGEEDITOR_API UVRMMaterialPostImportPipeline : public UInterchangePipelineBase
{
	GENERATED_BODY()
public:
	/** The name of the pipeline that will be displayed in the import dialog. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (StandAlonePipelineProperty = "True", PipelineInternalEditionData = "True"))
	FString PipelineDisplayName = TEXT("VRM Material Instance Hierarchy");

	/** Parent each per-material instance to the character material instance. */
	UPROPERTY(EditAnywhere, Category = "VRM Materials")
	bool bParentMaterialsToCharacterInstance = true;

	virtual void ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath) override;

protected:
	virtual void ExecutePostImportPipeline(const UInterchangeBaseNodeContainer* BaseNodeContainer, const FString& NodeKey, UObject* CreatedAsset, bool bIsAReimport) override;

	/** Reparenting edits UObjects, so post-import work must run on the game thread. */
	virtual bool CanExecuteOnAnyThread(EInterchangePipelineTask PipelineTask) override;

private:
	/** Pairs up the instances seen so far in this import; order of arrival does not matter. */
	void ResolveParents();

	/** Name the translator gives the character instance: MI_VRM_<source file base name>, sanitized. */
	FString CharacterInstanceName;

	TArray<TWeakObjectPtr<UMaterialInstanceConstant>> ImportedInstances;
};
