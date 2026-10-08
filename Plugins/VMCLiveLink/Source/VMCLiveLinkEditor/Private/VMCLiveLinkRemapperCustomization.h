// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DetailLayoutBuilder.h"
#include "IDetailCustomization.h"

class UVMCLiveLinkRemapper;

/**
 * Buttons for the VMC remapper's editing tools in its details panel (P3.2): apply the preset, seed
 * the maps from the subject, apply, find, save or create a mapping asset. The actions themselves are
 * UVMCLiveLinkRemapper's runtime API; creating an asset needs the editor, so it lives here.
 *
 * This layout applies only where the remapper is the details panel's root object. In the Live Link
 * panel the remapper is an instanced subobject of the subject settings, which the details panel
 * doesn't customize; FVMCLiveLinkSubjectSettingsCustomization adds the same tools there.
 */
class FVMCLiveLinkRemapperCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override;

	/**
	 * Adds the Mapping Tools category (the buttons) and, for a single remapper, the Live Mapping
	 * category (the table) to DetailBuilder. The buttons act on Remappers in one transaction each, then
	 * refresh RefreshBuilder, the panel that shows the remappers' maps. The transaction records only a
	 * transactional remapper: Live Link's subject settings and their remapper aren't, so in the Live Link
	 * panel the buttons can't be undone.
	 */
	static void AddMappingTools(IDetailLayoutBuilder& DetailBuilder, const TArray<TWeakObjectPtr<UVMCLiveLinkRemapper>>& Remappers,
		const TWeakPtr<IDetailLayoutBuilder>& RefreshBuilder, ECategoryPriority::Type Priority);

private:
	/** Runs Action on every remapper in Remappers, in one undoable transaction, then refreshes the panel. */
	static FReply Run(const TArray<TWeakObjectPtr<UVMCLiveLinkRemapper>>& Remappers, const TWeakPtr<IDetailLayoutBuilder>& RefreshBuilder,
		const FText& TransactionName, TFunctionRef<void(UVMCLiveLinkRemapper&)> Action);

	static void CreateMappingAsset(UVMCLiveLinkRemapper& Remapper);

	TWeakPtr<IDetailLayoutBuilder> Builder;
};
