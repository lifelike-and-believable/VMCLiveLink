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
			SetPreviewMeshOnAnimBP(AnimBP, Mesh);
		}
		if (ActorBP)
		{
			AssignSkeletalMeshToActorBP(ActorBP, Mesh);
			if (AnimBP)
			{
				AssignAnimBPToActorBP(ActorBP, AnimBP);
			}
			ActorBP->MarkPackageDirty();
		}
	}

	if (bGenerateLiveLinkRetargetActor)
	{
		bool bReused = false;
		UBlueprint* RetargetActorBP = Cast<UBlueprint>(DuplicateTemplateAsset(TEXT("/VRMInterchange/BP_LL_VRM_To_UE5_Template.BP_LL_VRM_To_UE5_Template"),
			LiveLinkFolder, FString::Printf(TEXT("BP_LL_VRM_To_UE5_%s"), *CharacterName), bOverwriteExisting, bReused));
		if (RetargetActorBP)
		{
			AssignSkeletalMeshToActorBPProperty(RetargetActorBP, Mesh);
			RetargetActorBP->MarkPackageDirty();
		}
	}
}

bool UVRMLiveLinkPostImportPipeline::AssignSkeletalMeshToActorBP(UObject* ActorBlueprintObj, USkeletalMesh* SkeletalMesh) const
{
	UBlueprint* BP = Cast<UBlueprint>(ActorBlueprintObj);
	if (!BP || !SkeletalMesh) return false;
	if (!BP->GeneratedClass) {
		FKismetEditorUtilities::CompileBlueprint(BP);
	} UClass* GenClass = BP->GeneratedClass;
	if (!GenClass) return false;
	UObject* CDO = GenClass->GetDefaultObject();
	if (!CDO) return false;
	USkeletalMeshComponent* FoundComp = nullptr;
	for (TObjectPtr<UActorComponent> Comp : CastChecked<AActor>(CDO)->GetComponents()) {
		if (USkeletalMeshComponent* SKC = Cast<USkeletalMeshComponent>(Comp)) {
			FoundComp = SKC; break;
		}
	}
	if (!FoundComp) { return false; }
	FoundComp->SetSkeletalMesh(SkeletalMesh);
	FoundComp->MarkPackageDirty();
	BP->MarkPackageDirty();
	return true;
}

bool UVRMLiveLinkPostImportPipeline::AssignSkeletalMeshToActorBPProperty(UObject* ActorBlueprintObj, USkeletalMesh* SkeletalMesh) const
{
	UBlueprint* BP = Cast<UBlueprint>(ActorBlueprintObj);
	if (!BP || !SkeletalMesh) return false;
	if (!BP->GeneratedClass) {
		FKismetEditorUtilities::CompileBlueprint(BP);
	} UClass* GenClass = BP->GeneratedClass;
	if (!GenClass) return false;
	UObject* CDO = GenClass->GetDefaultObject();
	if (!CDO) return false;
	if (FObjectProperty* ObjProp = FindFProperty<FObjectProperty>(GenClass, TEXT("VRM Character")))
	{
		ObjProp->SetObjectPropertyValue_InContainer(CDO, SkeletalMesh);
	}

	BP->MarkPackageDirty();
	return true;
}

bool UVRMLiveLinkPostImportPipeline::AssignAnimBPToActorBP(UObject* ActorBlueprintObj, UAnimBlueprint* AnimBP) const
{
	if(!ActorBlueprintObj||!AnimBP) return false; if(!AnimBP->GeneratedClass){ FKismetEditorUtilities::CompileBlueprint(AnimBP);} UClass* AnimClass=AnimBP->GeneratedClass; if(!AnimClass) return false; UBlueprint* BP=Cast<UBlueprint>(ActorBlueprintObj); if(!BP||!BP->GeneratedClass){ return false; } UObject* CDO=BP->GeneratedClass->GetDefaultObject(); if(!CDO) return false; USkeletalMeshComponent* FoundComp=nullptr; for (TObjectPtr<UActorComponent> Comp : CastChecked<AActor>(CDO)->GetComponents()) { if(USkeletalMeshComponent* SKC = Cast<USkeletalMeshComponent>(Comp)) { FoundComp = SKC; break; }} if(!FoundComp) return false; FoundComp->SetAnimInstanceClass(AnimClass); FoundComp->MarkPackageDirty(); BP->MarkPackageDirty(); AnimBP->MarkPackageDirty(); return true; }

bool UVRMLiveLinkPostImportPipeline::SetPreviewMeshOnAnimBP(UAnimBlueprint* AnimBP, USkeletalMesh* SkeletalMesh) const
{
	if(!AnimBP||!SkeletalMesh) return false; AnimBP->SetPreviewMesh(SkeletalMesh); AnimBP->MarkPackageDirty(); return true; }
