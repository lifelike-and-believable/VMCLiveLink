// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VMCLiveLinkRemapperCustomization.h"
#include "VMCLiveLinkRemapper.h"
#include "VMCLiveLinkMappingAsset.h"
#include "SVMCMappingTable.h"

#include "AssetToolsModule.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"
#include "Factories/DataAssetFactory.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"

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
	TArray<TWeakObjectPtr<UVMCLiveLinkRemapper>> Remappers;
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	for (const TWeakObjectPtr<UObject>& Object : Objects)
	{
		if (UVMCLiveLinkRemapper* Remapper = Cast<UVMCLiveLinkRemapper>(Object.Get()))
		{
			Remappers.Add(Remapper);
		}
	}

	// P6.2: the details read top to bottom as Target (what the names are checked against) ->
	// Mapping (preset, asset and the maps) -> Normalizer -> Mapping Tools -> Live Mapping (the
	// table of what each incoming name becomes).
	IDetailCategoryBuilder& Target = DetailBuilder.EditCategory(TEXT("Target"), LOCTEXT("Target", "Target"), ECategoryPriority::Important);
	Target.AddCustomRow(LOCTEXT("TargetHelpFilter", "Target Reference Skeleton"))
		.WholeRowContent()
		[
			SNew(STextBlock)
				.AutoWrapText(true)
				.Font(IDetailLayoutBuilder::GetDetailFontItalic())
				.Text(LOCTEXT("TargetHelp", "The mesh the stream drives. Its bones and morph targets are what the Live Mapping table checks names against, and its rest pose gives streamed bones their lengths."))
		];
	Target.AddProperty(GET_MEMBER_NAME_CHECKED(UVMCLiveLinkRemapper, ReferenceSkeleton));
	Target.AddProperty(GET_MEMBER_NAME_CHECKED(UVMCLiveLinkRemapper, bUseReferenceTranslations));

	IDetailCategoryBuilder& Mapping = DetailBuilder.EditCategory(TEXT("Mapping"), LOCTEXT("Mapping", "Mapping"), ECategoryPriority::Important);
	Mapping.AddProperty(GET_MEMBER_NAME_CHECKED(UVMCLiveLinkRemapper, Preset));
	Mapping.AddProperty(GET_MEMBER_NAME_CHECKED(UVMCLiveLinkRemapper, MappingAsset));
	Mapping.AddProperty(GET_MEMBER_NAME_CHECKED(UVMCLiveLinkRemapper, bAutoDetectMappingFromReference));
	Mapping.AddProperty(GET_MEMBER_NAME_CHECKED(UVMCLiveLinkRemapper, bCaptureSignatureOnSave));
	// The bone map is the Live Link base class's property; show it with the curve map.
	const TSharedRef<IPropertyHandle> BoneMap = DetailBuilder.GetProperty(TEXT("BoneNameMap"), ULiveLinkSubjectRemapper::StaticClass());
	if (BoneMap->IsValidHandle())
	{
		Mapping.AddProperty(BoneMap);
	}
	Mapping.AddProperty(GET_MEMBER_NAME_CHECKED(UVMCLiveLinkRemapper, CurveNameMap));

	IDetailCategoryBuilder& Normalizer = DetailBuilder.EditCategory(TEXT("Normalizer"), LOCTEXT("Normalizer", "Normalizer"), ECategoryPriority::Important);
	Normalizer.AddCustomRow(LOCTEXT("NormalizerHelpFilter", "Normalizer MetaHuman ARKit"))
		.WholeRowContent()
		[
			SNew(STextBlock)
				.AutoWrapText(true)
				.Font(IDetailLayoutBuilder::GetDetailFontItalic())
				.Text(LOCTEXT("NormalizerHelp", "Off by default. For ARKit targets (MetaHuman) fed by senders that only send some ARKit curves: it adds the missing blink or smile side as a copy of the other, and mouthPucker from mouthFunnel. It never changes a curve the stream sends."))
		];
	Normalizer.AddProperty(GET_MEMBER_NAME_CHECKED(UVMCLiveLinkRemapper, bEnableMetaHumanCurveNormalizer));
	Normalizer.AddProperty(GET_MEMBER_NAME_CHECKED(UVMCLiveLinkRemapper, JoyToSmileStrength));
	Normalizer.AddProperty(GET_MEMBER_NAME_CHECKED(UVMCLiveLinkRemapper, BlinkMirrorStrength));

	AddMappingTools(DetailBuilder, Remappers, Builder, ECategoryPriority::Important);
}

