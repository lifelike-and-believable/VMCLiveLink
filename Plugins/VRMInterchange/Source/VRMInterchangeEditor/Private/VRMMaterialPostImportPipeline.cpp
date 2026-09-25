// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMMaterialPostImportPipeline.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "InterchangeSourceData.h"
#include "ObjectTools.h"
#include "VRMInterchangeLog.h"

namespace
{
	const TCHAR* const VRMInstancePrefix = TEXT("MI_VRM_");

	/** The master UMaterial at the root of an instance chain, or nullptr. */
	const UMaterial* GetMasterMaterialOf(const UMaterialInterface* MaterialInterface)
	{
		const UMaterialInterface* Current = MaterialInterface;
		for (int32 Guard = 0; Current && Guard < 32; ++Guard)
		{
			if (const UMaterial* AsMaterial = Cast<UMaterial>(Current))
			{
				return AsMaterial;
			}
			const UMaterialInstance* AsInstance = Cast<UMaterialInstance>(Current);
			Current = AsInstance ? AsInstance->Parent.Get() : nullptr;
		}
		return nullptr;
	}

	FString GetFolderPath(const UObject* Object)
	{
		return FPackageName::GetLongPackagePath(Object->GetOutermost()->GetName());
	}
}

void UVRMMaterialPostImportPipeline::ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath)
{
	Super::ExecutePipeline(BaseNodeContainer, SourceDatas, ContentBasePath);

	CharacterInstanceName.Reset();
	ImportedInstances.Reset();
	for (const UInterchangeSourceData* SourceData : SourceDatas)
	{
		if (SourceData)
		{
			// Must match the display name VRMTranslator gives the character material instance.
			CharacterInstanceName = ObjectTools::SanitizeObjectName(
				FString(VRMInstancePrefix) + FPaths::GetBaseFilename(SourceData->GetFilename()));
			break;
		}
	}
}

bool UVRMMaterialPostImportPipeline::CanExecuteOnAnyThread(EInterchangePipelineTask PipelineTask)
{
	return PipelineTask != EInterchangePipelineTask::PostImport;
}

void UVRMMaterialPostImportPipeline::ExecutePostImportPipeline(const UInterchangeBaseNodeContainer* BaseNodeContainer, const FString& NodeKey, UObject* CreatedAsset, bool bIsAReimport)
{
	Super::ExecutePostImportPipeline(BaseNodeContainer, NodeKey, CreatedAsset, bIsAReimport);

	if (!bParentMaterialsToCharacterInstance)
	{
		return;
	}

	UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(CreatedAsset);
	if (!Instance || !Instance->GetName().StartsWith(VRMInstancePrefix))
	{
		return;
	}

	ImportedInstances.AddUnique(Instance);
	ResolveParents();
}

void UVRMMaterialPostImportPipeline::ResolveParents()
{
	TArray<UMaterialInstanceConstant*> Instances;
	for (const TWeakObjectPtr<UMaterialInstanceConstant>& Weak : ImportedInstances)
	{
		if (UMaterialInstanceConstant* Instance = Weak.Get())
		{
			Instances.Add(Instance);
		}
	}

	// Per-material instances are MI_VRM_<Character>_<Material> in the same folder as the
	// character instance MI_VRM_<Character>. Nothing happens until the character instance arrives.
	UMaterialInstanceConstant* Parent = nullptr;
	for (UMaterialInstanceConstant* Candidate : Instances)
	{
		if (!CharacterInstanceName.IsEmpty() && Candidate->GetName() == CharacterInstanceName)
		{
			Parent = Candidate;
			break;
		}
	}
	if (!Parent)
	{
		return;
	}

	const FString Prefix = Parent->GetName() + TEXT("_");
	const FString ParentFolder = GetFolderPath(Parent);
	const UMaterial* ParentMaster = GetMasterMaterialOf(Parent);

	for (UMaterialInstanceConstant* Child : Instances)
	{
		if (Child == Parent || Child->Parent == Parent)
		{
			continue;
		}
		if (!Child->GetName().StartsWith(Prefix) || GetFolderPath(Child) != ParentFolder)
		{
			continue;
		}

		// Only reparent within the same master material, so overrides keep their meaning.
		const UMaterial* ChildMaster = GetMasterMaterialOf(Child);
		if (!ChildMaster || ChildMaster != ParentMaster)
		{
			continue;
		}

		Child->SetParentEditorOnly(Parent);
		Child->PostEditChange();
		Child->MarkPackageDirty();
		UE_LOG(LogVRMInterchange, Verbose, TEXT("[VRMInterchange] Parented '%s' to '%s'."), *Child->GetName(), *Parent->GetName());
	}
}
