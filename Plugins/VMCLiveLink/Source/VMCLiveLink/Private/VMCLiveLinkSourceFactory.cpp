// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.

#include "VMCLiveLinkSourceFactory.h"
#include "VMCLiveLinkSource.h"
#include "VMCConnectionSettings.h"
#include "VMCLog.h"

#if WITH_EDITOR
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Framework/Application/SlateApplication.h"
#endif

TSharedPtr<ILiveLinkSource> UVMCLiveLinkSourceFactory::CreateSource(const FString& ConnectionString) const
{
	FVMCConnectionSettings Settings;
	TArray<FString> Errors;
	if (!FVMCConnectionSettings::FromString(ConnectionString, Settings, &Errors))
	{
		// Invalid values keep their defaults; say which, since the source may not be where the user expects.
		UE_LOG(LogVMCLiveLink, Warning, TEXT("VMC connection string '%s': %s. Using defaults for those."),
			*ConnectionString, *FString::Join(Errors, TEXT("; ")));
	}
	return MakeShared<FVMCLiveLinkSource>(Settings);
}

#if WITH_EDITOR
TSharedPtr<SWidget> UVMCLiveLinkSourceFactory::BuildCreationPanel(FOnLiveLinkSourceCreated OnCreated) const
{
	struct FState { int32 Port = FVMCConnectionSettings::DefaultPort; bool bUnityToUE = true; bool bMetersToCm = true; FString SubjectName = FString(TEXT("VMC_Subject")); };

	TSharedRef<FState> State = MakeShared<FState>();

	// Helper used by both Enter key and Create button
	auto CreateAndNotify = [OnCreated, State]()
	{
		// Validate again before creating (defensive)
		if (State->SubjectName.TrimStartAndEnd().IsEmpty())
		{
			return;
		}

		FVMCConnectionSettings Settings;
		Settings.Port = State->Port;
		Settings.bUnityToUE = State->bUnityToUE;
		Settings.bMetersToCm = State->bMetersToCm;
		Settings.SubjectName = FName(*State->SubjectName);
		if (!Settings.Validate())
		{
			return;
		}

		const TSharedPtr<ILiveLinkSource> Src = MakeShared<FVMCLiveLinkSource>(Settings);
		if (OnCreated.IsBound())
		{
			OnCreated.Execute(Src, Settings.ToString());
		}
	};

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(STextBlock).Text(NSLOCTEXT("VMCLiveLink", "Desc", "Listen for VMC over OSC"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[SNew(STextBlock).Text(NSLOCTEXT("VMCLiveLink", "Port", "Port"))]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SSpinBox<int32>)
						.MinValue(1).MaxValue(65535)
						.Value_Lambda([State] { return State->Port; })
						.OnValueChanged_Lambda([State](int32 V) { State->Port = V; })
				]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[SNew(STextBlock).Text(NSLOCTEXT("VMCLiveLink", "Subject", "Subject Name"))]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SEditableTextBox)
						.HintText(NSLOCTEXT("VMCLiveLink", "InputHint", "Enter subject name..."))
						.ClearKeyboardFocusOnCommit(false)
						.Text_Lambda([State] { return FText::FromString(State->SubjectName); })
						.OnTextChanged_Lambda([State](const FText& Text) { State->SubjectName = Text.ToString(); })
						.OnTextCommitted_Lambda([State, CreateAndNotify](const FText& Text, ETextCommit::Type CommitType)
							{
								State->SubjectName = Text.ToString();
								if (CommitType == ETextCommit::OnEnter)
								{
									if (!State->SubjectName.TrimStartAndEnd().IsEmpty())
									{
										CreateAndNotify();
									}
								}
							})
				]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 4, 4)
		[
			SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(STextBlock)
						.Visibility_Lambda([State]() { return State->SubjectName.TrimStartAndEnd().IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
						.ColorAndOpacity(FLinearColor::Red)
						.Text(NSLOCTEXT("VMCLiveLink", "EmptySubjectError", "Subject name cannot be empty."))
				]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 16, 0)
				[
					SNew(SCheckBox)
						.IsChecked_Lambda([State] { return State->bUnityToUE ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([State](ECheckBoxState S) { State->bUnityToUE = (S == ECheckBoxState::Checked); })
						[
							SNew(STextBlock).Text(NSLOCTEXT("VMCLiveLink", "UnityToUE", "Convert Unity to UE coordinates"))
						]
				]
			+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SCheckBox)
						.IsChecked_Lambda([State] { return State->bMetersToCm ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([State](ECheckBoxState S) { State->bMetersToCm = (S == ECheckBoxState::Checked); })
						[
							SNew(STextBlock).Text(NSLOCTEXT("VMCLiveLink", "MetersToCm", "Convert meters to centimeters"))
						]
				]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(4)
		[
			SNew(SUniformGridPanel).SlotPadding(FMargin(4))
				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
						.Text(NSLOCTEXT("VMCLiveLink", "Create", "Create"))
						.IsEnabled_Lambda([State]() { return !State->SubjectName.TrimStartAndEnd().IsEmpty(); })
						.OnClicked_Lambda([CreateAndNotify]()
							{
								CreateAndNotify();
								return FReply::Handled();
							})
				]
		];
}
#endif // WITH_EDITOR