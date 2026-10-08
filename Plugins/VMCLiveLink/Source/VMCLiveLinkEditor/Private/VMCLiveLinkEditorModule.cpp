// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VMCLiveLinkEditorModule.h"
#include "Modules/ModuleManager.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "PropertyEditorModule.h"
#include "VMCLiveLinkRemapper.h"
#include "VMCLiveLinkRemapperCustomization.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"


#define LOCTEXT_NAMESPACE "FVMCLiveLinkEditorModule"

static EAssetTypeCategories::Type GVMCLiveLinkAssetCategory = EAssetTypeCategories::Misc;

namespace VMCLiveLinkEditor
{
	VMCLIVELINKEDITOR_API EAssetTypeCategories::Type GetAssetCategoryBit()
	{
		return GVMCLiveLinkAssetCategory;
	}
}

class FVMCLiveLinkEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
#if WITH_EDITOR
		// The factories' "New" menu category. The mapping asset itself is described by
		// UAssetDefinition_VMCLiveLinkMappingAsset, which the editor finds on its own.
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		GVMCLiveLinkAssetCategory = AssetTools.RegisterAdvancedAssetCategory(TEXT("VMCLiveLink"), LOCTEXT("VMCLiveLinkCategory", "VMC LiveLink"));

		// Buttons for the remapper's editing tools in its details panel
		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyEditor.RegisterCustomClassLayout(UVMCLiveLinkRemapper::StaticClass()->GetFName(),
			FOnGetDetailCustomizationInstance::CreateStatic(&FVMCLiveLinkRemapperCustomization::MakeInstance));

		// Lets the runtime remapper read a mesh's humanoid-map metadata (P4.4, D-4). The package's
		// metadata itself, not UEditorAssetSubsystem, which reads nothing during Play In Editor, when
		// Live Link initializes the remappers of subjects made in PIE. Game thread.
		UVMCLiveLinkRemapper::ReadAssetMetadata = [](UObject* Object)
		{
			const TMap<FName, FString>* Tags = Object && IsInGameThread() ? FMetaData::GetMapForObject(Object) : nullptr;
			return Tags ? *Tags : TMap<FName, FString>();
		};
#endif
	}

	virtual void ShutdownModule() override
	{
#if WITH_EDITOR
		UVMCLiveLinkRemapper::ReadAssetMetadata = nullptr;
		if (FPropertyEditorModule* PropertyEditor = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
		{
			PropertyEditor->UnregisterCustomClassLayout(UVMCLiveLinkRemapper::StaticClass()->GetFName());
		}
#endif
	}
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FVMCLiveLinkEditorModule, VMCLiveLinkEditor)
