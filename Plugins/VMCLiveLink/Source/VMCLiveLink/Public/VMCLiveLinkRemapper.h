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
#include "VMCLiveLinkMappingAsset.h"
#include "UObject/SoftObjectPtr.h"
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
/**
 * Renames VMC bones and curves for a target skeleton (P3.2). Editing tools (apply or save a mapping
 * asset, auto-detect, seed from the subject) are buttons in its details panel, provided by the
 * VMCLiveLinkEditor module.
 */
UCLASS(EditInlineNew, DefaultToInstanced)
class VMCLIVELINK_API UVMCLiveLinkRemapper final : public ULiveLinkSubjectRemapper
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

	/** While both maps are empty, pick the mapping asset that matches the reference skeleton. */
	UPROPERTY(EditAnywhere, Category = "Remapper|Preset")
	bool bAutoDetectMappingFromReference = true;

	/** Saving into the mapping asset also records the reference skeleton's signature, so the asset is found for it later. */
	UPROPERTY(EditAnywhere, Category = "Remapper|Preset", meta=(DisplayName="Capture Signature On Save"))
	bool bCaptureSignatureOnSave = true;

	UFUNCTION(BlueprintCallable, Category="LiveLink|Remapper")
	void ApplyMappingAsset(UVMCLiveLinkMappingAsset* Asset, bool bAlsoCaptureSignature = false);

	/**
	 * Finds the mapping asset for the reference skeleton and applies it, loading only the assets
	 * whose signature tag matches (then, failing those, the rest). Blocks while loading; prefer
	 * StartAutoDetectMapping.
	 */
	UFUNCTION(BlueprintCallable, Category="LiveLink|Remapper")
	bool AutoDetectAndApplyMapping();

	/**
	 * The same, loading asynchronously; the mapping is applied when the loads finish. With
	 * bOnlyIfMapsEmpty, it isn't applied if the maps were filled in the meantime.
	 * Returns false if there's nothing to look for (no reference skeleton).
	 */
	bool StartAutoDetectMapping(bool bOnlyIfMapsEmpty);

	/** Saves the current maps into an asset (and the reference skeleton's signature, if asked). */
	UFUNCTION(BlueprintCallable, Category="LiveLink|Remapper")
	void SaveCurrentMappingTo(UVMCLiveLinkMappingAsset* Asset, bool bCaptureSignatureFromReference);

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

	/** Candidate mapping assets for a signature, from the asset registry: tag matches first, then
	 *  assets whose signatures are too old to have a usable tag. And every mapping asset. */
	static void FindMappingCandidates(uint32 Signature, TArray<FSoftObjectPath>& OutLikely, TArray<FSoftObjectPath>& OutAll);

	/** The loaded asset to use for Ref: one that matches it, else the best name overlap. */
	static UVMCLiveLinkMappingAsset* ChooseMapping(USkeletalMesh* Ref, TConstArrayView<FSoftObjectPath> Candidates, bool bAllowHeuristic);

	/** One pass of StartAutoDetectMapping: loads the pass's candidates (unless bLoadsDone), then chooses. */
	void OnAutoDetectLoaded(TArray<FSoftObjectPath> Likely, TArray<FSoftObjectPath> All, bool bSecondPass, bool bOnlyIfMapsEmpty, bool bLoadsDone);

	ELLRemapPreset GuessPreset(const TArray<FName>& BoneNames, const TArray<FName>& CurveNames) const;

private:
	FLiveLinkSubjectKey CachedKey;
	uint32 Revision = 0;
	TSharedPtr<FVMCLiveLinkRemapperWorker> Worker;
};
