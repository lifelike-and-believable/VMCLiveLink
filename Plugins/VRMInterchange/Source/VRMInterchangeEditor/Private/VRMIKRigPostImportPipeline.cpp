// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMIKRigPostImportPipeline.h"

#include "VRMImportMessages.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Engine/SkeletalMesh.h"
#include "InterchangeVRMNode.h"
#include "Rig/IKRigDefinition.h"
#include "VRMIKRigBuilder.h"
#include "VRMInterchangeLog.h"
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
	LastIKRig = nullptr; // only this import's rig counts
	// What this pipeline logs belongs to this import's message log page (P6.3).
	const VRM::ImportMessages::FScope MessageScope(GetFirstSourceFile(SourceDatas));
	// Only this instance's flag counts (seeded from the project settings in PostInitProperties).
	if (BeginImport(SourceDatas, ContentBasePath) && BaseNodeContainer && bGenerateIKRig)
	{
		// The humanoid map the translator read (P4.1); without one, the template is duplicated.
		StagedAvatar = FVRMAvatarData();
		bHaveHumanoid = false;
		if (const UInterchangeVRMNode* VRMNode = UInterchangeVRMNode::Find(*BaseNodeContainer))
		{
			const bool bHaveAvatar = VRMNode->GetAvatarData(StagedAvatar);
			bHaveHumanoid = bHaveAvatar && !VRMIKRig::RetargetRoot(StagedAvatar).IsNone();
			if (bHaveAvatar && !bHaveHumanoid && bBuildFromHumanoid)
			{
				UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] The VRM's humanoid map has no hips bone, so the IK Rig is copied from the template instead of built from the map."));
			}
		}
		WaitForSkeletalMesh();
	}
}

void UVRMIKRigPostImportPipeline::OnSkeletalMeshImported(USkeletalMesh* Mesh, bool bIsAReimport)
{
	const FString Folder = GetCharacterFolder() / IKRigDefinitionSubFolder;
	const FString Name = FString::Printf(TEXT("%s_%s"), *AssetBaseName, *Mesh->GetName());
	bool bReused = false;
	UIKRigDefinition* IKRig = nullptr;

	if (bBuildFromHumanoid && bHaveHumanoid)
	{
		IKRig = Cast<UIKRigDefinition>(CreateOrReuseAsset(UIKRigDefinition::StaticClass(), Folder, Name, bOverwriteExisting, bReused));
		TArray<FString> Problems;
		if (IKRig && !VRMIKRig::Build(IKRig, Mesh, StagedAvatar, &Problems))
		{
			IKRig->SetPreviewMesh(Mesh, true); // at least show the character
		}
		for (const FString& Problem : Problems)
		{
			UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] IK Rig for '%s': %s"), *Mesh->GetName(), *Problem);
		}
	}
	else
	{
		const TCHAR* TemplatePath = TEXT("/VRMInterchange/Animation/IK_Rig_VRMTemplate.IK_Rig_VRMTemplate");
		IKRig = Cast<UIKRigDefinition>(DuplicateTemplateAsset(TemplatePath, Folder, Name, bOverwriteExisting, bReused));
		if (IKRig)
		{
			IKRig->SetPreviewMesh(Mesh, true);
		}
	}

	if (IKRig)
	{
		IKRig->MarkPackageDirty();
		LastIKRig = IKRig;
	}
}
