// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMLiveLinkPostImportPipeline.h"

#include "InterchangeSourceData.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Engine/Blueprint.h"
#include "Components/SkeletalMeshComponent.h"
#include "VRMInterchangeSettings.h"
#include "VRMActorBlueprintWiring.h"
#include "Animation/AnimInstance.h"

#if WITH_EDITOR
void UVRMLiveLinkPostImportPipeline::PostInitProperties()
{
	Super::PostInitProperties();
	// The project setting is the default for new pipelines; the import dialog decides per import.
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		if (const UVRMInterchangeSettings* Settings = GetDefault<UVRMInterchangeSettings>())
		{
			bGenerateLiveLinkEnabledActor = Settings->bGenerateLiveLinkEnabledActor;
		}
	}
}
#endif

// Stage the work; the assets are made once the import's skeletal mesh exists.
void UVRMLiveLinkPostImportPipeline::ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath)
{
	Super::ExecutePipeline(BaseNodeContainer, SourceDatas, ContentBasePath);
	// Only this instance's flags count (seeded from the project settings in PostInitProperties).
	// Either asset is reason enough to run: the retarget actor doesn't need the Live Link actor.
	if (BeginImport(SourceDatas, ContentBasePath) && BaseNodeContainer && (bGenerateLiveLinkEnabledActor || bGenerateLiveLinkRetargetActor))
	{
		WaitForSkeletalMesh();
	}
}

void UVRMLiveLinkPostImportPipeline::OnSkeletalMeshImported(USkeletalMesh* Mesh, bool bIsAReimport)
{
	const FString CharacterName = Mesh->GetName();
	const FString LiveLinkFolder = GetCharacterFolder() / TEXT("LiveLink");

	if (bGenerateLiveLinkEnabledActor)
	{
		// Live Link actor and its AnimBP, then wire them up
		bool bActorReused = false;
		bool bAnimReused = false;
		UBlueprint* ActorBP = Cast<UBlueprint>(DuplicateTemplateAsset(TEXT("/VRMInterchange/BP_LL_VRM_Template.BP_LL_VRM_Template"),
			LiveLinkFolder, FString::Printf(TEXT("BP_LL_VRM_%s"), *CharacterName), bOverwriteExisting, bActorReused));
		UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(DuplicateTemplateAsset(TEXT("/VRMInterchange/Animation/ABP_LL_VRM_Template.ABP_LL_VRM_Template"),
			LiveLinkFolder / AnimationSubFolder, FString::Printf(TEXT("ABP_LL_VRM_%s"), *CharacterName), bOverwriteExisting, bAnimReused));
		if (AnimBP)
		{
			AnimBP->SetPreviewMesh(Mesh);
			AnimBP->MarkPackageDirty();
		}
		if (ActorBP)
		{
			VRMPipeline::SetActorBlueprintMesh(ActorBP, Mesh, AnimBP ? TSubclassOf<UAnimInstance>(AnimBP->GeneratedClass) : TSubclassOf<UAnimInstance>());
		}
	}

	if (bGenerateLiveLinkRetargetActor)
	{
		bool bReused = false;
		UBlueprint* RetargetActorBP = Cast<UBlueprint>(DuplicateTemplateAsset(TEXT("/VRMInterchange/BP_LL_VRM_To_UE5_Template.BP_LL_VRM_To_UE5_Template"),
			LiveLinkFolder, FString::Printf(TEXT("BP_LL_VRM_To_UE5_%s"), *CharacterName), bOverwriteExisting, bReused));
		if (RetargetActorBP)
		{
			// The template retargets the pose of the mesh in its "VRM Character" variable.
			VRMPipeline::SetBlueprintObjectVariable(RetargetActorBP, TEXT("VRM Character"), Mesh);
		}
	}
}
