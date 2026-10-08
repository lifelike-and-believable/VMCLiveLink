// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
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


/** Naming schemes UVMCLiveLinkRemapper::ApplyPreset can seed the maps with (see the README's preset table). */
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
	/** Incoming bone name to target bone name. */
	TMap<FName, FName> BoneNameMap;
	/** Incoming curve name to target curve name. */
	TMap<FName, FName> CurveNameMap;

	/** Local rest translation of each bone of the target skeleton (cm, parent bone space), by target (remapped) name. */
	TMap<FName, FVector> RefTranslations;
	/** Give bones the stream sends without a translation their target skeleton's rest translation. */
	bool bUseRefTranslations = true;

	/** Add missing ARKit blink, smile and pucker curves (UVMCLiveLinkRemapper::bEnableMetaHumanCurveNormalizer). */
	bool  bEnableMetaHumanCurveNormalizer = false;
	/** Scale of an added smile side, 0 to 1. */
	float JoyToSmileStrength = 1.0f;
	/** Scale of an added blink side, 0 to 1. */
	float BlinkMirrorStrength = 1.0f;

	/** Log problems found in the static data (duplicate targets). Off for the details panel's
	 *  mapping table, which shows them instead. */
	bool bLogProblems = true;
};

/** One incoming name and what the remapper makes of it: a row of the details panel's mapping table (P6.2). */
struct FVMCMappingRow
{
	FName Incoming;                // the name the stream sends; None for a curve the normalizer adds
	FName Outgoing;                // the name the subject has after remapping
	bool bMapped = false;          // a map entry renames it; otherwise it passes through unchanged
	bool bDuplicateTarget = false; // another incoming name of the same kind ends up with the same name
	bool bNotOnTarget = false;     // the reference mesh has no bone (for bones) or morph target (for curves) with that name
	bool bSynthesized = false;     // added by the curve normalizer
};

/**
 * The one place VMC names become target names (P3.2). Its configuration is fixed: a worker is made
 * from the remapper's settings (UVMCLiveLinkRemapper::MakeConfig) and never edited. A VMC source
 * makes one with each static data push and runs it over that static data and the frames after it;
 * for other sources' subjects, Live Link runs the one CreateWorker makes. Per-static-data state
 * (which curves to synthesize, which bones get a rest translation) is computed in RemapStaticData
 * and used by RemapFrameData.
 */
class FVMCLiveLinkRemapperWorker final : public ILiveLinkSubjectRemapperWorker
{
public:
	/** A worker for this configuration, which it never changes. */
	explicit FVMCLiveLinkRemapperWorker(FVMCRemapConfig InConfig) : Config(MoveTemp(InConfig)) {}

	/** The configuration it remaps with. */
	const FVMCRemapConfig& GetConfig() const { return Config; }

	/** Renames bones and curves, appends the normalizer's curves, and resolves what RemapFrameData
	 *  needs by index. May run off the game thread. */
	virtual void RemapStaticData(FLiveLinkStaticDataStruct& InOutStaticData) override;
	/** Gives rotation-only bones their rest translations (cm, parent space) and fills the added
	 *  curves, by index. May run off the game thread. */
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

/** Reads an asset's editor metadata tags (see UVMCLiveLinkRemapper::ReadAssetMetadata). */
using FVMCReadAssetMetadata = TFunction<TMap<FName, FString>(UObject*)>;

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
	/** The Animation role. */
	virtual TSubclassOf<ULiveLinkRole> GetSupportedRole() const override { return ULiveLinkAnimationRole::StaticClass(); }
	/** Always valid: empty maps pass every name through. */
	virtual bool IsValidRemapper() const override { return true; }
	/** The worker made by the last CreateWorker. */
	virtual FWorkerSharedPtr GetWorker() const override { return Worker; }
	/** Live Link's worker: it passes everything through. A VMC source applies MakeConfig in the
	 *  static data and frames it pushes, since Live Link evaluates with the source's static data
	 *  (see CreateWorker). Live Link calls it again whenever the remapper is dirty. */
	virtual FWorkerSharedPtr CreateWorker() override;

