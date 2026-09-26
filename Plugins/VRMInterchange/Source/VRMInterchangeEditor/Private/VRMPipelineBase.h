// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "InterchangePipelineBase.h"
#include "VRMPipelineBase.generated.h"

class UInterchangeBaseNodeContainer;
class UInterchangeSourceData;
class USkeletalMesh;

/**
 * What the VRM post-import pipelines (spring bones, IK Rig, Live Link) share (P3.4):
 *
 * - ExecutePipeline stages the work (BeginImport, then WaitForSkeletalMesh when there is any).
 * - Interchange then reports each asset this import created to ExecutePostImportPipeline. The first
 *   USkeletalMesh among them goes to OnSkeletalMeshImported, once. Assets of other imports are never
 *   reported to this pipeline, so nothing has to search folders or tell characters apart.
 * - CreateOrReuseAsset and DuplicateTemplateAsset make the generated assets. With "reuse", an asset
 *   that already has the name is updated or rewired in place, so references to it keep working;
 *   otherwise the new asset gets a unique name.
 */
UCLASS(Abstract)
class VRMINTERCHANGEEDITOR_API UVRMPipelineBase : public UInterchangePipelineBase
{
	GENERATED_BODY()

public:
	/** True while staged work waits for this import's skeletal mesh (tests use it). */
	bool HasPendingPostImportWork() const { return bWaitingForMesh; }

	/** What ExecutePostImportPipeline does with each created asset. Public so tests can drive it. */
	void HandleImportedAsset(UObject* CreatedAsset, bool bIsAReimport);

protected:
	virtual void ExecutePostImportPipeline(const UInterchangeBaseNodeContainer* BaseNodeContainer, const FString& NodeKey, UObject* CreatedAsset, bool bIsAReimport) override;

	/** Post-import work creates and edits assets, so it runs on the game thread. */
	virtual bool CanExecuteOnAnyThread(EInterchangePipelineTask PipelineTask) override;

	/**
	 * Records the import's source file and folders and clears what a previous import staged. False
	 * when there is no source data. Call at the start of ExecutePipeline.
	 */
	bool BeginImport(const TArray<UInterchangeSourceData*>& SourceDatas, const FString& InContentBasePath);

	/** Asks for OnSkeletalMeshImported: call once the pipeline has staged work that needs the mesh. */
	void WaitForSkeletalMesh() { bWaitingForMesh = true; }

	/** This import's skeletal mesh, once, if WaitForSkeletalMesh was called. */
	virtual void OnSkeletalMeshImported(USkeletalMesh* Mesh, bool bIsAReimport) {}

	const FString& GetSourceFilename() const { return ImportSourceFilename; }
	const FString& GetContentBasePath() const { return ImportContentBasePath; }
	/** <ContentBasePath>/<source file base name> (VRMPipeline::MakeCharacterBasePath). */
	const FString& GetCharacterFolder() const { return ImportCharacterFolder; }

	/**
	 * An asset of Class named Folder/Name. With bReuseExisting, an asset of that class already there
	 * is returned (bOutReused) for the caller to update. Otherwise, or when the name is taken by
	 * another class, a new asset is created under a unique name. Marked dirty, never saved.
	 */
	static UObject* CreateOrReuseAsset(UClass* Class, const FString& Folder, const FString& Name, bool bReuseExisting, bool& bOutReused);

	/**
	 * A copy of the template asset named Folder/Name, made with IAssetTools::DuplicateAsset, or the
	 * asset already there (bOutReused) with bReuseExisting, as above. Blueprints are compiled. Null
	 * if the template can't be loaded.
	 */
	static UObject* DuplicateTemplateAsset(const TCHAR* TemplatePath, const FString& Folder, const FString& Name, bool bReuseExisting, bool& bOutReused);

	/** The asset at Folder/Name if it exists (in memory or on disk), else null. */
	static UObject* FindExistingAsset(const FString& Folder, const FString& Name);

private:
	FString ImportSourceFilename;
	FString ImportContentBasePath;
	FString ImportCharacterFolder;
	bool bWaitingForMesh = false;
};
