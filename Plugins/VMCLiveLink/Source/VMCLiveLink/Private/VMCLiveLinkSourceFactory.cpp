// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.

#include "VMCLiveLinkSourceFactory.h"
#include "VMCLiveLinkSource.h"
#include "VMCConnectionSettings.h"
#include "VMCLog.h"

#if WITH_EDITOR
#include "VMCLiveLinkSourceSettings.h"
#include "VMCUdpReceiver.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
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
	// Every setting the source has, shown by a details view of a transient settings object (P6.1):
	// the same fields, tooltips and ranges as the Live Link panel shows after creation.
	struct FState
	{
		TStrongObjectPtr<UVMCLiveLinkSourceSettings> Settings;

		// The last port check, redone only when the address or port changes (the check opens a socket).
		FString CheckedAddress;
		int32 CheckedPort = -1;
		FString PortProblem;

		FVMCConnectionSettings Current() const { return Settings->ToConnectionSettings(); }

		/** Why the settings can't be used, or empty. */
		FString Errors() const
		{
			TArray<FString> Out;
			Current().Validate(&Out);
			return FString::Join(Out, TEXT("\n"));
		}

		/** Why the port can't be listened on right now, or empty. */
		const FString& PortWarning()
		{
			const FVMCConnectionSettings Now = Current();
			if (Now.BindAddress != CheckedAddress || Now.Port != CheckedPort)
			{
				CheckedAddress = Now.BindAddress;
				CheckedPort = Now.Port;
				PortProblem.Reset();
				FString Error;
				if (Now.Validate() && !FVMCUdpReceiver::CanBind(Now.BindAddress, Now.Port, Error))
				{
					PortProblem = FString::Printf(TEXT("Can't listen on %s:%d: %s. Another program, or another VMC source, may be using the port. The source can still be created; its status will say the port is in use until you pick another in its settings."),
						*Now.BindAddress, Now.Port, *Error);
				}
			}
			return PortProblem;
		}
	};
	TSharedRef<FState> State = MakeShared<FState>();
	State->Settings = TStrongObjectPtr<UVMCLiveLinkSourceSettings>(NewObject<UVMCLiveLinkSourceSettings>(GetTransientPackage(), NAME_None, RF_Transient));
	State->Settings->FromConnectionSettings(FVMCConnectionSettings());

	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs Args;
	Args.bAllowSearch = false;
	Args.bHideSelectionTip = true;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	TSharedRef<IDetailsView> Details = PropertyEditor.CreateDetailView(Args);
	// Only the VMC settings: the Live Link base class's (mode, buffering, connection string) are
	// set up by Live Link and shown in its panel once the source exists.
	Details->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([](const FPropertyAndParent& In)
	{
		const UClass* VMCClass = UVMCLiveLinkSourceSettings::StaticClass();
		if (In.Property.GetOwnerClass() == VMCClass)
		{
			return true;
		}
		for (const FProperty* Parent : In.ParentProperties)
		{
			if (Parent && Parent->GetOwnerClass() == VMCClass)
			{
				return true; // array entries (Allowed Senders)
			}
		}
		return false;
	}));
	Details->SetObject(State->Settings.Get());

	return SNew(SBox)
		.MinDesiredWidth(420.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(4)
			[
				SNew(STextBlock)
					.AutoWrapText(true)
					.Text(NSLOCTEXT("VMCLiveLink", "Desc", "Receive a VMC (OSC over UDP) stream from VSeeFace, VirtualMotionCapture, Warudo and other VMC senders. Hover a setting for what it does."))
			]
			+ SVerticalBox::Slot().FillHeight(1.f).Padding(4)
			[
				Details
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4)
			[
				SNew(STextBlock)
					.AutoWrapText(true)
					.ColorAndOpacity(FLinearColor::Red)
					.Text_Lambda([State] { return FText::FromString(State->Errors()); })
					.Visibility_Lambda([State] { return State->Errors().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4)
			[
				SNew(STextBlock)
					.AutoWrapText(true)
					.ColorAndOpacity(FLinearColor(1.f, 0.75f, 0.f))
					.Text_Lambda([State] { return FText::FromString(State->PortWarning()); })
					.Visibility_Lambda([State] { return State->PortWarning().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(4)
			[
				SNew(SButton)
					.Text(NSLOCTEXT("VMCLiveLink", "Create", "Create"))
					.IsEnabled_Lambda([State] { return State->Errors().IsEmpty(); })
					.OnClicked_Lambda([State, OnCreated]
					{
						const FVMCConnectionSettings Settings = State->Current();
						if (Settings.Validate() && OnCreated.IsBound())
						{
							const TSharedPtr<ILiveLinkSource> Source = MakeShared<FVMCLiveLinkSource>(Settings);
							OnCreated.Execute(Source, Settings.ToString());
						}
						return FReply::Handled();
					})
			]
		];
}
#endif // WITH_EDITOR
