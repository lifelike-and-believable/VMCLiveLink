// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMAvatarDescriptionPipeline.h"

#include "Engine/SkeletalMesh.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "InterchangeVRMNode.h"
#include "Misc/App.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "VRMAvatarDescription.h"
#include "VRMAvatarParser.h"
#include "VRMDocument.h"
#include "VRMInterchangeLog.h"
#include "VRMInterchangeSettings.h"
#include "VRMParsedModel.h"
#include "Widgets/Notifications/SNotificationList.h"

void UVRMAvatarDescriptionPipeline::PostInitProperties()
{
	Super::PostInitProperties();
	// The project setting is the default for new pipelines; the import dialog decides per import.
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		if (const UVRMInterchangeSettings* Settings = GetDefault<UVRMInterchangeSettings>())
		{
			bGenerateAvatarDescription = Settings->bGenerateAvatarDescription;
		}
	}
}

void UVRMAvatarDescriptionPipeline::ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath)
{
	Super::ExecutePipeline(BaseNodeContainer, SourceDatas, ContentBasePath);
	StagedAvatar = FVRMAvatarData();
	StagedSourceHash.Reset();
	LastDescription.Reset();
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
			bHaveAvatar = VRM::BuildAvatarData(*Document, Model, StagedAvatar);
			StagedSourceHash = LexToString(Document->GetSourceHash());
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
	// The spring pipeline runs first in the stack and names its asset <Mesh>_SpringData.
	if (UObject* SpringData = FindExistingAsset(GetCharacterFolder() / TEXT("SpringBones"), Mesh->GetName() + TEXT("_SpringData")))
	{
		Description->SpringData = SpringData;
	}
	Description->MarkPackageDirty();
	LastDescription = Description;

	const FString License = VRM::DescribeLicense(StagedAvatar.Meta);
	UE_LOG(LogVRMInterchange, Log, TEXT("[VRMInterchange] %s: %s"), *Description->GetPathName(), *License);
	if (bShowLicenseNotification && !License.IsEmpty() && !FApp::IsUnattended() && FSlateApplication::IsInitialized())
	{
		FNotificationInfo Info(FText::FromString(FString::Printf(TEXT("VRM avatar imported: %s"), *Mesh->GetName())));
		Info.SubText = FText::FromString(License);
		Info.ExpireDuration = 10.f;
		Info.bUseLargeFont = false;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
}
