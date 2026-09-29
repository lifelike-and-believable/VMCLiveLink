// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class UVRMSpringBoneData;

/**
 * Editing tools in the spring data asset's details (P6.4): scale every spring's stiffness, drag and
 * gravity at once, put a spring back to the file's values, and reimport the springs from the source
 * file. Each is one undoable transaction; running spring nodes (previews, PIE) pick the change up
 * without recompiling.
 */
class FVRMSpringBoneDataCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override;

private:
	/**
	 * The tools' inputs and last result for one asset (the first selected). Kept outside the
	 * customization, which the details view recreates on every refresh (the tools refresh it after
	 * each change), and per asset, so two open editors don't share them.
	 */
	struct FToolState
	{
		float StiffnessScale = 1.f;
		float DragScale = 1.f;
		float GravityScale = 1.f;
		int32 SpringToReset = 0;
		FText LastResult;
	};
	static TSharedRef<FToolState> GetToolState(const UObject* Asset);

	/**
	 * Runs Action on every spring data asset being edited, in one undoable transaction. Action calls
	 * Modify on the asset when it is about to change it, and returns whether it did. Assets left
	 * unchanged aren't dirtied, and a transaction that changed nothing is cancelled.
	 */
	FReply Run(const FText& TransactionName, TFunctionRef<bool(UVRMSpringBoneData&)> Action);

	TArray<TWeakObjectPtr<UVRMSpringBoneData>> Assets;
	TWeakPtr<IDetailLayoutBuilder> Builder;
	TSharedRef<FToolState> State = MakeShared<FToolState>(); // replaced by the asset's in CustomizeDetails
};
