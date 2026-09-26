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
#include "VRMPipelineTargets.h"

#if WITH_EDITOR
#include "UnrealEdGlobals.h"
#include "Subsystems/ImportSubsystem.h"
#endif

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

// Stage names/paths and defer creation to post-import (after dialog confirmation)
void UVRMLiveLinkPostImportPipeline::ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath)
{
#if WITH_EDITOR
	Super::ExecutePipeline(BaseNodeContainer, SourceDatas, ContentBasePath);

	// Only this instance's flags count (seeded from the project settings in PostInitProperties).
	// Either asset is reason enough to run: the retarget actor doesn't need the Live Link actor.
	if(!bGenerateLiveLinkEnabledActor && !bGenerateLiveLinkRetargetActor) return;
	if(!BaseNodeContainer) return;

	const UInterchangeSourceData* Source=nullptr;
	for(const UInterchangeSourceData* SD:SourceDatas){ if(SD){ Source=SD; break; }}
	if(!Source) return;

	const FString Filename = Source->GetFilename();
	const FString CharacterBasePath = VRMPipeline::MakeCharacterBasePath(Filename, ContentBasePath);
	DeferredPackagePath = CharacterBasePath;
	DeferredContentBasePath = ContentBasePath;
	DeferredSourceFilename = Filename;

	const FString CharacterName = FPaths::GetBaseFilename(Filename);
	// LiveLink folder root
	const FString LiveLinkFolder = CharacterBasePath / TEXT("LiveLink");
	const FString AnimFolder = LiveLinkFolder / AnimationSubFolder;

	// Cache desired asset names and locations for post-import creation
	DeferredActorBPPath = LiveLinkFolder;
	DeferredAnimBPPath = AnimFolder;
	DeferredActorBPName = FString::Printf(TEXT("BP_LL_VRM_%s"), *CharacterName);
	DeferredAnimBPName  = FString::Printf(TEXT("ABP_LL_VRM_%s"), *CharacterName);
	DeferredRetargetActorBPName = FString::Printf(TEXT("ABP_LL_VRM_To_UE5_%s"), *CharacterName);
	bDeferredOverwrite = bOverwriteExisting;

	// Do NOT duplicate here; defer until after import is confirmed and skeletal assets exist
	RegisterPostImportCommit();
#endif
}

#if WITH_EDITOR
void UVRMLiveLinkPostImportPipeline::BeginDestroy()
{
	UnregisterPostImportCommit();
	Super::BeginDestroy();
}
#endif

#if WITH_EDITOR

