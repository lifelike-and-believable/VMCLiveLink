// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMImportPipelineRegistration.h"

#include "InterchangeProjectSettings.h"
#include "VRMTranslator.h"
#include "VRMSpringBonesPostImportPipeline.h"
#include "VRMIKRigPostImportPipeline.h"
#include "VRMLiveLinkPostImportPipeline.h"
#include "VRMMaterialPostImportPipeline.h"
#include "VRMAvatarDescriptionPipeline.h"
#include "VRMInterchangeLog.h"

namespace
{
	/** Resolves a plugin pipeline asset if it exists, otherwise the pipeline class path. */
	FSoftObjectPath ResolvePipelinePath(const TCHAR* AssetPath, UClass* AssetClass, const TCHAR* ClassPath)
	{
		if (AssetPath)
		{
			if (UObject* Existing = StaticLoadObject(AssetClass, nullptr, AssetPath))
			{
				return FSoftObjectPath(Existing);
			}
		}
		return FSoftObjectPath(ClassPath);
	}

	FInterchangeTranslatorPipelines* FindOrAddPerTranslator(FInterchangeImportSettings& ImportSettings, bool& bOutDirty)
	{
		FInterchangePipelineStack* AssetsStack = ImportSettings.PipelineStacks.Find(TEXT("Assets"));
		if (!AssetsStack)
		{
			return nullptr;
		}

		const FString TranslatorPath = UVRMTranslator::StaticClass()->GetPathName();
		for (FInterchangeTranslatorPipelines& It : AssetsStack->PerTranslatorPipelines)
		{
			if (It.Translator.ToSoftObjectPath().ToString() == TranslatorPath)
			{
				return &It;
			}
		}

		// Start from the project's own Assets stack so other pipelines the project uses still run.
		FInterchangeTranslatorPipelines NewEntry;
		NewEntry.Translator = UVRMTranslator::StaticClass();
		NewEntry.Pipelines = AssetsStack->Pipelines;
		AssetsStack->PerTranslatorPipelines.Add(MoveTemp(NewEntry));
		bOutDirty = true;
		return &AssetsStack->PerTranslatorPipelines.Last();
	}

	void AppendIfMissing(FInterchangeTranslatorPipelines& Per, const FSoftObjectPath& Path, bool& bOutDirty)
	{
		for (const FSoftObjectPath& Existing : Per.Pipelines)
		{
			if (Existing.ToString() == Path.ToString())
			{
				return;
			}
		}
		Per.Pipelines.Add(Path);
		bOutDirty = true;
	}

	/**
	 * Makes the plugin's VRM assets pipeline the first entry for the VRM translator and removes the
	 * generic assets pipeline from the VRM translator's list only (the project's global stack is
	 * not touched), since both would create the same assets.
	 */
	void EnsureVRMAssetsPipelineIsFirst(FInterchangeTranslatorPipelines& Per, bool& bOutDirty)
	{
		const FSoftObjectPath DesiredPath = ResolvePipelinePath(
			TEXT("/VRMInterchange/DefaultPipelines/DefaultVRMAssetsPipeline.DefaultVRMAssetsPipeline"),
			UObject::StaticClass(),
			TEXT("/VRMInterchange/DefaultPipelines/DefaultVRMAssetsPipeline.DefaultVRMAssetsPipeline"));
		const FString Desired = DesiredPath.ToString();

		for (int32 i = Per.Pipelines.Num() - 1; i >= 0; --i)
		{
			const FString S = Per.Pipelines[i].ToString();
			const bool bIsGenericAssetsPipeline =
				(S.Contains(TEXT("DefaultAssetsPipeline")) && S != Desired)
				|| S.Equals(TEXT("/Script/InterchangePipelines.InterchangeGenericAssetsPipeline"))
				|| S.Equals(TEXT("/Script/InterchangeEditor.InterchangeGenericAssetsPipeline"));
			if (bIsGenericAssetsPipeline || (S == Desired && i > 0))
			{
				Per.Pipelines.RemoveAt(i);
				bOutDirty = true;
			}
		}

		if (Per.Pipelines.Num() == 0 || Per.Pipelines[0].ToString() != Desired)
		{
			Per.Pipelines.Insert(DesiredPath, 0);
			bOutDirty = true;
		}
	}

