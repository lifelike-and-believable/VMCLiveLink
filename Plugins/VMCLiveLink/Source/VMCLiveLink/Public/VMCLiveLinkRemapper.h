// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "ILiveLinkClient.h"
#include "Features/IModularFeatures.h"
#include "LiveLinkSubjectRemapper.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "Engine/SkeletalMesh.h"
#include "VMCLiveLinkMappingAsset.h" // new
#if WITH_EDITOR
#include "UObject/SoftObjectPtr.h"
#endif
#include "VMCLiveLinkRemapper.generated.h"


UENUM(BlueprintType)
enum class ELLRemapPreset : uint8
{
	None    UMETA(DisplayName	= "None / Manual"),
	ARKit   UMETA(DisplayName	= "ARKit (MetaHuman-friendly)"),
	VMC_VRM UMETA(DisplayName	= "VMC / VRM 0.x expressions (ARKit targets)"),
	VMC_VRM1 UMETA(DisplayName	= "VMC / VRM 1.0 expressions (ARKit targets)"),
	VRoid	UMETA(DisplayName	= "VMC / VRoid"),
	Rokoko  UMETA(DisplayName	= "Rokoko (ARKit names)"),
	Custom  UMETA(DisplayName	= "Custom (JSON)")
};


// ---------------- Worker ----------------
class FVMCLiveLinkRemapperWorker final : public ILiveLinkSubjectRemapperWorker
{
public:
	// Value shaping toggles (copied from asset on CreateWorker)
	bool  bEnableMetaHumanCurveNormalizer = false;
	float JoyToSmileStrength = 1.0f;
	float BlinkMirrorStrength = 1.0f;

	virtual void RemapStaticData(FLiveLinkStaticDataStruct& InOutStaticData) override;
	virtual void RemapFrameData(const FLiveLinkStaticDataStruct& InStatic, FLiveLinkFrameDataStruct& InOutFrameData) override;

	TMap<FName, FName> BoneNameMap;   // copied from asset on CreateWorker
	TMap<FName, FName> CurveNameMap;  // copied from asset on CreateWorker

private:
	/** A curve the normalizer adds because the stream only provides its counterpart. */
	struct FSynthesizedCurve
	{
		int32 SourceIndex = INDEX_NONE; // index of the counterpart in the incoming property values
		float Scale = 1.0f;
	};

	// Captured in RemapStaticData, applied in RemapFrameData. Synthesized curves are appended
	// after the incoming properties, so frames are only extended when their property count
	// matches the count seen at static time.
	TArray<FSynthesizedCurve> SynthesizedCurves;
	int32 IncomingPropertyCount = 0;
	bool bWarnedDuplicateCurves = false;
};

// ---------------- Asset ----------------
UCLASS(MinimalAPI, EditInlineNew, DefaultToInstanced)
class UVMCLiveLinkRemapper final : public ULiveLinkSubjectRemapper
{
	GENERATED_BODY()
public:
	// Remapper API
	virtual void Initialize(const FLiveLinkSubjectKey& InSubjectKey) override;
	virtual TSubclassOf<ULiveLinkRole> GetSupportedRole() const override { return ULiveLinkAnimationRole::StaticClass(); }
	virtual bool IsValidRemapper() const override { return true; }
	virtual FWorkerSharedPtr GetWorker() const override { return Worker; }
	virtual FWorkerSharedPtr CreateWorker() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Evt) override
	{
		Super::PostEditChangeProperty(Evt);
		bDirty = true;       // <-- important
		SyncWorker();
		RequestStaticDataRefresh();
	}
