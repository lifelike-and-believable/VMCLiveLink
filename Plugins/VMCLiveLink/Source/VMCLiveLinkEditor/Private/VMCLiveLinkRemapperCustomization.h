// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class UVMCLiveLinkRemapper;

/**
 * Buttons for the VMC remapper's editing tools in its details panel (P3.2): apply the preset, seed
 * the maps from the subject, apply, find, save or create a mapping asset. The actions themselves are
 * UVMCLiveLinkRemapper's runtime API; creating an asset needs the editor, so it lives here.
 */
class FVMCLiveLinkRemapperCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override;

private:
	/** Runs Action on every remapper being edited, in one undoable transaction, then refreshes the panel. */
	FReply Run(const FText& TransactionName, TFunctionRef<void(UVMCLiveLinkRemapper&)> Action);

	static void CreateMappingAsset(UVMCLiveLinkRemapper& Remapper);

	TArray<TWeakObjectPtr<UVMCLiveLinkRemapper>> Remappers;
	TWeakPtr<IDetailLayoutBuilder> Builder;
};
