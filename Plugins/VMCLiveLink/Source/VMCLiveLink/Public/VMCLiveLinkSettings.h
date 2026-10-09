// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once
#include "Engine/DeveloperSettings.h"
#include "VMCLiveLinkSettings.generated.h"

class ULiveLinkSubjectRemapper;
class UVMCLiveLinkRemapper;
class USkeletalMesh;

/** Project Settings > Plugins > VMC Live Link: defaults for new VMC subjects. Game thread. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "VMC Live Link"))
class UVMCLiveLinkSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    /** Sets DefaultRemapperClass to UVMCLiveLinkRemapper. */
    UVMCLiveLinkSettings();

    /** The remapper class a new VMC subject gets (a class, not an asset). Default: UVMCLiveLinkRemapper. */
    UPROPERTY(EditAnywhere, Config, Category = "Defaults", meta = (AllowAbstract = "false"))
    TSoftClassPtr<ULiveLinkSubjectRemapper> DefaultRemapperClass;

    /** Reference skeleton used by remappers that have none of their own: for the Live Mapping table,
     *  rest translations and Map Bones From Humanoid Metadata. */
    UPROPERTY(EditAnywhere, Config, Category = "Defaults")
    TSoftObjectPtr<USkeletalMesh> DefaultReferenceSkeleton;

    /** Turns Live Link's smoothing delay off, for every Live Link source in the project (it sets the
     *  console variable LiveLink.TimedDataInput.NumFramesForSmoothOffset to 0). Live Link then reads
     *  each subject exactly its source's Engine Time Offset (plus its clock offset) behind, instead of
     *  a delay that follows the frames' recent spacing: with a sender that sends unevenly (a busy
     *  machine), the avatar no longer lurches or steps back. Raise the VMC source's Engine Time
     *  Offset to 0.05 to 0.075 s with it, and that of any other source that relied on the smoothing.
     *  A value of the variable set in an ini file or the console takes precedence. */
    UPROPERTY(EditAnywhere, Config, Category = "Timing", meta = (DisplayName = "Fixed Live Link Delay (All Sources)"))
    bool bFixedLiveLinkDelay = false;

    /** Sets Live Link's smoothing console variable as bFixedLiveLinkDelay says: 0 when on (unless a
     *  higher-priority value holds it); when off, removes that value, so the variable goes back to
     *  what set it before. Called when the module starts and when the setting changes. */
    void ApplyLiveLinkSmoothing() const;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif
};
