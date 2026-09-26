// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "AssetDefinitionDefault.h"
#include "AssetDefinition_VMCLiveLinkMappingAsset.generated.h"

/** How mapping assets appear in the content browser (replaces FAssetTypeActions, P3.2). */
UCLASS()
class UAssetDefinition_VMCLiveLinkMappingAsset : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override;
	virtual FLinearColor GetAssetColor() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
};
