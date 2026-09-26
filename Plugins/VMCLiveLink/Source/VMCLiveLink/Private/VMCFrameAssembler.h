// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "LiveLinkTypes.h"

/**
 * Collects VMC messages into Live Link data, independent of the source's networking so it can be
 * unit tested (P3.1).
 *
 * The skeleton is "root" at index 0, then the Unity humanoid bones (VMCHumanoid), then any other
 * bone the sender streams, appended in arrival order. Indices never change once assigned, so the
 * pose and curves are kept in arrays indexed like the published static data.
 *
 * Not thread safe: one thread feeds it and builds from it.
 */
class FVMCFrameAssembler
{
public:
	/** How the frame's bone translations are chosen. */
	struct FFrameOptions
	{
		/** Use each bone's streamed translation (senders that send proper local translations).
		 *  Otherwise only Hips uses the stream, and the others use the reference offsets. */
		bool bPreferIncomingTranslations = false;
		/** Use the reference skeleton's local translations for bones without one of their own. */
		bool bUseRefOffsets = true;
		/** Reference local translation by *published* (remapped) bone name. May be null. */
		const TMap<FName, FVector>* RefOffsets = nullptr;
		/** Source bone name to published name. May be null. */
		const TMap<FName, FName>* BoneMap = nullptr;
	};

	FVMCFrameAssembler();

	/** A /VMC/Ext/Bone/Pos transform (already in UE space). Returns true if the bone is new, in
	 *  which case the static data must be published again. */
	bool SetBone(FName Bone, const FTransform& Local);

	/** A /VMC/Ext/Root/Pos transform (already in UE space). */
	void SetRoot(const FTransform& Root);

	/** A /VMC/Ext/Blend/Val. Returns true if the curve is new (static data must be published again). */
	bool SetCurve(FName Curve, float Value);

	/** After a frame is published: curves not sent before the next Apply read 0 when
	 *  bZeroMissingCurves, and otherwise hold their last value. Bones always hold theirs. */
	void EndFrame(bool bZeroMissingCurves);

	/** Static data with names mapped through the maps (either may be null). */
	FLiveLinkStaticDataStruct MakeStaticData(const TMap<FName, FName>* BoneMap, const TMap<FName, FName>* CurveMap) const;

	/** The current pose and curve values as a Live Link animation frame. */
	FLiveLinkFrameDataStruct MakeFrameData(const FFrameOptions& Options) const;

	TConstArrayView<FName> GetBoneNames() const { return BoneNames; }
	TConstArrayView<int32> GetBoneParents() const { return BoneParents; }
	TConstArrayView<FName> GetCurveNames() const { return CurveNames; }

private:
	TArray<FName> BoneNames;
	TArray<int32> BoneParents;
	TMap<FName, int32> BoneIndexByName;

	// Latest streamed transform per bone, and whether one has arrived.
	TArray<FTransform> Pose;
	TBitArray<> PoseReceived;
	FTransform Root = FTransform::Identity;

	TArray<FName> CurveNames;
	TMap<FName, int32> CurveIndexByName;
	TArray<float> CurveValues;
};
