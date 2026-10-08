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
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "Animation/AnimBlueprint.h"
#include "VRMActorBlueprintWiring.h"
#include "VRMAvatarDescription.h"
#include "VRMAvatarParser.h"
#include "VRMDocument.h"
#include "VRMImportReport.h"
#include "VRMInterchangeLog.h"
#include "VRMInterchangeSettings.h"
#include "VRMParsedModel.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace
{
	/**
	 * Writes the humanoid map onto the mesh as metadata for VMCLiveLink (P4.4, D-4), replacing any
	 * left by an earlier import. Touches nothing when the tags are already right, so an unchanged
	 * reimport doesn't dirty the mesh. Returns whether anything changed. Writes the package's metadata
	 * itself: UEditorAssetSubsystem's metadata functions do nothing during Play In Editor. Game thread.
	 */
	bool WriteHumanoidMetadata(USkeletalMesh* Mesh, const FVRMAvatarData& Avatar)
	{
		if (!Mesh)
		{
			return false;
		}
		FMetaData& MetaData = Mesh->GetPackage()->GetMetaData();
		const FString Prefix = VRM::HumanoidMetadataPrefix;
		TMap<FName, FString> Existing;
		if (const TMap<FName, FString>* Tags = FMetaData::GetMapForObject(Mesh))
		{
			for (const TPair<FName, FString>& Tag : *Tags)
			{
				const FString Key = Tag.Key.ToString();
				if (Key.StartsWith(Prefix) || Key == VRM::HumanoidMetadataVersionKey)
				{
					Existing.Add(Tag.Key, Tag.Value);
				}
			}
		}
		const TMap<FName, FString> Wanted = VRM::MakeHumanoidMetadata(Avatar);
		if (Existing.OrderIndependentCompareEqual(Wanted))
		{
			return false;
		}
		Mesh->Modify();
		for (const TPair<FName, FString>& Tag : Existing)
		{
			if (!Wanted.Contains(Tag.Key))
			{
				MetaData.RemoveValue(Mesh, Tag.Key);
			}
		}
		for (const TPair<FName, FString>& Tag : Wanted)
		{
			const FString* Old = Existing.Find(Tag.Key);
			if (!Old || *Old != Tag.Value)
			{
				MetaData.SetValue(Mesh, Tag.Key, *Tag.Value);
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

	// The face: the Live Link AnimBlueprint this import made gets a VRM Expressions node for this
	// avatar description (one it reused only gets an unset avatar description filled in). It's the
	// one the Live Link pipeline reported for this file, wherever it put it; nothing else is touched.
	// The Live Link pipeline adds the node instead when it runs after this one.
	if (const TArray<FVRMImportReport::FFile>& Files = FVRMImportReport::Get().GetPendingFiles(); Files.Num() > 0)
	{
		const FString Prefix = TEXT("ABP_LL_VRM_") + Mesh->GetName();
		for (const FVRMImportReport::FAsset& Asset : Files.Last().Assets)
		{
			UAnimBlueprint* AnimBlueprint = Asset.Name.StartsWith(Prefix) ? Cast<UAnimBlueprint>(Asset.Path.ResolveObject()) : nullptr;
			if (AnimBlueprint)
			{
				VRMPipeline::AddExpressionsNode(AnimBlueprint, Description, /*bInsert*/ !Asset.bUpdated);
				break;
			}
		}
	}

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