void FVMCLiveLinkRemapperCustomization::AddMappingTools(IDetailLayoutBuilder& DetailBuilder, const TArray<TWeakObjectPtr<UVMCLiveLinkRemapper>>& Remappers,
	const TWeakPtr<IDetailLayoutBuilder>& RefreshBuilder, ECategoryPriority::Type Priority)
{
	IDetailCategoryBuilder& Tools = DetailBuilder.EditCategory(TEXT("Mapping Tools"), LOCTEXT("MappingTools", "Mapping Tools"), Priority);

	// The buttons outlive this call; they keep their own copies of the remappers and the panel.
	auto RunAction = [Remappers, RefreshBuilder](const FText& TransactionName, TFunctionRef<void(UVMCLiveLinkRemapper&)> Action)
	{
		return Run(Remappers, RefreshBuilder, TransactionName, Action);
	};

	TSharedRef<SWrapBox> Buttons = SNew(SWrapBox).UseAllottedSize(true);
	auto AddButton = [&Buttons](const FText& Label, const FText& Tooltip, TFunction<FReply()> OnClicked)
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
		[RunAction]() { return RunAction(LOCTEXT("ApplyPresetTx", "Apply VMC Remapper Preset"), [](UVMCLiveLinkRemapper& R) { R.ApplyPreset(R.Preset); }); });
	AddButton(LOCTEXT("SeedFromSubject", "Seed From Subject"),
		LOCTEXT("SeedFromSubjectTip", "Lists the names the subject is receiving and applies the preset that fits them."),
		[RunAction]() { return RunAction(LOCTEXT("SeedTx", "Seed VMC Remapper From Subject"), [](UVMCLiveLinkRemapper& R) { R.DetectAndSeedFromSubject(); }); });
	AddButton(LOCTEXT("ApplyAsset", "Apply Mapping Asset"),
		LOCTEXT("ApplyAssetTip", "Replaces the maps with the selected mapping asset's."),
		[RunAction]() { return RunAction(LOCTEXT("ApplyAssetTx", "Apply VMC Mapping Asset"), [](UVMCLiveLinkRemapper& R) { R.ApplyMappingAsset(R.MappingAsset.LoadSynchronous()); }); });
	AddButton(LOCTEXT("FromHumanoid", "Map Bones From Humanoid Metadata"),
		LOCTEXT("FromHumanoidTip", "Replaces the bone map with the humanoid map stored on the reference skeleton (VRM.Humanoid.* metadata, written by VRM importers). Curves are left as they are."),
		[RunAction]() { return RunAction(LOCTEXT("FromHumanoidTx", "Map VMC Bones From Humanoid Metadata"), [](UVMCLiveLinkRemapper& R) { R.MapBonesFromHumanoidMetadata(); }); });
	AddButton(LOCTEXT("AutoDetect", "Auto-Detect Mapping"),
		LOCTEXT("AutoDetectTip", "Finds the mapping asset made for the reference skeleton and applies it."),
		[RunAction]() { return RunAction(LOCTEXT("AutoDetectTx", "Auto-Detect VMC Mapping"), [](UVMCLiveLinkRemapper& R) { R.AutoDetectAndApplyMapping(); }); });
	AddButton(LOCTEXT("SaveToAsset", "Save to Mapping Asset"),
		LOCTEXT("SaveToAssetTip", "Saves the maps into the selected mapping asset (and the reference skeleton's signature, if Capture Signature On Save is on)."),
		[RunAction]() { return RunAction(LOCTEXT("SaveTx", "Save VMC Mapping Asset"), [](UVMCLiveLinkRemapper& R)
		{
			if (UVMCLiveLinkMappingAsset* Asset = R.MappingAsset.LoadSynchronous())
			{
				R.SaveCurrentMappingTo(Asset, R.bCaptureSignatureOnSave);
				Asset->MarkPackageDirty();
			}
		}); });
	AddButton(LOCTEXT("CreateAsset", "Create Mapping Asset..."),
		LOCTEXT("CreateAssetTip", "Creates a mapping asset from the current maps and the reference skeleton's signature, and selects it."),
		[RunAction]() { return RunAction(LOCTEXT("CreateTx", "Create VMC Mapping Asset"), [](UVMCLiveLinkRemapper& R) { CreateMappingAsset(R); }); });

	Tools.AddCustomRow(LOCTEXT("MappingToolsFilter", "Mapping Tools Preset Asset Seed Detect Save Create Humanoid Metadata"))
		.WholeRowContent()
		[
			Buttons
		];

	// One table for one remapper; with several selected, their names differ.
	if (Remappers.Num() == 1)
	{
		IDetailCategoryBuilder& Live = DetailBuilder.EditCategory(TEXT("Live Mapping"), LOCTEXT("LiveMapping", "Live Mapping"), Priority);
		Live.AddCustomRow(LOCTEXT("LiveMappingFilter", "Live Mapping Incoming Outgoing Unmapped Duplicate"))
			.WholeRowContent()
			[
				SNew(SVMCMappingTable, Remappers[0])
			];
	}
}

FReply FVMCLiveLinkRemapperCustomization::Run(const TArray<TWeakObjectPtr<UVMCLiveLinkRemapper>>& Remappers, const TWeakPtr<IDetailLayoutBuilder>& RefreshBuilder,
	const FText& TransactionName, TFunctionRef<void(UVMCLiveLinkRemapper&)> Action)
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
	if (const TSharedPtr<IDetailLayoutBuilder> Pinned = RefreshBuilder.Pin())
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
