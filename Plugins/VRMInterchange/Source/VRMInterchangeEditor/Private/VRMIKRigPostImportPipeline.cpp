// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMIKRigPostImportPipeline.h"

#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Engine/SkeletalMesh.h"
#include "Rig/IKRigDefinition.h"
#include "VRMInterchangeSettings.h"

#if WITH_EDITOR
void UVRMIKRigPostImportPipeline::PostInitProperties()
{
	Super::PostInitProperties();
	// The project setting is the default for new pipelines; the import dialog decides per import.
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		if (const UVRMInterchangeSettings* Settings = GetDefault<UVRMInterchangeSettings>())
		{
			bGenerateIKRig = Settings->bGenerateIKRigAssets;
		}
	}
}
#endif

// Stage the work; the IK Rig is made once the import's skeletal mesh exists.
void UVRMIKRigPostImportPipeline::ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath)
{
	Super::ExecutePipeline(BaseNodeContainer, SourceDatas, ContentBasePath);
	// Only this instance's flag counts (seeded from the project settings in PostInitProperties).
	if (BeginImport(SourceDatas, ContentBasePath) && BaseNodeContainer && bGenerateIKRig)
	{
		WaitForSkeletalMesh();
	}
}

void UVRMIKRigPostImportPipeline::OnSkeletalMeshImported(USkeletalMesh* Mesh, bool bIsAReimport)
{
	const TCHAR* TemplatePath = TEXT("/VRMInterchange/Animation/IK_Rig_VRMTemplate.IK_Rig_VRMTemplate");
	bool bReused = false;
	UIKRigDefinition* IKRig = Cast<UIKRigDefinition>(DuplicateTemplateAsset(TemplatePath, GetCharacterFolder() / IKRigDefinitionSubFolder,
		FString::Printf(TEXT("%s_%s"), *AssetBaseName, *Mesh->GetName()), bOverwriteExisting, bReused));
	if (IKRig)
	{
		IKRig->SetPreviewMesh(Mesh, true);
		IKRig->MarkPackageDirty();
	}
}
