// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMMaterialPostImportPipeline.h"

#include "VRMImportMessages.h"
#include "Engine/SkeletalMesh.h"
#include "InterchangeMaterialInstanceNode.h"
#include "InterchangeSourceData.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "ObjectTools.h"
#include "UObject/UnrealType.h"
#include "VRMInterchangeLog.h"
#include "VRMMToonMaterial.h"

namespace VRMMaterialPipelinePrivate
{
	const TCHAR* const VRMInstancePrefix = TEXT("MI_VRM_");
	const TCHAR* const MToonCharacterSuffix = TEXT("__MToon");
	const TCHAR* const OutlineSuffix = TEXT("__Outline");

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

	bool IsMToonSurface(const UMaterial* Master)
	{
		return Master && Master->GetPathName() == VRM::MToon::SurfacePath;
	}

	/** An MToon instance's AlphaMode and DoubleSided parameters as its blend mode and two-sided overrides. */
	void ApplyAlphaAndSides(UMaterialInstanceConstant& Instance)
	{
		using namespace VRM::MToon;
		float AlphaMode = 0.f;
		float DoubleSided = 0.f;
		if (!Instance.GetScalarParameterValue(FHashedMaterialParameterInfo(FName(Param::AlphaMode)), AlphaMode)
			|| !Instance.GetScalarParameterValue(FHashedMaterialParameterInfo(FName(Param::DoubleSided)), DoubleSided))
		{
			return;
		}
		const EBlendMode Blend = AlphaMode > 1.5f ? BLEND_Translucent : (AlphaMode > 0.5f ? BLEND_Masked : BLEND_Opaque);
		const bool bTwoSided = DoubleSided > 0.5f;
		FMaterialInstanceBasePropertyOverrides& Overrides = Instance.BasePropertyOverrides;
		if (Overrides.bOverride_BlendMode && Overrides.BlendMode == Blend && Overrides.bOverride_TwoSided && Overrides.TwoSided == bTwoSided)
		{
			return;
		}
		Overrides.bOverride_BlendMode = true;
		Overrides.BlendMode = Blend;
		Overrides.bOverride_TwoSided = true;
		Overrides.TwoSided = bTwoSided;
		Instance.PostEditChange();
		Instance.MarkPackageDirty();
	}
}

void UVRMMaterialPostImportPipeline::ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath)
{
	Super::ExecutePipeline(BaseNodeContainer, SourceDatas, ContentBasePath);

	// What this pipeline logs belongs to this import's message log page (P6.3).
	ImportSourceFile.Reset();
	for (const UInterchangeSourceData* Source : SourceDatas)
	{
		if (Source)
		{
			ImportSourceFile = Source->GetFilename();
			break;
		}
	}
	const VRM::ImportMessages::FScope MessageScope(ImportSourceFile);

	using namespace VRMMaterialPipelinePrivate;
	CharacterInstanceName.Reset();
	ImportedInstances.Reset();
	OutlineInstance.Reset();
	ImportedMesh.Reset();
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

	// The MToon materials must exist before the instances that use them are created.
	bool bUsesMToon = false;
	if (BaseNodeContainer)
	{
		BaseNodeContainer->IterateNodesOfType<UInterchangeMaterialInstanceNode>([&bUsesMToon](const FString&, UInterchangeMaterialInstanceNode* Node)
		{
			FString Parent;
			if (Node && Node->GetCustomParent(Parent) && (Parent == VRM::MToon::SurfacePath || Parent == VRM::MToon::OutlinePath))
			{
				bUsesMToon = true;
			}
		});
	}
	if (bUsesMToon)
	{
		FString Error;
		const VRM::MToon::FMaterials Materials = VRM::MToon::FindOrCreateMToonMaterials(Error);
		if (!Materials.Surface || !Materials.Outline)
		{
			UE_LOG(LogVRMInterchange, Error, TEXT("[VRMInterchange] The MToon materials could not be created (%s); MToon material instances will have no parent."), *Error);
		}
	}
}

bool UVRMMaterialPostImportPipeline::CanExecuteOnAnyThread(EInterchangePipelineTask PipelineTask)
{
	return false;
}

void UVRMMaterialPostImportPipeline::ExecutePostImportPipeline(const UInterchangeBaseNodeContainer* BaseNodeContainer, const FString& NodeKey, UObject* CreatedAsset, bool bIsAReimport)
{
	Super::ExecutePostImportPipeline(BaseNodeContainer, NodeKey, CreatedAsset, bIsAReimport);
	const VRM::ImportMessages::FScope MessageScope(ImportSourceFile);
	HandleImportedAsset(CreatedAsset);
}

