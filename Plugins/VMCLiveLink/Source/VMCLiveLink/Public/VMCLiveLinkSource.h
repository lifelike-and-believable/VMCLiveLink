// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "ILiveLinkSource.h"
#include "Templates/UniquePtr.h"
#include "UObject/StrongObjectPtr.h"
#include "VMCConnectionSettings.h"

#include "Containers/Ticker.h"
#include <atomic>

// Forward declarations (keep OSC headers out of Public/)
class UOSCServer;
struct FOSCMessage;

class ULiveLinkSubjectRemapper;
class ULiveLinkSubjectSettings;
class FVMCFrameAssembler;
class FVMCUdpReceiver;
class ULiveLinkSourceSettings;
struct FPropertyChangedEvent;
namespace VMCProtocol { struct FArg; enum class EAddress : uint8; }

/**
 * VMC → Live Link source (UE 5.6).
 *
 * Threading (D-1). Frames are built on one thread at a time:
 *  - Receive Thread on (default): FVMCUdpReceiver reads the socket on its own thread; packets are
 *    parsed there (VMCOscParser) and frames pushed from there, timestamped when the packet arrived.
 *  - Off: the OSC plugin's UOSCServer dispatches messages on the game thread, as before.
 * Everything that touches UObjects (subject bootstrap, the remapper's maps and reference skeleton,
 * settings) stays on the game thread, which publishes what frame building needs as an immutable
 * snapshot. Switching paths, port, bind address or subject stops the old path before starting the
 * new one, so the two never run at once.
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
    /** Everything frame building reads that the game thread owns. Immutable once published. */
    struct FSnapshot
    {
        FVMCConnectionSettings Settings;
        TMap<FName, FName> BoneMap;
        TMap<FName, FName> CurveMap;
        TMap<FName, FVector> RefOffsets; // by published (remapped) bone name
        uint32 Version = 0;              // a new version republishes static data (names may have changed)
    };

    // ---- Game thread ----
    bool StartReceiving();   // the path Settings.bReceiveThread asks for
    void StopReceiving();    // stops either path; returns once no frame is being built
    bool StartOSC();
    void StopOSC();
    void OnOscMessageReceived(const FOSCMessage& Message, const FString& IPAddress, uint16 Port);
    bool Tick(float DeltaTime); // subject bootstrap and map refresh for the receive thread
    void PublishSnapshot();
    void RefreshStaticMapsFromSettings();
    void EnsureSubjectSettingsWithDefaults(); // create settings + attach default remapper/skeleton
    static uint32 HashMaps(const TMap<FName, FName>& A, const TMap<FName, FName>& B);
    void BuildRefOffsetsFromMesh(class USkeletalMesh* Mesh);

    // ---- Frame-building thread (receive thread, or game thread) ----
    void OnPacket(TConstArrayView<uint8> Packet, double ArrivalSeconds);
    void ProcessMessage(VMCProtocol::EAddress Kind, TConstArrayView<VMCProtocol::FArg> Args, double ArrivalSeconds, const FSnapshot& Snap);
    void PushStaticData(const FSnapshot& Snap);
    void PushFrame(const FSnapshot& Snap, double ArrivalSeconds);

    // ---- Any thread ----
    TSharedPtr<const FSnapshot> GetSnapshot() const;

private:
    // Identity / config (game thread)
    FString SourceName;
    FVMCConnectionSettings Settings;
    bool bListening = false;

    // Live Link client. Set before a receive path starts and cleared after it stops.
    ILiveLinkClient* Client = nullptr;
    FGuid  SourceGuid;
    bool   bIsValid = false;

    // Receive paths (game thread starts and stops them)
    TUniquePtr<FVMCUdpReceiver> Receiver;
    TStrongObjectPtr<UOSCServer> OscServer;
    FTSTicker::FDelegateHandle TickerHandle;
    double LastRefreshSeconds = 0.0;

    // Game thread's working copies, published through the snapshot
    TMap<FName, FName> CachedBoneMap;
    TMap<FName, FName> CachedCurveMap;
    uint32 CachedMapsHash = 0;
    TMap<FName, FVector> RefLocalTranslationByName;
    bool bHaveRefOffsets = false;
    TWeakObjectPtr<ULiveLinkSubjectRemapper> LastSeenRemapper;
    TWeakObjectPtr<USkeletalMesh> LastRefMeshBuiltFrom;
    uint32 SnapshotVersion = 0;

    mutable FCriticalSection SnapshotLock;
    TSharedPtr<const FSnapshot> Snapshot;

    // Frame building (one thread at a time)
    TUniquePtr<FVMCFrameAssembler> Assembler;
    void InitSkeleton();
    bool bStaticDirty = false;           // a new bone or curve arrived
    uint32 PublishedStaticVersion = 0;   // snapshot version of the last static publish
    bool bWarnedLegacyRoot = false;
    bool bWarnedRootScaleOffset = false;
    bool bWarnedMalformed = false;

    // Shared between the threads
    std::atomic<bool> bStaticSent { false };
    std::atomic<bool> bEnsuredDefaults { false };
    std::atomic<bool> bBootstrapRequested { false };

    // Frame timing, for the status (written by frame building, read by the game thread)
    mutable FCriticalSection StatsLock;
    double LastFrameSeconds = 0.0;
    double MeanFrameInterval = 0.0;   // seconds, moving average
    double MeanIntervalDeviation = 0.0; // seconds, moving average of |interval - mean|: the jitter
};
