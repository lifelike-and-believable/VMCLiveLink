// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "ILiveLinkSource.h"
#include "Templates/UniquePtr.h"
#include "UObject/StrongObjectPtr.h"
#include "VMCConnectionSettings.h"

// Forward declarations (keep OSC headers out of Public/)
class UOSCServer;
struct FOSCMessage;
// forward declare to avoid pulling headers into the .h

class ULiveLinkSubjectRemapper;
class ULiveLinkSubjectSettings;
class FVMCFrameAssembler;
class ULiveLinkSourceSettings;
struct FPropertyChangedEvent;

/**
 * VMC → Live Link source (UE 5.6).
 *
 * Threading: everything runs on the game thread. UOSCServer queues packets on its socket thread and
 * dispatches them on the game thread (UOSCServer::PumpPacketQueue), and ReceiveClient, shutdown and
 * subject bootstrap are game-thread calls too. So there is no locking. (D-1: a receive-thread path
 * is P3.1 step 2.) Message parsing is VMCProtocol, frame building is FVMCFrameAssembler.
 */
class VMCLIVELINK_API FVMCLiveLinkSource
    : public ILiveLinkSource
    , public TSharedFromThis<FVMCLiveLinkSource>
{
public:
    explicit FVMCLiveLinkSource(const FVMCConnectionSettings& InSettings, const FString& InSourceName = TEXT("VMC"));

    UE_DEPRECATED(5.6, "Use the FVMCConnectionSettings constructor.")
    FVMCLiveLinkSource(const FString& InSourceName);
    UE_DEPRECATED(5.6, "Use the FVMCConnectionSettings constructor.")
    FVMCLiveLinkSource(const FString& InSourceName, int32 InPort);
    UE_DEPRECATED(5.6, "Use the FVMCConnectionSettings constructor.")
    FVMCLiveLinkSource(const FString& InSourceName, int32 InPort, bool bInUnityToUE, bool bInMetersToCm, float InYawDeg);
    UE_DEPRECATED(5.6, "Use the FVMCConnectionSettings constructor.")
    FVMCLiveLinkSource(const FString& InSourceName, int32 InPort, bool bInUnityToUE, bool bInMetersToCm, float InYawDeg, FString Subject);
    virtual ~FVMCLiveLinkSource();

    const FVMCConnectionSettings& GetConnectionSettings() const { return Settings; }

    // ILiveLinkSource
    virtual void ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid) override;
    virtual bool IsSourceStillValid() const override { return bIsValid; }
    virtual bool RequestSourceShutdown() override;

    virtual FText GetSourceType() const override { return NSLOCTEXT("VMCLiveLink", "SourceType", "VMC (OSC)"); }
    virtual FText GetSourceMachineName() const override { return FText::FromString(TEXT("Local/Network")); }
    virtual FText GetSourceStatus() const override;

    // Settings shown in the Live Link panel (UVMCLiveLinkSourceSettings)
    virtual TSubclassOf<ULiveLinkSourceSettings> GetSettingsClass() const override;
    virtual void InitializeSettings(ULiveLinkSourceSettings* InSettings) override;
    virtual void OnSettingsChanged(ULiveLinkSourceSettings* InSettings, const FPropertyChangedEvent& PropertyChangedEvent) override;

private:
    // OSC lifecycle
    bool StartOSC();
    void StopOSC();
    void OnOscMessageReceived(const FOSCMessage& Message, const FString& IPAddress, uint16 Port);

    // Live Link pushes
    void PushStaticData();  // bones + property names
    void PushFrame();       // bone transforms + property values

    // Cached copies of the asset’s maps (so we don’t re-hash every frame)
    TMap<FName, FName> CachedBoneMap;
    TMap<FName, FName> CachedCurveMap;
    uint32 CachedMapsHash = 0;

    // Cached local ref-pose offsets from the remapper’s ReferenceSkeleton
    TMap<FName, FVector> RefLocalTranslationByName;
    bool bHaveRefOffsets = false;

    // One-shot flag to force static re-publish next Apply when maps change
    bool bForceStaticNext = false;

    // Helpers
    void RefreshStaticMapsFromSettings(); // (we’ll extend this to also pull the ReferenceSkeleton)
    static uint32 HashMaps(const TMap<FName, FName>& A, const TMap<FName, FName>& B);
    void BuildRefOffsetsFromMesh(class USkeletalMesh* Mesh);

private:
    // Identity / config
    FString SourceName;
    FVMCConnectionSettings Settings;
    bool bListening = false; // the OSC server is bound and listening

    // Live Link client
    ILiveLinkClient* Client = nullptr;
    FGuid  SourceGuid;
    bool   bIsValid = false;

    // OSC server (UObject) – strong ref so it isn't GC'd
    TStrongObjectPtr<UOSCServer> OscServer;

    // Static (skeleton) tracking
    bool bStaticSent = false;
    bool bStaticDirty = false; // a new bone or curve arrived; publish static data before the next frame

    // Skeleton, pose and curves
    TUniquePtr<FVMCFrameAssembler> Assembler;
    void InitSkeleton();

    // One-time warnings for non-conformant or unsupported input
    bool bWarnedLegacyRoot = false;
    bool bWarnedRootScaleOffset = false;
    bool bWarnedMalformed = false;
   
    // Track which remapper is currently bound (from subject settings)
    TWeakObjectPtr<ULiveLinkSubjectRemapper> LastSeenRemapper;
    TWeakObjectPtr<USkeletalMesh> LastRefMeshBuiltFrom; // NEW

    private:
        void EnsureSubjectSettingsWithDefaults(); // create settings + attach default remapper/skeleton
        bool bEnsuredDefaults = false;            // NEW: track we've done it once

};
