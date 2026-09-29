// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/SkeletalMesh.h"
#include "VMCLiveLinkMappingAsset.generated.h"

/**
 * Reusable VMC bone and curve name maps for a target skeleton. A remapper can pick the asset whose
 * skeleton signature matches its reference skeleton (UVMCLiveLinkRemapper::StartAutoDetectMapping).
 */
UCLASS(BlueprintType)
class VMCLIVELINK_API UVMCLiveLinkMappingAsset : public UDataAsset
{
	GENERATED_BODY()
public:
	/** Version of ComputeSignature. Signatures saved with an older version are recomputed. */
	static constexpr int32 CurrentSignatureVersion = 2;

	/** Incoming (VMC) bone name to the target skeleton's bone name. */
	UPROPERTY(EditAnywhere, Category="Mapping")
	TMap<FName, FName> BoneNameMap;

	/** Incoming curve (blend shape) name to the target's curve or morph target name. */
	UPROPERTY(EditAnywhere, Category="Mapping")
	TMap<FName, FName> CurveNameMap;

	// Hint meshes that this mapping applies to (optional; used for auto-detect)
	UPROPERTY(EditAnywhere, Category="Detection")
	TArray<TSoftObjectPtr<USkeletalMesh>> ExampleReferenceMeshes;

	// Signatures of skeletons this mapping applies to (auto-detect)
	UPROPERTY(EditAnywhere, Category="Detection")
	TArray<uint32> SkeletonSignatures;

	/** SkeletonSignatures as ";"-separated hex, for finding matching assets in the asset registry without loading them. */
	UPROPERTY(VisibleAnywhere, AssetRegistrySearchable, Category="Detection")
	FString SignatureTag;

	/** The ComputeSignature version SkeletonSignatures were computed with (CurrentSignatureVersion when up to date). */
	UPROPERTY(VisibleAnywhere, AssetRegistrySearchable, Category="Detection")
	int32 SignatureVersion = 0;

	/** Computes a signature from a mesh and adds it to this asset. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category="Detection")
	void CaptureSignatureFrom(USkeletalMesh* Mesh);

	/** True if this asset lists the mesh, or a skeleton with the same bone names. */
	UFUNCTION(BlueprintCallable, Category="Detection")
	bool MatchesMesh(USkeletalMesh* Mesh);

	/**
	 * A signature of a mesh's bone names that doesn't depend on case, '_' or '-', or bone order:
	 * CRC32 of the sorted normalized names. Stable across engine versions and platforms.
	 */
	static uint32 ComputeSignature(const USkeletalMesh* Mesh);
	/** The same from a list of bone names. */
	static uint32 ComputeSignature(TConstArrayView<FName> BoneNames);

	/** The tag text a signature appears as in SignatureTag. */
	static FString SignatureToTag(uint32 Signature);

	/** Recomputes signatures saved by an older version from ExampleReferenceMeshes (loads them). */
	void UpdateSignaturesIfOld();

	/** Refreshes SignatureTag when the signatures are current. Old ones aren't recomputed here (that
	 *  would load other assets); MatchesMesh or an edit does it, through UpdateSignaturesIfOld. */
	virtual void PostLoad() override;
#if WITH_EDITOR
	/** Recomputes old signatures and refreshes SignatureTag after an edit. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif

private:
	void RefreshSignatureTag();
};