	/** Everything this remapper does, for a worker to apply: the maps, the normalizer and the
	 *  reference skeleton's rest translations. Game thread (it may load the reference skeleton). */
	FVMCRemapConfig MakeConfig() const;

	/** Goes up with every change, so a source can republish its static data through the new worker. */
	uint32 GetRevision() const { return Revision; }

#if WITH_EDITOR
	/** Any edit reaches the subject: the VMC source republishes with the new settings (and Live
	 *  Link builds a new worker). */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Evt) override
	{
		Super::PostEditChangeProperty(Evt);
		MarkDirty();
	}
#endif

	// Utilities
	/** Makes the VMC source republish the static data with the current settings (and Live Link
	 *  build a new worker). */
	UFUNCTION(BlueprintCallable, Category = "LiveLink|Remapper")
	void ForceRefreshStaticData() { MarkDirty(); }

	/** Set by the VMC source that publishes this remapper's subject, when it takes MakeConfig to apply
	 *  itself; CreateWorker then gives Live Link a worker that passes everything through, so nothing
	 *  is applied twice. Not set for other sources' subjects, which get the full worker. Game thread. */
	void SetAppliedBySource(bool bApplied);

	/** Lists every name the subject is receiving in the maps (mapped to itself), then applies the
	 *  preset that fits them (Seed From Subject). Game thread. */
	UFUNCTION(BlueprintCallable, Category = "LiveLink|Remapper")
	void DetectAndSeedFromSubject();

	/**
	 * Adds a preset's entries to the maps (Apply Preset). Entries earlier presets added and the user
	 * hasn't edited are removed first; edited ones are kept. With a reference skeleton and incoming
	 * names, the body bones are also mapped to UE mannequin-style names found on the mesh. Game thread.
	 */
	UFUNCTION(BlueprintCallable, Category = "LiveLink|Remapper")
	void ApplyPreset(ELLRemapPreset InPreset);

	/** Merges {"Curves": {incoming: target, ...}, "Bones": {...}} into the curve and bone maps
	 *  (either object may be absent). Game thread. */
	UFUNCTION(BlueprintCallable, Category = "LiveLink|Remapper")
	void LoadCustomCurveMapFromJSON(const FString& JsonText);

	/** A saved mapping to apply with Apply Mapping Asset, or to save the maps into. */
	UPROPERTY(EditAnywhere, Category = "Mapping")
	TSoftObjectPtr<UVMCLiveLinkMappingAsset> MappingAsset;

	/** While both maps are empty, pick the mapping asset that matches the reference skeleton. */
	UPROPERTY(EditAnywhere, Category = "Mapping")
	bool bAutoDetectMappingFromReference = true;

	/** Saving into the mapping asset also records the reference skeleton's signature, so the asset is found for it later. */
	UPROPERTY(EditAnywhere, Category = "Mapping", meta=(DisplayName="Capture Signature On Save"))
	bool bCaptureSignatureOnSave = true;

	/** Replaces the maps with the asset's; with bAlsoCaptureSignature, also records the reference
	 *  skeleton's signature in the asset. Game thread. */
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

	/**
	 * The humanoid-map metadata convention shared with VRM importers (decision D-4; no plugin
	 * dependency): a skeletal mesh may carry editor metadata "VRM.Humanoid.<UnityBoneName>" = its
	 * bone. Returns that as a bone map (Unity bone name, which VMC streams, to the mesh's bone).
	 * Other keys, and "VRM.HumanoidVersion", are ignored.
	 */
	static TMap<FName, FName> MakeBoneMapFromHumanoidMetadata(const TMap<FName, FString>& Tags);

	/**
	 * Replaces the bone map with the reference skeleton's humanoid metadata (see above), keeping only
	 * bones the skeleton has. False, leaving the map alone, when there is no reference skeleton, when
	 * metadata can't be read (outside the editor: ReadAssetMetadata is unset and cooked builds have no
	 * metadata), or when the mesh's metadata gives no bone the skeleton has.
	 */
	UFUNCTION(BlueprintCallable, Category="LiveLink|Remapper")
	bool MapBonesFromHumanoidMetadata();

	/** Reads an asset's editor metadata. The editor module sets it (this runtime module can't read
	 *  editor metadata itself); unset outside the editor. */
	static FVMCReadAssetMetadata ReadAssetMetadata;

	/** Saves the current maps into an asset (and the reference skeleton's signature, if asked). */
	UFUNCTION(BlueprintCallable, Category="LiveLink|Remapper")
	void SaveCurrentMappingTo(UVMCLiveLinkMappingAsset* Asset, bool bCaptureSignatureFromReference);

	/** The subject this remapper renames (set by Live Link through Initialize). */
	const FLiveLinkSubjectKey& GetSubjectKey() const { return CachedKey; }

	/**
	 * The names the subject receives, before renaming, from the VMC source that publishes it. False
	 * if no VMC source publishes the subject, or it hasn't sent anything yet. (The static data Live
	 * Link holds for a subject may already be renamed by this remapper, so it isn't used.)
	 */
	bool GetIncomingNames(TArray<FName>& OutBones, TArray<FName>& OutCurves) const;

	/**
	 * What the current settings make of these incoming names, one row per name, as the worker
	 * would remap them (curves the normalizer adds come last). With Reference, each outgoing name
	 * is checked against its bones (bones) and morph targets (curves).
	 */
	void BuildMappingTable(TConstArrayView<FName> Bones, TConstArrayView<FName> Curves, const USkeletalMesh* Reference,
		TArray<FVMCMappingRow>& OutBones, TArray<FVMCMappingRow>& OutCurves) const;

	/** The reference skeleton, or the project default. May load it. */
	USkeletalMesh* ResolveReferenceSkeleton() const;

