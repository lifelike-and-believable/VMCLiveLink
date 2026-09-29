// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "SVMCMappingTable.h"

#include "Engine/SkeletalMesh.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SVMCMappingTable"

namespace VMCMappingTable
{
	const FLinearColor ProblemColor(1.f, 0.3f, 0.25f);
	const FLinearColor WarningColor(1.f, 0.7f, 0.1f);
	const FLinearColor QuietColor(0.6f, 0.6f, 0.6f);

	bool IsProblem(const FVMCMappingRow& Row)
	{
		return Row.bDuplicateTarget || Row.bNotOnTarget;
	}
}

void SVMCMappingTable::Construct(const FArguments& InArgs, TWeakObjectPtr<UVMCLiveLinkRemapper> InRemapper)
{
	Remapper = InRemapper;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(STextBlock)
				.AutoWrapText(true)
				.Text_Lambda([this] { return Summary; })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SCheckBox)
				.IsChecked_Lambda([this] { return bOnlyProblems ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
				{
					bOnlyProblems = State == ECheckBoxState::Checked;
					Refresh(/*bForce*/ true);
				})
				[
					SNew(STextBlock).Text(LOCTEXT("OnlyProblems", "Only names that don't reach the mesh"))
				]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SBox)
				.MaxDesiredHeight(360.f)
				[
					SAssignNew(List, SListView<FItemPtr>)
						.ListItemsSource(&Items)
						.SelectionMode(ESelectionMode::None)
						.OnGenerateRow(this, &SVMCMappingTable::OnGenerateRow)
				]
		]
	];

	Refresh(/*bForce*/ true);
	RegisterActiveTimer(1.f, FWidgetActiveTimerDelegate::CreateSP(this, &SVMCMappingTable::OnRefreshTimer));
}

EActiveTimerReturnType SVMCMappingTable::OnRefreshTimer(double InCurrentTime, float InDeltaTime)
{
	Refresh(/*bForce*/ false);
	return EActiveTimerReturnType::Continue;
}

void SVMCMappingTable::Refresh(bool bForce)
{
	const UVMCLiveLinkRemapper* R = Remapper.Get();
	if (!R)
	{
		Items.Reset();
		Summary = FText::GetEmpty();
		List->RequestListRefresh();
		return;
	}

	TArray<FName> Bones, Curves;
	const bool bHaveNames = R->GetIncomingNames(Bones, Curves);
	const bool bRemapperChanged = R->GetRevision() != LastRevision;
	if (!bForce && !bRemapperChanged && Bones == LastBones && Curves == LastCurves)
	{
		return;
	}
	if (bRemapperChanged || bForce)
	{
		Reference = R->ResolveReferenceSkeleton(); // may load it; only when the settings change
	}
	LastRevision = R->GetRevision();
	LastBones = Bones;
	LastCurves = Curves;

	Items.Reset();
	if (!bHaveNames)
	{
		Summary = LOCTEXT("NoData", "The subject hasn't received anything yet. Start the sender, and check the source's status in the Live Link panel.");
		List->RequestListRefresh();
		return;
	}

	TArray<FVMCMappingRow> BoneRows, CurveRows;
	R->BuildMappingTable(Bones, Curves, Reference.Get(), BoneRows, CurveRows);

	int32 Duplicates = 0, NotOnTarget = 0, Unmapped = 0;
	auto Add = [&](const TArray<FVMCMappingRow>& Rows, bool bCurve)
	{
		for (const FVMCMappingRow& Row : Rows)
		{
			Duplicates += Row.bDuplicateTarget ? 1 : 0;
			NotOnTarget += Row.bNotOnTarget ? 1 : 0;
			Unmapped += (!Row.bMapped && !Row.bSynthesized) ? 1 : 0;
			if (!bOnlyProblems || VMCMappingTable::IsProblem(Row))
			{
				Items.Add(MakeShared<FItem>(FItem{ Row, bCurve }));
			}
		}
	};
	Add(BoneRows, false);
	Add(CurveRows, true);

	const FText Target = Reference.IsValid()
		? FText::Format(LOCTEXT("CheckedAgainst", "checked against {0}"), FText::FromString(Reference->GetName()))
		: LOCTEXT("NotChecked", "no reference skeleton, so targets aren't checked");
	Summary = FText::Format(LOCTEXT("Summary", "{0} bones and {1} curves received ({2}). {3} unmapped (passed through as sent), {4} sharing a target with another name, {5} not on the reference mesh."),
		FText::AsNumber(Bones.Num()), FText::AsNumber(Curves.Num()), Target,
		FText::AsNumber(Unmapped), FText::AsNumber(Duplicates), FText::AsNumber(NotOnTarget));
	List->RequestListRefresh();
}

TSharedRef<ITableRow> SVMCMappingTable::OnGenerateRow(FItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	using namespace VMCMappingTable;
	const FVMCMappingRow& Row = Item->Row;

	FText Note;
	FLinearColor NoteColor = QuietColor;
	if (Row.bDuplicateTarget)
	{
		Note = Item->bCurve
			? LOCTEXT("DuplicateCurve", "Another curve has this name too: only one value reaches it")
			: LOCTEXT("DuplicateBone", "Another bone has this name too: only one drives it");
		NoteColor = ProblemColor;
	}
	else if (Row.bNotOnTarget)
	{
		Note = Item->bCurve
			? LOCTEXT("NoMorph", "No morph target of this name on the reference mesh (an AnimBlueprint may still use it)")
			: LOCTEXT("NoBone", "No bone of this name on the reference mesh");
		NoteColor = WarningColor;
	}
	else if (Row.bSynthesized)
	{
		Note = LOCTEXT("Synthesized", "Added by the normalizer");
	}
	else if (!Row.bMapped)
	{
		Note = LOCTEXT("Unmapped", "Unmapped: passes through as sent");
	}

	const FText Kind = Item->bCurve ? LOCTEXT("Curve", "Curve") : LOCTEXT("Bone", "Bone");
	const FText Incoming = Row.bSynthesized ? LOCTEXT("FromNormalizer", "(normalizer)") : FText::FromName(Row.Incoming);

	return SNew(STableRow<FItemPtr>, OwnerTable)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2, 1)
			[
				SNew(SBox).WidthOverride(42.f)
				[
					SNew(STextBlock).Text(Kind).ColorAndOpacity(QuietColor)
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(2, 1)
			[
				SNew(STextBlock).Text(Incoming)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 1)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("->")))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(2, 1)
			[
				SNew(STextBlock).Text(FText::FromName(Row.Outgoing)).ColorAndOpacity(IsProblem(Row) ? NoteColor : FLinearColor::White)
			]
			+ SHorizontalBox::Slot().FillWidth(1.4f).Padding(2, 1)
			[
				SNew(STextBlock).Text(Note).ColorAndOpacity(NoteColor).AutoWrapText(true)
			]
		];
}

#undef LOCTEXT_NAMESPACE
