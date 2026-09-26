// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "VRMPipelineBase.h"
#include "VRMSpringBoneData.h" // Ensure UVRMSpringBoneData is a complete type here
#include "VRMSpringBonesPostImportPipeline.generated.h"

class UInterchangeBaseNodeContainer;
class UInterchangeSourceData;
class USkeleton;
class USkeletalMesh;
class UFactory;
struct FVRMSpringConfig;
class FVRMDocument;

/**
 * VRM Spring Bones (Post-Import)
 *
 * - Parses VRM spring bone data while the import is set up, and creates a spring data asset once
 *   the import's skeletal mesh exists (UVRMPipelineBase).
 * - Optionally duplicates a Post-Process AnimBP, injects the SpringConfig, and assigns it to the SkeletalMesh.
 * - Does NOT save packages during import; marks packages dirty so Save All/SCC handle persistence.
 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced, ClassGroup=(Interchange), meta=(DisplayName="VRM Spring Bones (Post-Import)"))
class VRMINTERCHANGEEDITOR_API UVRMSpringBonesPostImportPipeline : public UVRMPipelineBase
{
	GENERATED_BODY()
public:
	UVRMSpringBonesPostImportPipeline() = default;

#if WITH_EDITOR
	virtual void PostInitProperties() override;

	/** The name of the pipeline that will be display in the import dialog. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (StandAlonePipelineProperty = "True", PipelineInternalEditionData = "True"))
	FString PipelineDisplayName = "VRM Spring Bones Import and Configuration";	// Dialog toggles (defaults loaded from project settings)
  
	UPROPERTY(EditAnywhere, Category = "VRM Spring")
	bool bGenerateSpringBoneData = true;

	/** If a spring data asset with this name exists, update it in place (what refers to it keeps working); otherwise the new one gets a unique name. */
	UPROPERTY(EditAnywhere, Category = "VRM Spring")
	bool bOverwriteExisting = false;

	UPROPERTY(EditAnywhere, Category = "VRM Spring")
	bool bGeneratePostProcessAnimBP = false;

	UPROPERTY(EditAnywhere, Category = "VRM Spring")
	bool bAssignPostProcessABP = false;

	/** If the post-process AnimBP exists, reuse it (its spring data is updated); otherwise a new one gets a unique name. */
	UPROPERTY(EditAnywhere, Category = "VRM Spring")
	bool bOverwriteExistingPostProcessABP = false;

	UPROPERTY(EditAnywhere, Category = "VRM Spring")
	bool bReusePostProcessABPOnReimport = true;

	UPROPERTY(EditAnywhere, Category = "VRM Spring")
	FString AnimationSubFolder = TEXT("SpringBones");

	UPROPERTY(EditAnywhere, Category = "VRM Spring")
	FString SubFolder = TEXT("SpringBones");
#endif

	// UInterchangePipelineBase
	virtual void ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath) override;

protected:
	virtual void OnSkeletalMeshImported(USkeletalMesh* Mesh, bool bIsAReimport) override;

private:
	// Parsing/materialization helpers
	bool ParseAndFillDataAsset(const FVRMDocument& Document, UVRMSpringBoneData* Dest) const;
	bool ResolveBoneNames(const FVRMDocument& Document, FVRMSpringConfig& InOut, int32& OutResolvedColliders, int32& OutResolvedJoints, int32& OutResolvedCenters) const;
	void ValidateBoneNamesAgainstSkeleton(const USkeleton* Skeleton, const FVRMSpringConfig& Config) const;

	// Asset helpers
	bool SetSpringConfigOnAnimBlueprint(UObject* AnimBlueprintObj, UVRMSpringBoneData* SpringData) const;
	bool AssignPostProcessABPToMesh(USkeletalMesh* SkelMesh, UObject* AnimBlueprintObj) const;

	/** Spring data parsed in ExecutePipeline, waiting for the mesh. */
	TStrongObjectPtr<UVRMSpringBoneData> StagedSpringData;
};
