// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "InterchangePipelineBase.h"
#include "VRMMaterialPostImportPipeline.generated.h"

class UMaterialInstanceConstant;
class USkeletalMesh;

/**
 * VRM Materials (Post-Import)
 *
 * The VRM translator creates a character material instance per master material it uses
 * (MI_VRM_<Character> on M_VRM_Master, MI_VRM_<Character>__MToon on M_VRM_MToon) and one instance
 * per VRM material (MI_VRM_<Character>_<Material>). This pipeline:
 *
 * - makes the generated MToon materials exist before the instances are created, when the import
 *   uses them (VRM::MToon::FindOrCreateMToonMaterials);
 * - turns each MToon instance's AlphaMode and DoubleSided parameters into its blend mode and
 *   two-sided overrides;
 * - reparents each per-material instance to the character instance with the same master, so shared
 *   parameters can be tuned in one place;
 * - sets MI_VRM_<Character>__Outline, when the model has MToon outlines, as the skeletal mesh's
 *   overlay material.
 *
 * It only touches assets created by the current import.
 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced, ClassGroup=(Interchange), meta=(DisplayName="VRM Materials (Post-Import)"))
class VRMINTERCHANGEEDITOR_API UVRMMaterialPostImportPipeline : public UInterchangePipelineBase
{
	GENERATED_BODY()
public:
	/** The name of the pipeline that will be displayed in the import dialog. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (StandAlonePipelineProperty = "True", PipelineInternalEditionData = "True"))
	FString PipelineDisplayName = TEXT("VRM Material Instance Hierarchy");

	/** Parent each per-material instance to the character material instance, so one edit changes every material of the character. */
	UPROPERTY(EditAnywhere, Category = "VRM Import", meta = (DisplayName = "Materials: Share a Character Parent"))
	bool bParentMaterialsToCharacterInstance = true;

	/** Draw the MToon outline, as the skeletal mesh's overlay material (MI_VRM_<Material>__Outline). */
	UPROPERTY(EditAnywhere, Category = "VRM Import", meta = (DisplayName = "Materials: MToon Outline"))
	bool bApplyMToonOutline = true;

	virtual void ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath) override;

	/** What ExecutePostImportPipeline does with each created asset. Public so tests can drive it. */
	void HandleImportedAsset(UObject* CreatedAsset);

protected:
	virtual void ExecutePostImportPipeline(const UInterchangeBaseNodeContainer* BaseNodeContainer, const FString& NodeKey, UObject* CreatedAsset, bool bIsAReimport) override;

	/** Creating the MToon materials and editing the imported assets must run on the game thread. */
	virtual bool CanExecuteOnAnyThread(EInterchangePipelineTask PipelineTask) override;

private:
	/** The import's first source file, which its messages are filed under (P6.3). */
	FString ImportSourceFile;

	/** Pairs up the instances seen so far in this import; order of arrival does not matter. */
	void ResolveParents();

	/** Sets the outline instance as the mesh's overlay material once both have arrived. */
	void ResolveOverlay();

	/** Name the translator gives the character instance: MI_VRM_<source file base name>, sanitized. */
	FString CharacterInstanceName;

	TArray<TWeakObjectPtr<UMaterialInstanceConstant>> ImportedInstances;
	TWeakObjectPtr<UMaterialInstanceConstant> OutlineInstance;
	TWeakObjectPtr<USkeletalMesh> ImportedMesh;
};
