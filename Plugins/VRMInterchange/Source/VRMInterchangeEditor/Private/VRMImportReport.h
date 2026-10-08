// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "UObject/SoftObjectPath.h"

/**
 * What a VRM import made, shown once when it finishes (P6.3): one notification listing the assets
 * the VRM pipelines created or updated and the avatar's licence, with buttons to find the assets
 * and to open the "VRM Import" message log, where the import's warnings and errors are listed.
 *
 * The pipelines report to it as they go (UVRMPipelineBase does for the mesh and every asset it
 * creates); it shows the report shortly after the last one, on the game thread.
 */
class VRMINTERCHANGEEDITOR_API FVRMImportReport
{
public:
	/** The message log the import warnings go to. */
	static const FName LogName;

	/** Seconds after the last report before it is shown. */
	static constexpr float ShowDelaySeconds = 0.5f;

	struct FAsset
	{
		FSoftObjectPath Path;
		FString Name;
		FString Kind;          // "IK Rig", "Spring data", ...
		bool bUpdated = false; // an asset already there was updated in place
		bool bRewired = false; // updated, but only pointed at the new mesh (Blueprints): edits kept
	};

	struct FFile
	{
		FString SourceFile;
		bool bReimport = false;
		TArray<FAsset> Assets;
		FString License;
	};

	static FVRMImportReport& Get();

	/** The import of SourceFile has its skeletal mesh; the calls below report to it until another file begins. */
	void BeginFile(const FString& SourceFile, bool bReimport);
	/** An asset of the current file: created, or updated in place (bUpdated). Reported once however often it's added. */
	void AddAsset(const UObject* Asset, bool bUpdated);
	/** The current file's licence summary (VRM::DescribeLicense). */
	void SetLicense(const FString& License);

	/** Shows what is pending, the import messages collected since the import began, and clears both. */
	void Flush();
	/** Clears what is pending without showing it (tests). */
	void Discard();

	const TArray<FFile>& GetPendingFiles() const { return Files; }
	/** The pending entry for SourceFile, or null. The report is only flushed by a ticker, so a script
	 *  or test that imports several files keeps them all pending: find a file by its source. */
	const FFile* FindFile(const FString& SourceFile) const
	{
		return Files.FindByPredicate([&SourceFile](const FFile& File) { return File.SourceFile == SourceFile; });
	}

	/** The notification's text for one file: what was created, updated and the licence. */
	static FString Describe(const FFile& File);

	/** The kind of asset shown in the report ("Skeletal mesh", "IK Rig", ...). */
	static FString DescribeKind(const UObject* Asset);

private:
	FFile* FindCurrent();
	void ScheduleShow();

	TArray<FFile> Files;
	FString CurrentFile;
	FTSTicker::FDelegateHandle ShowHandle;
};
