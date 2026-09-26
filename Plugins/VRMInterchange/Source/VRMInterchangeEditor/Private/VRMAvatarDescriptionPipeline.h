// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "VRMAvatarTypes.h"
#include "VRMPipelineBase.h"
#include "VRMAvatarDescriptionPipeline.generated.h"

class UVRMAvatarDescription;

/**
 * VRM Avatar Description (Post-Import)
 *
 * Makes a UVRMAvatarDescription next to the imported skeletal mesh (P4.1): humanoid bone map,
 * expressions, look-at, first person and meta. The translator reads them from the file and passes
 * them on the VRM node. After the import, a notification shows the avatar's licence.
 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced, ClassGroup=(Interchange), meta=(DisplayName="VRM Avatar Description (Post-Import)"))
class VRMINTERCHANGEEDITOR_API UVRMAvatarDescriptionPipeline : public UVRMPipelineBase
{
	GENERATED_BODY()

public:
	/** The name of the pipeline that will be displayed in the import dialog. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (StandAlonePipelineProperty = "True", PipelineInternalEditionData = "True"))
	FString PipelineDisplayName = TEXT("VRM Avatar Description");

	/** Make the avatar description asset (defaults to the project setting). */
	UPROPERTY(EditAnywhere, Category = "VRM Avatar")
	bool bGenerateAvatarDescription = true;

	/** If the description exists, update it in place (what refers to it keeps working); otherwise the new one gets a unique name. */
	UPROPERTY(EditAnywhere, Category = "VRM Avatar")
	bool bOverwriteExisting = true;

	/** After the import, show a notification with the avatar's licence and usage permissions. */
	UPROPERTY(EditAnywhere, Category = "VRM Avatar")
	bool bShowLicenseNotification = true;

	virtual void PostInitProperties() override;
	virtual void ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath) override;

	/** The description this pipeline made or updated in the last import (tests use it). */
	UVRMAvatarDescription* GetLastDescription() const { return LastDescription.Get(); }

protected:
	virtual void OnSkeletalMeshImported(USkeletalMesh* Mesh, bool bIsAReimport) override;

private:
	FVRMAvatarData StagedAvatar;
	FString StagedSourceHash;
	TWeakObjectPtr<UVRMAvatarDescription> LastDescription;
};