#endif

	// Utilities
	UFUNCTION(BlueprintCallable, Category = "LiveLink|Remapper")
	void ForceRefreshStaticData() { RequestStaticDataRefresh(); }

	UFUNCTION(BlueprintCallable, Category = "LiveLink|Remapper")
	void DetectAndSeedFromSubject();

	UFUNCTION(BlueprintCallable, Category = "LiveLink|Remapper")
	void ApplyPreset(ELLRemapPreset InPreset);

	UFUNCTION(BlueprintCallable, Category = "LiveLink|Remapper")
	void LoadCustomCurveMapFromJSON(const FString& JsonText);

	// Reusable mapping asset selection
	UPROPERTY(EditAnywhere, Category = "Remapper|Preset")
	TSoftObjectPtr<UVMCLiveLinkMappingAsset> MappingAsset;

	// Auto-apply a mapping when ReferenceSkeleton is set/changed (editor)
	UPROPERTY(EditAnywhere, Category = "Remapper|Preset", meta=(EditConditionHides))
	bool bAutoDetectMappingFromReference = true;

	// Optional: control capturing signature when saving into the assigned asset
	UPROPERTY(EditAnywhere, Category = "Remapper|Preset", meta=(DisplayName="Capture Signature On Save"))
	bool bCaptureSignatureOnSave = true;

	UFUNCTION(BlueprintCallable, Category="LiveLink|Remapper")
	void ApplyMappingAsset(UVMCLiveLinkMappingAsset* Asset, bool bAlsoCaptureSignature = false);

	// Scans content for mapping assets and applies the first that matches ReferenceSkeleton
	UFUNCTION(BlueprintCallable, Category="LiveLink|Remapper")
	bool AutoDetectAndApplyMapping();

	// Save the current maps into an asset (optionally capture signature from ReferenceSkeleton)
	UFUNCTION(CallInEditor, Category="LiveLink|Remapper")
	void SaveCurrentMappingTo(UVMCLiveLinkMappingAsset* Asset, bool bCaptureSignatureFromReference);

#if WITH_EDITOR
	// UX buttons (no-arg, show as buttons in Details)
	UFUNCTION(CallInEditor, Category="LiveLink|Remapper", meta=(DisplayName="Apply Selected Mapping Asset"))
	void ApplySelectedMappingAsset();

	UFUNCTION(CallInEditor, Category="LiveLink|Remapper", meta=(DisplayName="Auto Detect and Apply Mapping"))
	void AutoDetectAndApplyMappingInEditor();

	UFUNCTION(CallInEditor, Category="LiveLink|Remapper", meta=(DisplayName="Save Current Mapping to Assigned Asset"))
	void SaveCurrentMappingToAssignedAsset();

	UFUNCTION(CallInEditor, Category="LiveLink|Remapper", meta=(DisplayName="Create New Mapping Asset"))
	void CreateAndAssignNewMappingAsset();
#endif

public:
	// NOTE: BoneNameMap is declared on the base (ULiveLinkSubjectRemapper). Don't redeclare it here.

	UPROPERTY(EditAnywhere, Category = "Remapper")
	TMap<FName, FName> CurveNameMap;

	UPROPERTY(EditAnywhere, Category = "Remapper|Skeleton", meta = (DisplayThumbnail = "false"))
	TSoftObjectPtr<USkeletalMesh> ReferenceSkeleton;

	UPROPERTY(EditAnywhere, Category = "Remapper|Preset")
	ELLRemapPreset Preset = ELLRemapPreset::None;

	/**
	 * Adds ARKit curves the stream does not provide, derived from their counterparts:
	 * - eyeBlinkLeft/Right: when only one side is present, the other side is added as a copy (scaled by BlinkMirrorStrength).
	 * - mouthSmileLeft/Right: when only one side is present, the other side is added as a copy (scaled by JoyToSmileStrength).
	 * - mouthPucker: when absent and mouthFunnel is present, added as half of mouthFunnel.
	 * Curves that the stream does provide are never modified.
	 */
	UPROPERTY(EditAnywhere, Category = "Normalizer")
	bool bEnableMetaHumanCurveNormalizer = false;

	/** Scale for the smile side the normalizer adds when the stream only sends one side. */
	UPROPERTY(EditAnywhere, Category = "Normalizer", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float JoyToSmileStrength = 1.0f;

	/** Scale for the blink side the normalizer adds when the stream only sends one side. */
	UPROPERTY(EditAnywhere, Category = "Normalizer", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BlinkMirrorStrength = 1.0f;

private:
	// Helpers
	void RequestStaticDataRefresh();   // flips bDirty
	void SyncWorker() const;

	void SeedFromReferenceSkeleton();

	/** Map entries a preset seeds. None and Custom seed nothing. */
	static void GetPresetMaps(ELLRemapPreset InPreset, TMap<FName, FName>& OutBones, TMap<FName, FName>& OutCurves);

	/** Removes entries whose key and value both match PresetEntries (entries the user has not edited). */
	static void RemoveUnchangedEntries(TMap<FName, FName>& InOutMap, const TMap<FName, FName>& PresetEntries);

	void SeedBones_FromHumanoidLike(const TArray<FName>& Incoming);

	ELLRemapPreset GuessPreset(const TArray<FName>& BoneNames, const TArray<FName>& CurveNames) const;

private:
	FLiveLinkSubjectKey CachedKey;
	mutable TSharedPtr<FVMCLiveLinkRemapperWorker> Worker;
};
