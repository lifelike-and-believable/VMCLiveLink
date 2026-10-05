// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMImportPipelineRegistration.h"

#include "InterchangeProjectSettings.h"
#include "VRMTranslator.h"
#include "VRMInterchangeLog.h"

namespace
{
	/**
	 * The plugin's pipeline assets, in the order they run after the assets pipeline. Interchange
	 * only instantiates pipeline assets (UE::Interchange::GeneratePipelineInstance); a class path
	 * such as /Script/VRMInterchangeEditor.VRMMaterialPostImportPipeline loads a UClass, which it
	 * rejects as "type is unknown", so every pipeline needs an asset here.
	 */
	const TCHAR* const VRMPostImportPipelineAssets[] =
	{
		TEXT("/VRMInterchange/DefaultPipelines/DefaultSpringBonesPipeline.DefaultSpringBonesPipeline"),
		TEXT("/VRMInterchange/DefaultPipelines/DefaultVRMIKRigPipeline.DefaultVRMIKRigPipeline"),
		TEXT("/VRMInterchange/DefaultPipelines/DefaultVRMLiveLinkPipeline.DefaultVRMLiveLinkPipeline"),
		TEXT("/VRMInterchange/DefaultPipelines/DefaultVRMMaterialPipeline.DefaultVRMMaterialPipeline"),
		// After the spring pipeline, so the description can point at its spring data.
		TEXT("/VRMInterchange/DefaultPipelines/DefaultVRMAvatarDescriptionPipeline.DefaultVRMAvatarDescriptionPipeline"),
	};

	const TCHAR* const VRMAssetsPipelineAsset = TEXT("/VRMInterchange/DefaultPipelines/DefaultVRMAssetsPipeline.DefaultVRMAssetsPipeline");

	/** Class paths that earlier versions registered for pipelines without an asset; they never instantiate. */
	const TCHAR* const StaleClassPathPrefix = TEXT("/Script/VRMInterchangeEditor.");

	// Templated like UpdateRegistration, which calls it, so both take whatever type the project
	// settings' ContentImportSettings has.
	template <typename FContentImportSettings>
	FInterchangeTranslatorPipelines* FindOrAddPerTranslator(FContentImportSettings& ImportSettings, bool& bOutDirty)
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

	/** Removes the VRM class-path entries that earlier versions saved; Interchange can't instantiate them. */
	void RemoveStaleClassPaths(FInterchangeTranslatorPipelines& Per, bool& bOutDirty)
	{
		const int32 Removed = Per.Pipelines.RemoveAll([](const FSoftObjectPath& Path)
		{
			return Path.ToString().StartsWith(StaleClassPathPrefix);
		});
		if (Removed > 0)
		{
			bOutDirty = true;
		}
	}

	/**
	 * Makes the plugin's VRM assets pipeline the first entry for the VRM translator and removes the
	 * generic assets pipeline from the VRM translator's list only (the project's global stack is
	 * not touched), since both would create the same assets.
	 */
	void EnsureVRMAssetsPipelineIsFirst(FInterchangeTranslatorPipelines& Per, bool& bOutDirty)
	{
		const FSoftObjectPath DesiredPath(VRMAssetsPipelineAsset);
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
			RemoveStaleClassPaths(*Per, bDirty);
			for (const TCHAR* AssetPath : VRMPostImportPipelineAssets)
			{
				AppendIfMissing(*Per, FSoftObjectPath(AssetPath), bDirty);
			}
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

	TArray<FSoftObjectPath> PreviewVRMTranslatorPipelines(const TArray<FSoftObjectPath>* SeedPipelines)
	{
		const UInterchangeProjectSettings* Settings = GetDefault<UInterchangeProjectSettings>();
		if (!Settings)
		{
			return {};
		}
		auto Copy = Settings->ContentImportSettings;
		bool bUnused = false;
		if (SeedPipelines)
		{
			if (FInterchangeTranslatorPipelines* Per = FindOrAddPerTranslator(Copy, bUnused))
			{
				Per->Pipelines = *SeedPipelines;
			}
		}
		UpdateRegistration(Copy);
		const FInterchangeTranslatorPipelines* Per = FindOrAddPerTranslator(Copy, bUnused);
		return Per ? Per->Pipelines : TArray<FSoftObjectPath>();
	}
}
