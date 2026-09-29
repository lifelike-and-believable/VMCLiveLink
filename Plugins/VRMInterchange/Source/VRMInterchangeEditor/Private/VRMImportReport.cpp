// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMImportReport.h"

#include "Animation/AnimBlueprint.h"
#include "ContentBrowserModule.h"
#include "Engine/Blueprint.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IContentBrowserSingleton.h"
#include "AssetRegistry/AssetData.h"
#include "Logging/MessageLog.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Rig/IKRigDefinition.h"
#include "VRMAvatarDescription.h"
#include "VRMImportMessages.h"
#include "VRMSpringBoneData.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "VRMImportReport"

const FName FVRMImportReport::LogName(TEXT("VRMInterchange"));

FVRMImportReport& FVRMImportReport::Get()
{
	static FVRMImportReport Report;
	return Report;
}

FVRMImportReport::FFile* FVRMImportReport::FindCurrent()
{
	return Files.FindByPredicate([this](const FFile& File) { return File.SourceFile == CurrentFile; });
}

void FVRMImportReport::BeginFile(const FString& SourceFile, bool bReimport)
{
	CurrentFile = SourceFile;
	if (FFile* Existing = FindCurrent())
	{
		Existing->bReimport |= bReimport;
	}
	else
	{
		FFile& File = Files.AddDefaulted_GetRef();
		File.SourceFile = SourceFile;
		File.bReimport = bReimport;
	}
	ScheduleShow();
}

void FVRMImportReport::AddAsset(const UObject* Asset, bool bUpdated)
{
	FFile* File = FindCurrent();
	if (!Asset || !File)
	{
		return;
	}
	const FSoftObjectPath Path(Asset);
	if (File->Assets.ContainsByPredicate([&Path](const FAsset& Existing) { return Existing.Path == Path; }))
	{
		return;
	}
	FAsset& Entry = File->Assets.AddDefaulted_GetRef();
	Entry.Path = Path;
	Entry.Name = Asset->GetName();
	Entry.Kind = DescribeKind(Asset);
	Entry.bUpdated = bUpdated;
	// Blueprints that already exist are only pointed at the new mesh (UVRMLiveLinkPostImportPipeline,
	// the spring post-process AnimBlueprint); everything else is rebuilt from the file.
	Entry.bRewired = bUpdated && Asset->IsA<UBlueprint>();
	ScheduleShow();
}

void FVRMImportReport::SetLicense(const FString& License)
{
	if (FFile* File = FindCurrent())
	{
		File->License = License;
	}
}

FString FVRMImportReport::DescribeKind(const UObject* Asset)
{
	if (!Asset) return FString();
	if (Asset->IsA<USkeletalMesh>()) return TEXT("Skeletal mesh");
	if (Asset->IsA<UVRMSpringBoneData>()) return TEXT("Spring data");
	if (Asset->IsA<UIKRigDefinition>()) return TEXT("IK Rig");
	if (Asset->IsA<UVRMAvatarDescription>()) return TEXT("Avatar description");
	if (Asset->IsA<UAnimBlueprint>()) return TEXT("Animation Blueprint");
	if (Asset->IsA<UBlueprint>()) return TEXT("Actor Blueprint");
	return Asset->GetClass()->GetName();
}

FString FVRMImportReport::Describe(const FFile& File)
{
	auto List = [&File](TFunctionRef<bool(const FAsset&)> Filter)
	{
		TArray<FString> Parts;
		for (const FAsset& Asset : File.Assets)
		{
			if (Filter(Asset))
			{
				Parts.Add(FString::Printf(TEXT("%s (%s)"), *Asset.Name, *Asset.Kind));
			}
		}
		return FString::Join(Parts, TEXT(", "));
	};

	TArray<FString> Lines;
	const FString Created = List([](const FAsset& A) { return !A.bUpdated; });
	const FString Regenerated = List([](const FAsset& A) { return A.bUpdated && !A.bRewired; });
	const FString Rewired = List([](const FAsset& A) { return A.bRewired; });
	if (!Created.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("Created: %s."), *Created));
	}
	if (!Regenerated.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("Updated in place from the file (references kept, edits to them replaced): %s."), *Regenerated));
	}
	if (!Rewired.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("Pointed at the new mesh (your edits kept): %s."), *Rewired));
	}
	if (File.bReimport)
	{
		Lines.Add(TEXT("Reimport: the mesh, its materials and textures come from the file again."));
	}
	if (!File.License.IsEmpty())
	{
		Lines.Add(File.License);
	}
	return FString::Join(Lines, TEXT("\n"));
}

