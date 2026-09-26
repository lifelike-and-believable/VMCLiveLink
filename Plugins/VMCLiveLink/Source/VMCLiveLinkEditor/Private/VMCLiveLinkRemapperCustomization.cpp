// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VMCLiveLinkRemapperCustomization.h"
#include "VMCLiveLinkRemapper.h"
#include "VMCLiveLinkMappingAsset.h"

#include "AssetToolsModule.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Factories/DataAssetFactory.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SWrapBox.h"

#define LOCTEXT_NAMESPACE "VMCLiveLinkRemapperCustomization"

TSharedRef<IDetailCustomization> FVMCLiveLinkRemapperCustomization::MakeInstance()
{
	return MakeShared<FVMCLiveLinkRemapperCustomization>();
}

void FVMCLiveLinkRemapperCustomization::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder)
{
	Builder = DetailBuilder;
	CustomizeDetails(*DetailBuilder);
}

void FVMCLiveLinkRemapperCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	Remappers.Reset();
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	for (const TWeakObjectPtr<UObject>& Object : Objects)
	{
		if (UVMCLiveLinkRemapper* Remapper = Cast<UVMCLiveLinkRemapper>(Object.Get()))
		{
			Remappers.Add(Remapper);
		}
	}

	IDetailCategoryBuilder& Tools = DetailBuilder.EditCategory(TEXT("Mapping Tools"), LOCTEXT("MappingTools", "Mapping Tools"), ECategoryPriority::Important);

	TSharedRef<SWrapBox> Buttons = SNew(SWrapBox).UseAllottedSize(true);
	auto AddButton = [this, &Buttons](const FText& Label, const FText& Tooltip, TFunction<FReply()> OnClicked)
	{
		Buttons->AddSlot().Padding(2.f)
		[
			SNew(SButton)
				.Text(Label)
				.ToolTipText(Tooltip)
				.OnClicked_Lambda(MoveTemp(OnClicked))
		];
	};

	AddButton(LOCTEXT("ApplyPreset", "Apply Preset"),
		LOCTEXT("ApplyPresetTip", "Adds the selected preset's entries to the maps (entries you edited are kept)."),
		[this]() { return Run(LOCTEXT("ApplyPresetTx", "Apply VMC Remapper Preset"), [](UVMCLiveLinkRemapper& R) { R.ApplyPreset(R.Preset); }); });
	AddButton(LOCTEXT("SeedFromSubject", "Seed From Subject"),
		LOCTEXT("SeedFromSubjectTip", "Lists the names the subject is receiving and applies the preset that fits them."),
		[this]() { return Run(LOCTEXT("SeedTx", "Seed VMC Remapper From Subject"), [](UVMCLiveLinkRemapper& R) { R.DetectAndSeedFromSubject(); }); });
	AddButton(LOCTEXT("ApplyAsset", "Apply Mapping Asset"),
		LOCTEXT("ApplyAssetTip", "Replaces the maps with the selected mapping asset's."),
		[this]() { return Run(LOCTEXT("ApplyAssetTx", "Apply VMC Mapping Asset"), [](UVMCLiveLinkRemapper& R) { R.ApplyMappingAsset(R.MappingAsset.LoadSynchronous()); }); });
	AddButton(LOCTEXT("AutoDetect", "Auto-Detect Mapping"),
		LOCTEXT("AutoDetectTip", "Finds the mapping asset made for the reference skeleton and applies it."),
		[this]() { return Run(LOCTEXT("AutoDetectTx", "Auto-Detect VMC Mapping"), [](UVMCLiveLinkRemapper& R) { R.AutoDetectAndApplyMapping(); }); });
	AddButton(LOCTEXT("SaveToAsset", "Save to Mapping Asset"),
		LOCTEXT("SaveToAssetTip", "Saves the maps into the selected mapping asset (and the reference skeleton's signature, if Capture Signature On Save is on)."),
		[this]() { return Run(LOCTEXT("SaveTx", "Save VMC Mapping Asset"), [](UVMCLiveLinkRemapper& R)
		{
			if (UVMCLiveLinkMappingAsset* Asset = R.MappingAsset.LoadSynchronous())
			{
				R.SaveCurrentMappingTo(Asset, R.bCaptureSignatureOnSave);
				Asset->MarkPackageDirty();
			}
		}); });
	AddButton(LOCTEXT("CreateAsset", "Create Mapping Asset..."),
		LOCTEXT("CreateAssetTip", "Creates a mapping asset from the current maps and the reference skeleton's signature, and selects it."),
		[this]() { return Run(LOCTEXT("CreateTx", "Create VMC Mapping Asset"), [](UVMCLiveLinkRemapper& R) { CreateMappingAsset(R); }); });

	Tools.AddCustomRow(LOCTEXT("MappingToolsFilter", "Mapping Tools Preset Asset Seed Detect Save Create"))
		.WholeRowContent()
		[
			Buttons
		];
}

FReply FVMCLiveLinkRemapperCustomization::Run(const FText& TransactionName, TFunctionRef<void(UVMCLiveLinkRemapper&)> Action)
{
	{
		const FScopedTransaction Transaction(TransactionName);
		for (const TWeakObjectPtr<UVMCLiveLinkRemapper>& Weak : Remappers)
		{
			if (UVMCLiveLinkRemapper* Remapper = Weak.Get())
			{
				Remapper->Modify();
				Action(*Remapper);
			}
		}
	}
	// Map entries may have been added or removed; rebuild the rows.
	if (const TSharedPtr<IDetailLayoutBuilder> Pinned = Builder.Pin())
	{
		Pinned->ForceRefreshDetails();
	}
	return FReply::Handled();
}

void FVMCLiveLinkRemapperCustomization::CreateMappingAsset(UVMCLiveLinkRemapper& Remapper)
{
	// Next to the reference mesh if there is one
	FString DefaultPath = TEXT("/Game");
	if (USkeletalMesh* Ref = Remapper.ReferenceSkeleton.LoadSynchronous())
	{
		DefaultPath = FPackageName::GetLongPackagePath(Ref->GetOutermost()->GetName());
	}

	UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
	Factory->DataAssetClass = UVMCLiveLinkMappingAsset::StaticClass();
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UVMCLiveLinkMappingAsset* NewMapping = Cast<UVMCLiveLinkMappingAsset>(
		AssetTools.CreateAssetWithDialog(TEXT("VMCMapping"), DefaultPath, UVMCLiveLinkMappingAsset::StaticClass(), Factory));
	if (!NewMapping)
	{
		return; // cancelled
	}
	Remapper.SaveCurrentMappingTo(NewMapping, /*bCaptureSignatureFromReference=*/true);
	Remapper.ApplyMappingAsset(NewMapping);
	NewMapping->MarkPackageDirty();
}

#undef LOCTEXT_NAMESPACE
