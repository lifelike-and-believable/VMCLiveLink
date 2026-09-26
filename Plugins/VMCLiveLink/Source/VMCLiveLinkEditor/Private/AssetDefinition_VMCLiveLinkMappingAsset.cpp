// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "AssetDefinition_VMCLiveLinkMappingAsset.h"
#include "VMCLiveLinkMappingAsset.h"

#define LOCTEXT_NAMESPACE "AssetDefinition_VMCLiveLinkMappingAsset"

FText UAssetDefinition_VMCLiveLinkMappingAsset::GetAssetDisplayName() const
{
	return LOCTEXT("DisplayName", "VMC LiveLink Mapping Asset");
}

FLinearColor UAssetDefinition_VMCLiveLinkMappingAsset::GetAssetColor() const
{
	return FLinearColor(FColor(0x00, 0xA3, 0xE8));
}

TSoftClassPtr<UObject> UAssetDefinition_VMCLiveLinkMappingAsset::GetAssetClass() const
{
	return UVMCLiveLinkMappingAsset::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_VMCLiveLinkMappingAsset::GetAssetCategories() const
{
	static const FAssetCategoryPath Categories[] = { EAssetCategoryPaths::Animation };
	return Categories;
}

#undef LOCTEXT_NAMESPACE
