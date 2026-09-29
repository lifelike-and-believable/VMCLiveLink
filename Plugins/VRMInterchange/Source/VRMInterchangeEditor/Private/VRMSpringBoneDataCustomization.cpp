// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMSpringBoneDataCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "VRMSpringBoneData.h"
#include "VRMSpringBonesPostImportPipeline.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "VRMSpringBoneDataCustomization"

TSharedRef<IDetailCustomization> FVRMSpringBoneDataCustomization::MakeInstance()
{
	return MakeShared<FVRMSpringBoneDataCustomization>();
}

TSharedRef<FVRMSpringBoneDataCustomization::FToolState> FVRMSpringBoneDataCustomization::GetToolState(const UObject* Asset)
{
	static TMap<TWeakObjectPtr<const UObject>, TSharedRef<FToolState>> States;
	for (auto It = States.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent(); // assets deleted or unloaded since
		}
	}
	if (const TSharedRef<FToolState>* Found = States.Find(Asset))
	{
		return *Found;
	}
	return States.Add(Asset, MakeShared<FToolState>());
}

void FVRMSpringBoneDataCustomization::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder)
{
	Builder = DetailBuilder;
	CustomizeDetails(*DetailBuilder);
}

void FVRMSpringBoneDataCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	Assets.Reset();
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	for (const TWeakObjectPtr<UObject>& Object : Objects)
	{
		if (UVRMSpringBoneData* Data = Cast<UVRMSpringBoneData>(Object.Get()))
		{
			Assets.Add(Data);
		}
	}

	// Each asset keeps its own tool inputs and last result across refreshes.
	State = GetToolState(Assets.Num() > 0 ? Assets[0].Get() : nullptr);

	IDetailCategoryBuilder& Tools = DetailBuilder.EditCategory(TEXT("Editing Tools"), LOCTEXT("EditingTools", "Editing Tools"), ECategoryPriority::Important);
	const FSlateFontInfo Font = IDetailLayoutBuilder::GetDetailFont();

	auto ScaleBox = [Font](float* Value, const FText& Tooltip)
	{
		return SNew(SSpinBox<float>)
			.Font(Font)
			.MinValue(0.f).MaxValue(10.f).MinSliderValue(0.f).MaxSliderValue(4.f).Delta(0.05f)
			.ToolTipText(Tooltip)
			.Value_Lambda([Value] { return *Value; })
			.OnValueChanged_Lambda([Value](float NewValue) { *Value = NewValue; });
	};

	// Scale all
	Tools.AddCustomRow(LOCTEXT("ScaleFilter", "Scale Stiffness Drag Gravity"))
		.NameContent()
		[
			SNew(STextBlock).Font(Font).Text(LOCTEXT("ScaleLabel", "Scale All (stiffness, drag, gravity)"))
		]
		.ValueContent()
		.MinDesiredWidth(300.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 2, 0)[ ScaleBox(&State->StiffnessScale, LOCTEXT("StiffnessScaleTip", "Multiplies every joint's stiffness")) ]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(2, 0)[ ScaleBox(&State->DragScale, LOCTEXT("DragScaleTip", "Multiplies every joint's drag")) ]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(2, 0)[ ScaleBox(&State->GravityScale, LOCTEXT("GravityScaleTip", "Multiplies every joint's gravity power")) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0, 0, 0)
			[
				SNew(SButton)
					.Text(LOCTEXT("ApplyScale", "Apply"))
					.ToolTipText(LOCTEXT("ApplyScaleTip", "Multiplies every joint's and spring's stiffness, drag and gravity power by these factors (drag stays within 0 to 1; stiffness and gravity at least 0). Only the quantities whose factor isn't 1 change."))
					.OnClicked_Lambda([this]()
					{
						const float S = State->StiffnessScale, D = State->DragScale, G = State->GravityScale;
						State->LastResult = FText::Format(LOCTEXT("Scaled", "Scaled stiffness x{0}, drag x{1}, gravity x{2}."), FText::AsNumber(S), FText::AsNumber(D), FText::AsNumber(G));
						return Run(LOCTEXT("ScaleTx", "Scale Spring Parameters"), [S, D, G](UVRMSpringBoneData& Data)
						{
							Data.Modify();
							Data.ScaleParameters(S, D, G);
							return true;
						});
					})
			]
		];

	// Reset one spring
	const UVRMSpringBoneData* First = Assets.Num() > 0 ? Assets[0].Get() : nullptr;
	const int32 NumSprings = First ? First->SpringConfig.Springs.Num() : 0;
	State->SpringToReset = FMath::Clamp(State->SpringToReset, 0, FMath::Max(0, NumSprings - 1));
	Tools.AddCustomRow(LOCTEXT("ResetFilter", "Reset Spring File Source Values"))
		.NameContent()
		[
			SNew(STextBlock).Font(Font).Text(LOCTEXT("ResetLabel", "Reset Spring to File Values"))
		]
		.ValueContent()
		.MinDesiredWidth(300.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[
				SNew(SSpinBox<int32>)
					.Font(Font)
					.MinValue(0).MaxValue(FMath::Max(0, NumSprings - 1))
					.ToolTipText(LOCTEXT("SpringIndexTip", "The spring to reset (its index in Springs)"))
					.Value_Lambda([this] { return State->SpringToReset; })
					.OnValueChanged_Lambda([this](int32 NewValue) { State->SpringToReset = NewValue; })
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[
				SNew(STextBlock)
					.Font(Font)
					.Text_Lambda([this]()
					{
						const UVRMSpringBoneData* Data = Assets.Num() > 0 ? Assets[0].Get() : nullptr;
						return Data && Data->SpringConfig.Springs.IsValidIndex(State->SpringToReset)
							? FText::FromString(Data->SpringConfig.Springs[State->SpringToReset].Name)
							: FText::GetEmpty();
					})
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
					.Text(LOCTEXT("Reset", "Reset"))
					.ToolTipText(LOCTEXT("ResetTip", "Puts this spring's and its joints' stiffness, drag, gravity and hit radius back to the values the file gave them at import."))
					.IsEnabled_Lambda([this]()
					{
						const UVRMSpringBoneData* Data = Assets.Num() > 0 ? Assets[0].Get() : nullptr;
						return Data && Data->HasSourceValues();
					})
					.OnClicked_Lambda([this]()
					{
						const int32 Index = State->SpringToReset;
						TArray<FString> Skipped;
						const FReply Reply = Run(LOCTEXT("ResetTx", "Reset Spring to File Values"), [Index, &Skipped](UVRMSpringBoneData& Data)
						{
							if (!Data.HasSourceValues() || !Data.SpringConfig.Springs.IsValidIndex(Index))
							{
								Skipped.Add(Data.GetName());
								return false;
							}
							Data.Modify();
							return Data.ResetSpringToSource(Index);
						});
						State->LastResult = Skipped.Num() == 0
							? FText::Format(LOCTEXT("ResetDone", "Spring {0} reset to the file's values."), FText::AsNumber(Index))
							: FText::Format(LOCTEXT("ResetSkipped", "Spring {0} not reset in {1}: no such spring, or no file values recorded (reimport first)."),
								FText::AsNumber(Index), FText::FromString(FString::Join(Skipped, TEXT(", "))));
						return Reply;
					})
			]
		];
	if (First && !First->HasSourceValues())
	{
		Tools.AddCustomRow(LOCTEXT("NoSourceFilter", "Reset Spring File Values"))
			.WholeRowContent()
			[
				SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFontItalic())
					.AutoWrapText(true)
					.Text(LOCTEXT("NoSourceValues", "This asset doesn't have the file's values recorded (it was imported before they were kept, or its springs changed since). Reimport from Source to record them."))
			];
	}

	// Reimport from source
	Tools.AddCustomRow(LOCTEXT("ReimportFilter", "Reimport Source File"))
		.NameContent()
		[
			SNew(STextBlock).Font(Font).Text(LOCTEXT("ReimportLabel", "Reimport from Source"))
		]
		.ValueContent()
		.MinDesiredWidth(300.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[
				SNew(STextBlock)
					.Font(Font)
					.Text(First ? FText::FromString(FPaths::GetCleanFilename(First->SourceFilename)) : FText::GetEmpty())
					.ToolTipText(First ? FText::FromString(First->SourceFilename) : FText::GetEmpty())
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
					.Text(LOCTEXT("Reimport", "Reimport"))
					.ToolTipText(LOCTEXT("ReimportTip", "Reads the source VRM file again and replaces the springs, joints and colliders with its values. Edits made to them here are replaced."))
					.IsEnabled_Lambda([this]()
					{
						const UVRMSpringBoneData* Data = Assets.Num() > 0 ? Assets[0].Get() : nullptr;
						return Data && !Data->SourceFilename.IsEmpty() && FPaths::FileExists(Data->SourceFilename);
					})
					.OnClicked_Lambda([this]()
					{
						TArray<FString> Errors;
						const FReply Reply = Run(LOCTEXT("ReimportTx", "Reimport Spring Data"), [&Errors](UVRMSpringBoneData& Data)
						{
							FString Error;
							if (!UVRMSpringBonesPostImportPipeline::ReimportFromSource(&Data, Error)) // calls Modify only when it replaces the data
							{
								Errors.Add(FString::Printf(TEXT("%s: %s"), *Data.GetName(), *Error));
								return false;
							}
							return true;
						});
						State->LastResult = Errors.Num() > 0
							? FText::FromString(FString::Join(Errors, TEXT("\n")))
							: LOCTEXT("Reimported", "Reimported from the source file.");
						return Reply;
					})
			]
		];

	Tools.AddCustomRow(LOCTEXT("ResultFilter", "Result"))
		.WholeRowContent()
		[
			SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFontItalic())
				.AutoWrapText(true)
				.Text_Lambda([this] { return State->LastResult; })
				.Visibility_Lambda([this] { return State->LastResult.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
		];
}

FReply FVRMSpringBoneDataCustomization::Run(const FText& TransactionName, TFunctionRef<bool(UVRMSpringBoneData&)> Action)
{
	int32 Changed = 0;
	{
		FScopedTransaction Transaction(TransactionName);
		for (const TWeakObjectPtr<UVRMSpringBoneData>& Weak : Assets)
		{
			if (UVRMSpringBoneData* Data = Weak.Get(); Data && Action(*Data))
			{
				Data->MarkPackageDirty();
				++Changed;
			}
		}
		if (Changed == 0)
		{
			Transaction.Cancel(); // no empty undo entry
		}
	}
	// Reimport can change the number of springs, so rebuild the rows.
	if (const TSharedPtr<IDetailLayoutBuilder> Pinned = Changed > 0 ? Builder.Pin() : nullptr)
	{
		Pinned->ForceRefreshDetails();
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