UObject* UVRMLiveLinkPostImportPipeline::DuplicateTemplate(const TCHAR* TemplatePath, const FString& TargetPackagePath, const FString& DesiredName, bool bOverwrite) const
{
	if(!TemplatePath||TargetPackagePath.IsEmpty()||DesiredName.IsEmpty()) return nullptr; UObject* TemplateObj = StaticLoadObject(UObject::StaticClass(), nullptr, TemplatePath); if(!TemplateObj) return nullptr; FString NewAssetPath = TargetPackagePath / DesiredName; FAssetToolsModule& AssetToolsModule=FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools"); FString UniquePath,UniqueName; if(!bOverwrite) { AssetToolsModule.Get().CreateUniqueAssetName(NewAssetPath,TEXT(""),UniquePath,UniqueName);} else { UniquePath=NewAssetPath; UniqueName=DesiredName; } const FString LongPackage = UniquePath.StartsWith(TEXT("/"))?UniquePath:TEXT("/")+UniquePath; UPackage* Pkg=CreatePackage(*LongPackage); if(!Pkg) return nullptr; UObject* Duplicated=StaticDuplicateObject(TemplateObj,Pkg,*UniqueName); if(!Duplicated) return nullptr; FAssetRegistryModule::AssetCreated(Duplicated); if(UBlueprint* BP=Cast<UBlueprint>(Duplicated)) { FKismetEditorUtilities::CompileBlueprint(BP); } return Duplicated; }

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

void UVRMLiveLinkPostImportPipeline::RegisterPostImportCommit()
{
	if (ImportPostHandle.IsValid())
	{
		return;
	}
	if (UImportSubsystem* ImportSubsystem = GEditor ? GEditor->GetEditorSubsystem<UImportSubsystem>() : nullptr)
	{
		ImportPostHandle = ImportSubsystem->OnAssetPostImport.AddUObject(this, &UVRMLiveLinkPostImportPipeline::OnAssetPostImport);
	}
}

void UVRMLiveLinkPostImportPipeline::UnregisterPostImportCommit()
{
	if (ImportPostHandle.IsValid())
	{
		if (UImportSubsystem* ImportSubsystem = GEditor ? GEditor->GetEditorSubsystem<UImportSubsystem>() : nullptr)
		{
			ImportSubsystem->OnAssetPostImport.Remove(ImportPostHandle);
		}
		ImportPostHandle.Reset();
	}
}

void UVRMLiveLinkPostImportPipeline::OnAssetPostImport(UFactory* InFactory, UObject* InCreatedObject)
{
	if (bDeferredCompleted || !InCreatedObject)
	{
		return;
	}

	// Only this import's mesh: every import in the editor reports its assets here (VRMPipelineTargets.h).
	USkeletalMesh* SkelMesh = VRMPipeline::ResolveImportedMesh(InCreatedObject, DeferredSourceFilename, DeferredPackagePath, DeferredContentBasePath);
	if (!SkelMesh)
	{
		return;
	}

	if(bGenerateLiveLinkEnabledActor) {
		// Create LiveLink Actor + AnimBP
		const FString EffectiveCharacterName = ResolveEffectiveCharacterName(SkelMesh, DeferredPackagePath);
		if (!EffectiveCharacterName.IsEmpty())
		{
			DeferredActorBPName = FString::Printf(TEXT("BP_LL_VRM_%s"), *EffectiveCharacterName);
			DeferredAnimBPName  = FString::Printf(TEXT("ABP_LL_VRM_%s"), *EffectiveCharacterName);
			// Create assets now, then wire them up
			UBlueprint* ActorBP = Cast<UBlueprint>(DuplicateTemplate(TEXT("/VRMInterchange/BP_LL_VRM_Template.BP_LL_VRM_Template"), DeferredActorBPPath, DeferredActorBPName, bDeferredOverwrite));
			UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(DuplicateTemplate(TEXT("/VRMInterchange/Animation/ABP_LL_VRM_Template.ABP_LL_VRM_Template"), DeferredAnimBPPath, DeferredAnimBPName, bDeferredOverwrite));

			if (SkelMesh && AnimBP)
			{
				SetPreviewMeshOnAnimBP(AnimBP, SkelMesh);
			}
			if (ActorBP && SkelMesh)
			{
				AssignSkeletalMeshToActorBP(ActorBP, SkelMesh);
				if (AnimBP) AssignAnimBPToActorBP(ActorBP, AnimBP);
			}

			// Leave packages dirty; let editor Save/SCC handle persistence
			if (AnimBP) { AnimBP->MarkPackageDirty(); }
			if (ActorBP) { ActorBP->MarkPackageDirty(); }
		}
	}

	if (bGenerateLiveLinkRetargetActor)
	{
		// Create Retarget Actor
		const FString EffectiveCharacterName = ResolveEffectiveCharacterName(SkelMesh, DeferredPackagePath);
		if (!EffectiveCharacterName.IsEmpty())
		{
			DeferredRetargetActorBPName = FString::Printf(TEXT("BP_LL_VRM_To_UE5_%s"), *EffectiveCharacterName);
			UBlueprint* RetargetActorBP = Cast<UBlueprint>(DuplicateTemplate(TEXT("/VRMInterchange/BP_LL_VRM_To_UE5_Template.BP_LL_VRM_To_UE5_Template"), DeferredActorBPPath, DeferredRetargetActorBPName, bDeferredOverwrite));
			if (RetargetActorBP && SkelMesh)
			{
				AssignSkeletalMeshToActorBPProperty(RetargetActorBP, SkelMesh);
			}
			if (RetargetActorBP) { RetargetActorBP->MarkPackageDirty(); }
		}
	}

	bDeferredCompleted = true;
	UnregisterPostImportCommit();
}

FString UVRMLiveLinkPostImportPipeline::ResolveEffectiveCharacterName(USkeletalMesh* SkelMesh, const FString& PackagePath) const
{
	if (SkelMesh)
	{
		return SkelMesh->GetName();
	}

	// Fallback: last segment of the package path (e.g., /Game/MyChar -> MyChar)
	int32 SlashIdx = INDEX_NONE;
	if (PackagePath.FindLastChar(TEXT('/'), SlashIdx) && SlashIdx != INDEX_NONE && SlashIdx + 1 < PackagePath.Len())
	{
		return PackagePath.Mid(SlashIdx + 1);
	}
	return PackagePath.IsEmpty() ? TEXT("Character") : PackagePath;
}
#endif