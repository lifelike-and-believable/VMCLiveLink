// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMAvatarDescriptionPipeline.h"

#include "VRMImportMessages.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "InterchangeVRMNode.h"
#include "Misc/App.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "VRMAvatarDescription.h"
#include "VRMAvatarParser.h"
#include "VRMDocument.h"
#include "VRMImportReport.h"
#include "VRMInterchangeLog.h"
#include "VRMInterchangeSettings.h"
#include "VRMParsedModel.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace
{
	/**
	 * Writes the humanoid map onto the mesh as metadata for VMCLiveLink (P4.4, D-4), replacing any
	 * left by an earlier import. Touches nothing when the tags are already right, so an unchanged
	 * reimport doesn't dirty the mesh. Returns whether anything changed. Editor only.
	 */
	bool WriteHumanoidMetadata(USkeletalMesh* Mesh, const FVRMAvatarData& Avatar)
	{
		UEditorAssetSubsystem* Assets = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
		if (!Assets || !Mesh)
		{
			return false;
		}
		const FString Prefix = VRM::HumanoidMetadataPrefix;
		TMap<FName, FString> Existing;
		for (const TPair<FName, FString>& Tag : Assets->GetMetadataTagValues(Mesh))
		{
			const FString Key = Tag.Key.ToString();
			if (Key.StartsWith(Prefix) || Key == VRM::HumanoidMetadataVersionKey)
			{
				Existing.Add(Tag.Key, Tag.Value);
			}
		}
		const TMap<FName, FString> Wanted = VRM::MakeHumanoidMetadata(Avatar);
		if (Existing.OrderIndependentCompareEqual(Wanted))
		{
			return false;
		}
		for (const TPair<FName, FString>& Tag : Existing)
		{
			if (!Wanted.Contains(Tag.Key))
			{
				Assets->RemoveMetadataTag(Mesh, Tag.Key);
			}
		}
		for (const TPair<FName, FString>& Tag : Wanted)
		{
			const FString* Old = Existing.Find(Tag.Key);
			if (!Old || *Old != Tag.Value)
			{
				Assets->SetMetadataTag(Mesh, Tag.Key, Tag.Value);
			}
		}
		return true;
	}
}

void UVRMAvatarDescriptionPipeline::ApplyProjectSettings()
{
	// The project setting is the default for new pipelines; the import dialog decides per import.
	if (const UVRMInterchangeSettings* Settings = GetDefault<UVRMInterchangeSettings>())
	{
		bGenerateAvatarDescription = Settings->bGenerateAvatarDescription;
	}
}

void UVRMAvatarDescriptionPipeline::ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath)
{
	Super::ExecutePipeline(BaseNodeContainer, SourceDatas, ContentBasePath);
	StagedAvatar = FVRMAvatarData();
	StagedSourceHash.Reset();
	LastDescription.Reset();
	// What this pipeline logs belongs to this import's message log page (P6.3).
	const VRM::ImportMessages::FScope MessageScope(GetFirstSourceFile(SourceDatas));
	if (!BeginImport(SourceDatas, ContentBasePath) || !BaseNodeContainer || !bGenerateAvatarDescription)
	{
		return;
	}

	bool bHaveAvatar = false;
	if (const UInterchangeVRMNode* VRMNode = UInterchangeVRMNode::Find(*BaseNodeContainer))
	{
		// The translator read it (the file isn't opened again).
		bHaveAvatar = VRMNode->GetAvatarData(StagedAvatar);
		VRMNode->GetSourceHash(StagedSourceHash);
	}
	else
	{
		// A container the VRM translator didn't make: read the file. The model is needed to name
		// the bones and morph targets the avatar points at.
		FString Error;
		FVRMParsedModel Model;
		const TSharedPtr<const FVRMDocument> Document = FVRMDocument::LoadFile(GetSourceFilename(), Error);
		if (Document.IsValid() && VRM::BuildParsedModel(*Document, Model))
		{
			TArray<FString> AvatarWarnings;
			bHaveAvatar = VRM::BuildAvatarData(*Document, Model, StagedAvatar, &AvatarWarnings);
			StagedSourceHash = LexToString(Document->GetSourceHash());
			for (const FString& AvatarWarning : AvatarWarnings)
			{
				UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] %s"), *AvatarWarning);
			}
		}
	}

	if (bHaveAvatar)
	{
		WaitForSkeletalMesh();
	}
}

void UVRMAvatarDescriptionPipeline::OnSkeletalMeshImported(USkeletalMesh* Mesh, bool bIsAReimport)
{
	const FString Name = Mesh->GetName() + TEXT("_Avatar");
	bool bReused = false;
	UVRMAvatarDescription* Description = Cast<UVRMAvatarDescription>(
		CreateOrReuseAsset(UVRMAvatarDescription::StaticClass(), GetCharacterFolder(), Name, bOverwriteExisting, bReused));
	if (!Description)
	{
		return;
	}
	Description->Avatar = StagedAvatar;
	Description->Mesh = Mesh;
	Description->SourceFilename = GetSourceFilename();
	Description->SourceHash = StagedSourceHash;
	// The spring pipeline runs first in the stack and names its asset <Mesh>_SpringData, in its
	// SubFolder ("SpringBones" by default) or, with SubFolder empty, in the character folder.
	const FString SpringDataName = Mesh->GetName() + TEXT("_SpringData");
	UObject* SpringData = FindExistingAsset(GetCharacterFolder() / TEXT("SpringBones"), SpringDataName);
	if (!SpringData)
	{
		SpringData = FindExistingAsset(GetCharacterFolder(), SpringDataName);
	}
	if (SpringData)
	{
		Description->SpringData = SpringData;
	}
	Description->MarkPackageDirty();
	LastDescription = Description;

	// The humanoid map, on the mesh itself, for VMCLiveLink's "Create Mapping" (no plugin dependency).
	if (WriteHumanoidMetadata(Mesh, StagedAvatar))
	{
		Mesh->MarkPackageDirty();
	}

	const FString License = VRM::DescribeLicense(StagedAvatar.Meta);
	UE_LOG(LogVRMInterchange, Log, TEXT("[VRMInterchange] %s: %s"), *Description->GetPathName(), *License);
	if (bShowLicenseNotification && !License.IsEmpty())
	{
		FVRMImportReport::Get().SetLicense(License); // shown with the import's notification (P6.3)
	}
}
