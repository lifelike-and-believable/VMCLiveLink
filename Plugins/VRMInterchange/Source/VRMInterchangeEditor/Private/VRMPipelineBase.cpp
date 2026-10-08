// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMPipelineBase.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Engine/Blueprint.h"
#include "Engine/SkeletalMesh.h"
#include "IAssetTools.h"
#include "InterchangeSourceData.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "VRMImportMessages.h"
#include "VRMImportReport.h"
#include "VRMInterchangeLog.h"
#include "VRMPipelineTargets.h"
#include "UObject/UObjectIterator.h"

// Where the project settings apply. Not at construction: Interchange copies pipelines (from the
// pipeline assets for an import, from the import data for a reimport, from the first file's for
// "Import All") with DuplicateObject, which writes only what differs from the class defaults, so a
// copy seeded at construction would take the project setting wherever the choice equals the default.
bool UVRMPipelineBase::FollowsProjectSettings() const
{
	// The plugin's own pipeline assets (DefaultPipelines). A studio's copy elsewhere keeps its values,
	// and so does a pipeline stored in an asset's import data (not an asset).
	return IsAsset() && GetPackage()->GetName().StartsWith(TEXT("/VRMInterchange/"));
}

void UVRMPipelineBase::PostLoad()
{
	Super::PostLoad();
	// Not the values saved in the asset: a save in a project with other settings would bake those in.
	if (FollowsProjectSettings())
	{
		ApplyProjectSettings();
	}
}

void UVRMPipelineBase::PreDialogCleanup(const FName PipelineStackName)
{
	Super::PreDialogCleanup(PipelineStackName);
	// The import dialog has just restored the values used last time (LoadSettings); the project
	// settings are the defaults instead. Interchange doesn't call this for a reimport.
	ApplyProjectSettings();
}

void UVRMPipelineBase::ApplyProjectSettingsToLoadedAssets()
{
	for (TObjectIterator<UVRMPipelineBase> It; It; ++It)
	{
		if (It->FollowsProjectSettings())
		{
			It->ApplyProjectSettings();
		}
	}
}

void UVRMPipelineBase::ExecutePostImportPipeline(const UInterchangeBaseNodeContainer* BaseNodeContainer, const FString& NodeKey, UObject* CreatedAsset, bool bIsAReimport)
{
	Super::ExecutePostImportPipeline(BaseNodeContainer, NodeKey, CreatedAsset, bIsAReimport);
	HandleImportedAsset(CreatedAsset, bIsAReimport);
}

bool UVRMPipelineBase::CanExecuteOnAnyThread(EInterchangePipelineTask PipelineTask)
{
	return PipelineTask != EInterchangePipelineTask::PostImport;
}

void UVRMPipelineBase::HandleImportedAsset(UObject* CreatedAsset, bool bIsAReimport)
{
	if (!bWaitingForMesh)
	{
		return;
	}
	if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(CreatedAsset))
	{
		bWaitingForMesh = false;
		// What the pipeline makes from here on is reported for this file (P6.3).
		const VRM::ImportMessages::FScope MessageScope(ImportSourceFilename);
		FVRMImportReport& Report = FVRMImportReport::Get();
		Report.BeginFile(ImportSourceFilename, bIsAReimport);
		Report.AddAsset(Mesh, bIsAReimport);
		OnSkeletalMeshImported(Mesh, bIsAReimport);
	}
}

FString UVRMPipelineBase::GetFirstSourceFile(const TArray<UInterchangeSourceData*>& SourceDatas)
{
	for (const UInterchangeSourceData* Source : SourceDatas)
	{
		if (Source)
		{
			return Source->GetFilename();
		}
	}
	return FString();
}

bool UVRMPipelineBase::BeginImport(const TArray<UInterchangeSourceData*>& SourceDatas, const FString& InContentBasePath)
{
	bWaitingForMesh = false;
	ImportSourceFilename.Reset();
	ImportContentBasePath = InContentBasePath;
	ImportCharacterFolder.Reset();
	for (const UInterchangeSourceData* Source : SourceDatas)
	{
		if (Source)
		{
			ImportSourceFilename = Source->GetFilename();
			ImportCharacterFolder = VRMPipeline::MakeCharacterBasePath(ImportSourceFilename, InContentBasePath);
			return true;
		}
	}
	return false;
}

