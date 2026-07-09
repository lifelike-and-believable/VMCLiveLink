// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "AssetTypeActions_VMCLiveLinkMappingAsset.h"
#include "VMCLiveLinkMappingAsset.h"
#include "VMCLiveLinkEditorModule.h"

UClass* FAssetTypeActions_VMCLiveLinkMappingAsset::GetSupportedClass() const
{
	return UVMCLiveLinkMappingAsset::StaticClass();
}

uint32 FAssetTypeActions_VMCLiveLinkMappingAsset::GetCategories()
{
	return VMCLiveLinkEditor::GetAssetCategoryBit();
}