void FVRMImportReport::ScheduleShow()
{
	// Shown once the pipelines have gone quiet: every VRM pipeline of the import reports within the
	// same few frames, so one notification covers them all.
	if (ShowHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ShowHandle);
	}
	ShowHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float)
	{
		ShowHandle.Reset();
		Flush();
		return false;
	}), ShowDelaySeconds);
}

void FVRMImportReport::Discard()
{
	if (ShowHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ShowHandle);
		ShowHandle.Reset();
	}
	Files.Reset();
	CurrentFile.Reset();
}

void FVRMImportReport::Flush()
{
	TArray<FFile> Shown = MoveTemp(Files);
	Discard();
	if (Shown.Num() == 0)
	{
		return;
	}
	// Each file's own messages (imports that overlap keep theirs apart), plus any logged outside an import.
	TArray<VRM::ImportMessages::FMessage> Messages;
	for (const FFile& File : Shown)
	{
		Messages.Append(VRM::ImportMessages::Take(File.SourceFile));
	}

	// The message log page: what was made, then every warning and error the import logged.
	TArray<FString> Names;
	for (const FFile& File : Shown)
	{
		Names.Add(FPaths::GetCleanFilename(File.SourceFile));
	}
	const FText Title = FText::FromString(FString::Join(Names, TEXT(", ")));
	int32 Warnings = 0, Errors = 0;
	{
		// Already in the output log as they happened, so only shown here.
		FMessageLog Log(LogName);
		Log.SuppressLoggingToOutputLog(true);
		Log.NewPage(FText::Format(LOCTEXT("PageTitle", "Import of {0}"), Title));
		for (const FFile& File : Shown)
		{
			Log.Info(FText::FromString(FString::Printf(TEXT("%s\n%s"), *FPaths::GetCleanFilename(File.SourceFile), *Describe(File))));
		}
		for (const VRM::ImportMessages::FMessage& Message : Messages)
		{
			if (Message.Verbosity == ELogVerbosity::Warning)
			{
				Log.Warning(FText::FromString(Message.Text));
				++Warnings;
			}
			else
			{
				Log.Error(FText::FromString(Message.Text));
				++Errors;
			}
		}
		if (Messages.Num() == 0)
		{
			Log.Info(LOCTEXT("NoProblems", "No warnings."));
		}
	}

	if (FApp::IsUnattended() || !FSlateApplication::IsInitialized())
	{
		return;
	}

	TArray<FSoftObjectPath> Paths;
	TArray<FString> Bodies;
	bool bReimport = false;
	for (const FFile& File : Shown)
	{
		for (const FAsset& Asset : File.Assets)
		{
			Paths.Add(Asset.Path);
		}
		Bodies.Add(Shown.Num() > 1 ? FString::Printf(TEXT("%s: %s"), *FPaths::GetCleanFilename(File.SourceFile), *Describe(File)) : Describe(File));
		bReimport |= File.bReimport;
	}

	FNotificationInfo Info(FText::Format(bReimport ? LOCTEXT("Reimported", "Reimported {0}") : LOCTEXT("Imported", "Imported {0}"), Title));
	Info.SubText = FText::FromString(FString::Join(Bodies, TEXT("\n\n")));
	Info.bUseLargeFont = false;
	Info.ExpireDuration = 12.f;
	Info.ButtonDetails.Add(FNotificationButtonInfo(
		LOCTEXT("ShowAssets", "Show in Content Browser"),
		LOCTEXT("ShowAssetsTip", "Select the assets this import made in the Content Browser."),
		FSimpleDelegate::CreateLambda([Paths]()
		{
			TArray<FAssetData> Assets;
			for (const FSoftObjectPath& Path : Paths)
			{
				if (UObject* Object = Path.ResolveObject())
				{
					Assets.Add(FAssetData(Object));
				}
			}
			FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
			ContentBrowser.Get().SyncBrowserToAssets(Assets);
		}),
		SNotificationItem::CS_None));
	Info.ButtonDetails.Add(FNotificationButtonInfo(
		Warnings + Errors > 0
			? FText::Format(LOCTEXT("ShowProblems", "Show {0} {0}|plural(one=problem,other=problems)"), FText::AsNumber(Warnings + Errors))
			: LOCTEXT("ShowLog", "Show import log"),
		LOCTEXT("ShowLogTip", "Open the VRM Import message log."),
		FSimpleDelegate::CreateLambda([]() { FMessageLog(LogName).Open(); }),
		SNotificationItem::CS_None));
	FSlateNotificationManager::Get().AddNotification(Info);
}

#undef LOCTEXT_NAMESPACE