UObject* UVRMPipelineBase::FindExistingAsset(const FString& Folder, const FString& Name)
{
	const FString PackageName = Folder / Name;
	const FString ObjectPath = PackageName + TEXT(".") + Name;
	if (UObject* InMemory = FindObject<UObject>(nullptr, *ObjectPath))
	{
		return InMemory;
	}
	if (FPackageName::DoesPackageExist(PackageName))
	{
		return StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	return nullptr;
}

namespace
{
	/** A free name for Folder/Name: the name itself if nothing has it, else Name_1, Name_2... */
	void MakeUniqueName(const FString& Folder, const FString& Name, FString& OutPackageName, FString& OutAssetName)
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		AssetTools.CreateUniqueAssetName(Folder / Name, TEXT(""), OutPackageName, OutAssetName);
	}
}

UObject* UVRMPipelineBase::CreateOrReuseAsset(UClass* Class, const FString& Folder, const FString& Name, bool bReuseExisting, bool& bOutReused)
{
	bOutReused = false;
	if (!Class || Folder.IsEmpty() || Name.IsEmpty())
	{
		return nullptr;
	}
	if (bReuseExisting)
	{
		if (UObject* Existing = FindExistingAsset(Folder, Name))
		{
			if (Existing->IsA(Class))
			{
				bOutReused = true;
				Existing->Modify();
				FVRMImportReport::Get().AddAsset(Existing, /*bUpdated*/ true);
				return Existing;
			}
			UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] '%s' is a %s, not a %s; creating the new asset under another name."),
				*Existing->GetPathName(), *Existing->GetClass()->GetName(), *Class->GetName());
		}
	}

	FString PackageName, AssetName;
	MakeUniqueName(Folder, Name, PackageName, AssetName);
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return nullptr;
	}
	UObject* Asset = NewObject<UObject>(Package, Class, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
	FAssetRegistryModule::AssetCreated(Asset);
	Asset->MarkPackageDirty();
	FVRMImportReport::Get().AddAsset(Asset, /*bUpdated*/ false);
	return Asset;
}

UObject* UVRMPipelineBase::DuplicateTemplateAsset(const TCHAR* TemplatePath, const FString& Folder, const FString& Name, bool bReuseExisting, bool& bOutReused)
{
	bOutReused = false;
	if (!TemplatePath || Folder.IsEmpty() || Name.IsEmpty())
	{
		return nullptr;
	}
	UObject* Template = StaticLoadObject(UObject::StaticClass(), nullptr, TemplatePath);
	if (!Template)
	{
		UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] Template '%s' not found."), TemplatePath);
		return nullptr;
	}
	if (bReuseExisting)
	{
		if (UObject* Existing = FindExistingAsset(Folder, Name))
		{
			if (Existing->GetClass() == Template->GetClass())
			{
				bOutReused = true;
				Existing->Modify();
				FVRMImportReport::Get().AddAsset(Existing, /*bUpdated*/ true);
				return Existing;
			}
			UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] '%s' is a %s, not a %s; creating the new asset under another name."),
				*Existing->GetPathName(), *Existing->GetClass()->GetName(), *Template->GetClass()->GetName());
		}
	}

	FString PackageName, AssetName;
	MakeUniqueName(Folder, Name, PackageName, AssetName);
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* Duplicate = AssetTools.DuplicateAsset(AssetName, FPackageName::GetLongPackagePath(PackageName), Template);
	if (!Duplicate)
	{
		UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] Could not copy '%s' to '%s'."), TemplatePath, *PackageName);
		return nullptr;
	}
	if (UBlueprint* Blueprint = Cast<UBlueprint>(Duplicate))
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipSave);
	}
	Duplicate->MarkPackageDirty();
	FVRMImportReport::Get().AddAsset(Duplicate, /*bUpdated*/ false);
	return Duplicate;
}
