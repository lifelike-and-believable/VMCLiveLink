// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "VMCLiveLinkRemapper.h"

class USkeletalMesh;

/**
 * The remapper's live mapping table (P6.2): every bone and curve the subject receives, what it is
 * renamed to, and why it may not reach the mesh (no map entry, a target another name also uses, a
 * name the reference mesh doesn't have). Refreshes itself while shown.
 */
class SVMCMappingTable : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVMCMappingTable) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TWeakObjectPtr<UVMCLiveLinkRemapper> InRemapper);

private:
	struct FItem
	{
		FVMCMappingRow Row;
		bool bCurve = false;
	};
	using FItemPtr = TSharedPtr<FItem>;

	EActiveTimerReturnType OnRefreshTimer(double InCurrentTime, float InDeltaTime);
	/** Rebuilds the rows if the incoming names, the remapper or the filter changed. */
	void Refresh(bool bForce);
	TSharedRef<ITableRow> OnGenerateRow(FItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable);

	TWeakObjectPtr<UVMCLiveLinkRemapper> Remapper;
	TSharedPtr<SListView<FItemPtr>> List;
	TArray<FItemPtr> Items;
	FText Summary;
	bool bOnlyProblems = false;

	// What the rows were built from
	TArray<FName> LastBones;
	TArray<FName> LastCurves;
	uint32 LastRevision = MAX_uint32;
	TWeakObjectPtr<USkeletalMesh> Reference;
};