	/** Shows the import dialog (and reimport dialog) for VRM textures. */
	template <typename FContentImportSettings>
	void EnsureTextureDialogOverride(FContentImportSettings& ContentSettings, bool& bOutDirty)
	{
		auto& TexturesOverride = ContentSettings.ShowImportDialogOverride.FindOrAdd(EInterchangeTranslatorAssetType::Textures);
		auto& PerTranslator = TexturesOverride.PerTranslatorImportDialogOverride;
		const FString VRMTranslatorPath = UVRMTranslator::StaticClass()->GetPathName();

		int32 FoundIndex = INDEX_NONE;
		for (int32 i = 0; i < PerTranslator.Num(); ++i)
		{
			if (PerTranslator[i].Translator.ToSoftObjectPath().ToString() == VRMTranslatorPath)
			{
				FoundIndex = i;
				break;
			}
		}
		if (FoundIndex == INDEX_NONE)
		{
			FoundIndex = PerTranslator.AddDefaulted();
			PerTranslator[FoundIndex].Translator = UVRMTranslator::StaticClass();
			bOutDirty = true;
		}
		if (!PerTranslator[FoundIndex].bShowImportDialog)
		{
			PerTranslator[FoundIndex].bShowImportDialog = true;
			bOutDirty = true;
		}
		if (!PerTranslator[FoundIndex].bShowReimportDialog)
		{
			PerTranslator[FoundIndex].bShowReimportDialog = true;
			bOutDirty = true;
		}
	}

	/** Applies every registration step to InOut. Returns true if anything changed. */
	template <typename FContentImportSettings>
	bool UpdateRegistration(FContentImportSettings& InOut)
	{
		bool bDirty = false;
		if (FInterchangeTranslatorPipelines* Per = FindOrAddPerTranslator(InOut, bDirty))
		{
			AppendIfMissing(*Per, ResolvePipelinePath(
				TEXT("/VRMInterchange/DefaultPipelines/DefaultSpringBonesPipeline.DefaultSpringBonesPipeline"),
				UVRMSpringBonesPostImportPipeline::StaticClass(),
				TEXT("/Script/VRMInterchangeEditor.VRMSpringBonesPostImportPipeline")), bDirty);
			AppendIfMissing(*Per, ResolvePipelinePath(
				TEXT("/VRMInterchange/DefaultPipelines/DefaultVRMIKRigPipeline.DefaultVRMIKRigPipeline"),
				UVRMIKRigPostImportPipeline::StaticClass(),
				TEXT("/Script/VRMInterchangeEditor.VRMIKRigPostImportPipeline")), bDirty);
			AppendIfMissing(*Per, ResolvePipelinePath(
				TEXT("/VRMInterchange/DefaultPipelines/DefaultVRMLiveLinkPipeline.DefaultVRMLiveLinkPipeline"),
				UVRMLiveLinkPostImportPipeline::StaticClass(),
				TEXT("/Script/VRMInterchangeEditor.VRMLiveLinkPostImportPipeline")), bDirty);
			AppendIfMissing(*Per, ResolvePipelinePath(
				nullptr,
				UVRMMaterialPostImportPipeline::StaticClass(),
				TEXT("/Script/VRMInterchangeEditor.VRMMaterialPostImportPipeline")), bDirty);
			// After the spring pipeline, so the description can point at its spring data.
			AppendIfMissing(*Per, ResolvePipelinePath(
				nullptr,
				UVRMAvatarDescriptionPipeline::StaticClass(),
				TEXT("/Script/VRMInterchangeEditor.VRMAvatarDescriptionPipeline")), bDirty);
			EnsureVRMAssetsPipelineIsFirst(*Per, bDirty);
		}
		EnsureTextureDialogOverride(InOut, bDirty);
		return bDirty;
	}
}

namespace VRMImportPipelineRegistration
{
	bool IsUpToDate()
	{
		const UInterchangeProjectSettings* Settings = GetDefault<UInterchangeProjectSettings>();
		if (!Settings)
		{
			return true;
		}
		// Dry run on a copy so checking never modifies the live settings.
		auto Copy = Settings->ContentImportSettings;
		return !UpdateRegistration(Copy);
	}

	bool Apply()
	{
		UInterchangeProjectSettings* Settings = GetMutableDefault<UInterchangeProjectSettings>();
		if (!Settings)
		{
			return false;
		}
		auto Copy = Settings->ContentImportSettings;
		if (!UpdateRegistration(Copy))
		{
			return false;
		}
		Settings->ContentImportSettings = Copy;
		Settings->SaveConfig();
		UE_LOG(LogVRMInterchange, Log, TEXT("[VRMInterchange] Registered the VRM import pipelines in the Interchange project settings."));
		return true;
	}
}