void UVRMMaterialPostImportPipeline::HandleImportedAsset(UObject* CreatedAsset)
{
	using namespace VRMMaterialPipelinePrivate;
	if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(CreatedAsset))
	{
		ImportedMesh = Mesh;
		ResolveOverlay();
		return;
	}

	UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(CreatedAsset);
	if (!Instance || !Instance->GetName().StartsWith(VRMInstancePrefix))
	{
		return;
	}

	if (!CharacterInstanceName.IsEmpty() && Instance->GetName() == CharacterInstanceName + OutlineSuffix)
	{
		OutlineInstance = Instance;
		ResolveOverlay();
		return;
	}

	if (IsMToonSurface(GetMasterMaterialOf(Instance)))
	{
		ApplyAlphaAndSides(*Instance);
	}

	if (bParentMaterialsToCharacterInstance)
	{
		ImportedInstances.AddUnique(Instance);
		ResolveParents();
	}
}

void UVRMMaterialPostImportPipeline::ResolveOverlay()
{
	USkeletalMesh* Mesh = ImportedMesh.Get();
	UMaterialInstanceConstant* Outline = OutlineInstance.Get();
	if (!bApplyMToonOutline || !Mesh || !Outline)
	{
		return;
	}
	// By reflection: the property has no stable setter across engine versions.
	FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(USkeletalMesh::StaticClass(), TEXT("OverlayMaterial"));
	if (!Property)
	{
		UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] This engine's skeletal meshes have no overlay material; the MToon outline '%s' is not applied."), *Outline->GetName());
		return;
	}
	void* Value = Property->ContainerPtrToValuePtr<void>(Mesh);
	if (Property->GetObjectPropertyValue(Value) == Outline)
	{
		return;
	}
	// The mesh was just imported: components pick the overlay up when they register.
	Mesh->Modify();
	Property->SetObjectPropertyValue(Value, Outline);
	Mesh->MarkPackageDirty();
}

void UVRMMaterialPostImportPipeline::ResolveParents()
{
	using namespace VRMMaterialPipelinePrivate;
	TArray<UMaterialInstanceConstant*> Instances;
	for (const TWeakObjectPtr<UMaterialInstanceConstant>& Weak : ImportedInstances)
	{
		if (UMaterialInstanceConstant* Instance = Weak.Get())
		{
			Instances.Add(Instance);
		}
	}

	// Per-material instances are MI_VRM_<Character>_<Material> in the same folder as the character
	// instances MI_VRM_<Character> (M_VRM_Master) and MI_VRM_<Character>__MToon (M_VRM_MToon). Each
	// goes to the character instance with its master, once that one has arrived.
	TArray<UMaterialInstanceConstant*> Parents;
	for (UMaterialInstanceConstant* Candidate : Instances)
	{
		if (!CharacterInstanceName.IsEmpty()
			&& (Candidate->GetName() == CharacterInstanceName || Candidate->GetName() == CharacterInstanceName + MToonCharacterSuffix))
		{
			Parents.Add(Candidate);
		}
	}
	if (Parents.Num() == 0)
	{
		return;
	}

	const FString Prefix = CharacterInstanceName + TEXT("_");
	for (UMaterialInstanceConstant* Child : Instances)
	{
		if (Parents.Contains(Child) || !Child->GetName().StartsWith(Prefix))
		{
			continue;
		}
		const UMaterial* ChildMaster = GetMasterMaterialOf(Child);
		UMaterialInstanceConstant* const* Match = Parents.FindByPredicate([&](const UMaterialInstanceConstant* Candidate)
		{
			return GetMasterMaterialOf(Candidate) == ChildMaster && GetFolderPath(Candidate) == GetFolderPath(Child);
		});
		if (!ChildMaster || !Match || Child->Parent == *Match)
		{
			continue;
		}
		UMaterialInstanceConstant* Parent = *Match;

		// Only within the same master material (the match above), so overrides keep their meaning.
		Child->SetParentEditorOnly(Parent);
		Child->PostEditChange();
		Child->MarkPackageDirty();
		UE_LOG(LogVRMInterchange, Verbose, TEXT("[VRMInterchange] Parented '%s' to '%s'."), *Child->GetName(), *Parent->GetName());
	}
}
