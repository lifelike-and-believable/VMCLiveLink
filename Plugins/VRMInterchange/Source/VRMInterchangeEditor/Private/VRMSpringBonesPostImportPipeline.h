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
  
	/** Creates <Mesh>_SpringData in <import folder>/<file name>/SpringBones: the file's spring bones and colliders, for the VRM Spring Bones anim node. */
	UPROPERTY(EditAnywhere, Category = "VRM Import", meta = (DisplayName = "Spring Bones"))
	bool bGenerateSpringBoneData = true;

	/** If <Mesh>_SpringData exists, update it in place from the file (what refers to it keeps working; edits to it are replaced). Otherwise the new one gets a unique name. */
	UPROPERTY(EditAnywhere, Category = "VRM Import", meta = (DisplayName = "Spring Bones: Update Existing"))
	bool bOverwriteExisting = false;

	/** Creates a post-process AnimBlueprint in <import folder>/<file name>/<Animation Sub Folder> that runs the spring bones on the mesh. */
	UPROPERTY(EditAnywhere, Category = "VRM Import", meta = (DisplayName = "Spring Bones: Post-Process AnimBlueprint"))
	bool bGeneratePostProcessAnimBP = false;

	/** Set that AnimBlueprint as the skeletal mesh's post-process AnimBlueprint. */
	UPROPERTY(EditAnywhere, Category = "VRM Import", meta = (DisplayName = "Spring Bones: Assign Post-Process AnimBlueprint"))
	bool bAssignPostProcessABP = false;

	/** If the post-process AnimBlueprint exists, reuse it (its spring data is updated, your graph edits kept); otherwise a new one gets a unique name. */
	UPROPERTY(EditAnywhere, Category = "VRM Import", meta = (DisplayName = "Spring Bones: Update Existing AnimBlueprint"))
	bool bOverwriteExistingPostProcessABP = false;

	/** On reimport, reuse the post-process AnimBlueprint made by the first import. */
	UPROPERTY(EditAnywhere, Category = "VRM Import", meta = (DisplayName = "Spring Bones: Reuse AnimBlueprint On Reimport"))
	bool bReusePostProcessABPOnReimport = true;

	/** Folder under <import folder>/<file name> for the post-process AnimBlueprint. */
	UPROPERTY(EditAnywhere, Category = "VRM Import", meta = (DisplayName = "Spring Bones: AnimBlueprint Folder"))
	FString AnimationSubFolder = TEXT("SpringBones");

	/** Folder under <import folder>/<file name> for the spring data (empty: <import folder>/<file name> itself). */
	UPROPERTY(EditAnywhere, Category = "VRM Import", meta = (DisplayName = "Spring Bones: Folder"))
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
