// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMIKRigPostImportPipeline.h"

#include "InterchangeSourceData.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "Modules/ModuleManager.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Rig/IKRigDefinition.h"
#include "VRMInterchangeSettings.h"
#include "VRMInterchangeLog.h"
#include "VRMPipelineTargets.h"

#if WITH_EDITOR
#include "UnrealEdGlobals.h"
#include "Subsystems/ImportSubsystem.h"
#endif

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

// Stage names/paths and defer creation to post-import (after dialog confirmation)
void UVRMIKRigPostImportPipeline::ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath)
{
#if WITH_EDITOR
	// Only this instance's flag counts (seeded from the project settings in PostInitProperties).
	if (!bGenerateIKRig || !BaseNodeContainer)
	{
		return;
	}

	const UInterchangeSourceData* Source = nullptr;
	for (const UInterchangeSourceData* SD : SourceDatas)
	{
		if (SD) { Source = SD; break; }
	}
	if (!Source)
	{
		return;
	}

	const FString Filename = Source->GetFilename();

	// Character base path and desired names
	const FString CharacterBasePath = VRMPipeline::MakeCharacterBasePath(Filename, ContentBasePath);
	DeferredPackagePath = CharacterBasePath;
	DeferredContentBasePath = ContentBasePath;
	DeferredSourceFilename = Filename;

	const FString CharacterName = FPaths::GetBaseFilename(Filename);
	DeferredAnimFolder = CharacterBasePath / IKRigDefinitionSubFolder;
	DeferredDesiredIKName = FString::Printf(TEXT("%s_%s"), *AssetBaseName, *CharacterName);
	bDeferredOverwriteIK = bOverwriteExisting;

	// Defer actual creation until the import is confirmed and skeletal assets exist
	RegisterPostImportCommit();
#endif
}

#if WITH_EDITOR
void UVRMIKRigPostImportPipeline::BeginDestroy()
{
	UnregisterPostImportCommit();
	Super::BeginDestroy();
}
#endif

#if WITH_EDITOR

bool UVRMIKRigPostImportPipeline::DuplicateTemplateIKRig(const FString& TargetPackagePath, const FString& BaseName, UIKRigDefinition*& OutIKRig, bool bOverwrite) const
{
	OutIKRig = nullptr;
	const TCHAR* TemplatePath = TEXT("/VRMInterchange/Animation/IK_Rig_VRMTemplate.IK_Rig_VRMTemplate");
	UIKRigDefinition* TemplateRig = Cast<UIKRigDefinition>(StaticLoadObject(UIKRigDefinition::StaticClass(), nullptr, TemplatePath));
	if (!TemplateRig)
	{
		UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] IK Rig pipeline: Could not find template IK Rig at '%s'."), TemplatePath);
		return false;
	}

	FString NewAssetPath = TargetPackagePath / BaseName;
	FAssetToolsModule& AssetToolsModule=FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	FString UniquePath, UniqueName;
	if (!bOverwrite)
	{
		AssetToolsModule.Get().CreateUniqueAssetName(NewAssetPath, TEXT(""), UniquePath, UniqueName);
	}
	else
	{
		UniquePath = NewAssetPath;
		UniqueName = BaseName;
	}

	const FString LongPackage = UniquePath.StartsWith(TEXT("/")) ? UniquePath : TEXT("/") + UniquePath;
	UPackage* Pkg = CreatePackage(*LongPackage);
	if (!Pkg) return false;

	UObject* Duplicated = StaticDuplicateObject(TemplateRig, Pkg, *UniqueName);
	if (!Duplicated) { UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] IK Rig pipeline: Failed to duplicate template IK Rig.")); return false; }
	FAssetRegistryModule::AssetCreated(Duplicated);
	OutIKRig = Cast<UIKRigDefinition>(Duplicated);
	return OutIKRig != nullptr;
}

void UVRMIKRigPostImportPipeline::RegisterPostImportCommit()
{
	if (ImportPostHandle.IsValid())
	{
		return;
	}
	if (UImportSubsystem* ImportSubsystem = GEditor ? GEditor->GetEditorSubsystem<UImportSubsystem>() : nullptr)
	{
		ImportPostHandle = ImportSubsystem->OnAssetPostImport.AddUObject(this, &UVRMIKRigPostImportPipeline::OnAssetPostImport);
	}
}

void UVRMIKRigPostImportPipeline::UnregisterPostImportCommit()
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

void UVRMIKRigPostImportPipeline::OnAssetPostImport(UFactory* InFactory, UObject* InCreatedObject)
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

	// Use the SkeletalMesh asset name (fallback to last package path segment) for character substitution
	const FString CharName = ResolveEffectiveCharacterName(SkelMesh, DeferredPackagePath);
	DeferredDesiredIKName = FString::Printf(TEXT("%s_%s"), *AssetBaseName, *CharName);

	// Create IK Rig now (no save)
	UIKRigDefinition* NewIKRig = nullptr;
	if (DuplicateTemplateIKRig(DeferredAnimFolder, DeferredDesiredIKName, NewIKRig, bDeferredOverwriteIK))
	{
		if (NewIKRig && SkelMesh)
		{
			NewIKRig->SetPreviewMesh(SkelMesh, true);
			NewIKRig->MarkPackageDirty();
		}
	}

	bDeferredCompleted = true;
	UnregisterPostImportCommit();
}

FString UVRMIKRigPostImportPipeline::ResolveEffectiveCharacterName(USkeletalMesh* SkelMesh, const FString& PackagePath) const
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