public:
	// NOTE: BoneNameMap is declared on the base (ULiveLinkSubjectRemapper). Don't redeclare it here.

	/** Incoming curve (blend shape) name to the name the mesh or AnimBlueprint uses. Curves without an entry pass through unchanged. */
	UPROPERTY(EditAnywhere, Category = "Mapping")
	TMap<FName, FName> CurveNameMap;

	/** The target skeleton. Its rest pose gives bones the stream sends without a translation
	 *  their length. Falls back to the project's Default Reference Skeleton (VMC Live Link settings). */
	UPROPERTY(EditAnywhere, Category = "Target", meta = (DisplayThumbnail = "false"))
	TSoftObjectPtr<USkeletalMesh> ReferenceSkeleton;

	/** Give bones the stream sends without a translation the reference skeleton's rest translation
	 *  (VMC senders give most bones rotation only). Root and Hips always use the stream. */
	UPROPERTY(EditAnywhere, Category = "Target")
	bool bUseReferenceTranslations = true;

	/** The naming scheme Apply Preset seeds the maps with. */
	UPROPERTY(EditAnywhere, Category = "Mapping")
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
	void MarkDirty();   // a new revision (the VMC source republishes) and a new Live Link worker

	void SeedFromReferenceSkeleton();

	/** MapBonesFromHumanoidMetadata for a given mesh. */
	bool ApplyHumanoidMetadata(USkeletalMesh* Ref);

	/** The subject's incoming names for seeding the maps: the VMC source's (GetIncomingNames), or else
	 *  the static data Live Link holds, for subjects from other sources. */
	bool GetSubjectNamesForSeeding(TArray<FName>& OutBones, TArray<FName>& OutCurves) const;

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
	bool bAppliedBySource = false; // see SetAppliedBySource
	TSharedPtr<FVMCLiveLinkRemapperWorker> Worker;
};
