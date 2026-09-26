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

/** Everything a worker remaps with. Copied when the worker is created; the worker never changes it. */
struct FVMCRemapConfig
{
	TMap<FName, FName> BoneNameMap;
	TMap<FName, FName> CurveNameMap;

	/** Local rest translation of each bone of the target skeleton, by target (remapped) name. */
	TMap<FName, FVector> RefTranslations;
	/** Give bones the stream sends without a translation their target skeleton's rest translation. */
	bool bUseRefTranslations = true;

	bool  bEnableMetaHumanCurveNormalizer = false;
	float JoyToSmileStrength = 1.0f;
	float BlinkMirrorStrength = 1.0f;
};

/**
 * The one place VMC names become target names (P3.2). Immutable: the remapper builds a new worker
 * whenever its settings change, and Live Link swaps it in, so a worker in use is never edited
 * under it. Per-static-data state (which curves to synthesize, which bones get a rest
 * translation) is computed in RemapStaticData and used by RemapFrameData.
 */
class FVMCLiveLinkRemapperWorker final : public ILiveLinkSubjectRemapperWorker
{
public:
	explicit FVMCLiveLinkRemapperWorker(FVMCRemapConfig InConfig) : Config(MoveTemp(InConfig)) {}

	const FVMCRemapConfig& GetConfig() const { return Config; }

	virtual void RemapStaticData(FLiveLinkStaticDataStruct& InOutStaticData) override;
	virtual void RemapFrameData(const FLiveLinkStaticDataStruct& InStatic, FLiveLinkFrameDataStruct& InOutFrameData) override;

private:
	const FVMCRemapConfig Config;

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

	// Per incoming bone: the rest translation to use when the stream sends none (root and Hips,
	// whose translations carry the motion, have none).
	TArray<FVector> RestTranslations;
	TBitArray<> HasRestTranslation;

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
	/** A new worker from the current settings. Live Link calls it again whenever the remapper is dirty. */
	virtual FWorkerSharedPtr CreateWorker() override;

	/** Goes up with every change, so a source can republish its static data through the new worker. */
	uint32 GetRevision() const { return Revision; }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Evt) override
	{
		Super::PostEditChangeProperty(Evt);
		MarkDirty(); // Live Link builds a new worker from the edited settings
	}
#endif

	// Utilities
	UFUNCTION(BlueprintCallable, Category = "LiveLink|Remapper")
	void ForceRefreshStaticData() { MarkDirty(); }

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

	/** The target skeleton. Its rest pose gives bones the stream sends without a translation
	 *  their length. Falls back to the project's Default Reference Skeleton (VMC Live Link settings). */
	UPROPERTY(EditAnywhere, Category = "Remapper|Skeleton", meta = (DisplayThumbnail = "false"))
	TSoftObjectPtr<USkeletalMesh> ReferenceSkeleton;

	/** Give bones the stream sends without a translation the reference skeleton's rest translation
	 *  (VMC senders give most bones rotation only). Root and Hips always use the stream. */
	UPROPERTY(EditAnywhere, Category = "Remapper|Skeleton")
	bool bUseReferenceTranslations = true;

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
	void MarkDirty();   // Live Link creates a new worker and republishes the static data

	/** The reference skeleton, or the project default. May load it. */
	USkeletalMesh* ResolveReferenceSkeleton() const;

	void SeedFromReferenceSkeleton();

	/** Map entries a preset seeds. None and Custom seed nothing. */
	static void GetPresetMaps(ELLRemapPreset InPreset, TMap<FName, FName>& OutBones, TMap<FName, FName>& OutCurves);

	/** Removes entries whose key and value both match PresetEntries (entries the user has not edited). */
	static void RemoveUnchangedEntries(TMap<FName, FName>& InOutMap, const TMap<FName, FName>& PresetEntries);

	void SeedBones_FromHumanoidLike(const TArray<FName>& Incoming);

	ELLRemapPreset GuessPreset(const TArray<FName>& BoneNames, const TArray<FName>& CurveNames) const;

private:
	FLiveLinkSubjectKey CachedKey;
	uint32 Revision = 0;
	TSharedPtr<FVMCLiveLinkRemapperWorker> Worker;
};
