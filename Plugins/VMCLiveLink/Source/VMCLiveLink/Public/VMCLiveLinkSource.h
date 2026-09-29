// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "ILiveLinkSource.h"
#include "LiveLinkTypes.h"
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
class FVMCSenderFilter;
struct FVMCDevicePose;
class FVMCUdpReceiver;
class FVMCMessageStats;
class FOutputDevice;
class FInternetAddr;
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
 * Everything that touches UObjects (subject bootstrap, settings) stays on the game thread, which
 * publishes what frame building needs as an immutable snapshot. The source publishes VMC's own
 * names; the subject's remapper (UVMCLiveLinkRemapper) renames them and adds rest translations. Switching paths, port, bind address or subject stops the old path before starting the
 * new one, so the two never run at once.
 */
class VMCLIVELINK_API FVMCLiveLinkSource
    : public ILiveLinkSource
    , public TSharedFromThis<FVMCLiveLinkSource>
{
public:
    /** A source with these settings. Nothing is received until Live Link calls ReceiveClient. Game thread. */
    explicit FVMCLiveLinkSource(const FVMCConnectionSettings& InSettings, const FString& InSourceName = TEXT("VMC"));

    UE_DEPRECATED(5.6, "Use the FVMCConnectionSettings constructor.")
    FVMCLiveLinkSource(const FString& InSourceName);
    UE_DEPRECATED(5.6, "Use the FVMCConnectionSettings constructor.")
    FVMCLiveLinkSource(const FString& InSourceName, int32 InPort);
    UE_DEPRECATED(5.6, "Use the FVMCConnectionSettings constructor.")
    FVMCLiveLinkSource(const FString& InSourceName, int32 InPort, bool bInUnityToUE, bool bInMetersToCm, float InYawDeg);
    UE_DEPRECATED(5.6, "Use the FVMCConnectionSettings constructor.")
    FVMCLiveLinkSource(const FString& InSourceName, int32 InPort, bool bInUnityToUE, bool bInMetersToCm, float InYawDeg, FString Subject);
    /** Removes the receive callbacks and the ticker, even if Live Link never called RequestSourceShutdown. */
    virtual ~FVMCLiveLinkSource();

    /** The settings in use. Game thread. */
    const FVMCConnectionSettings& GetConnectionSettings() const { return Settings; }

    // ILiveLinkSource. Live Link calls these on the game thread.

    /** Starts receiving on the path Settings.bReceiveThread asks for. The source stays valid if the
     *  port can't be opened, so its status can say why. */
    virtual void ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid) override;
    /** False once the source has been shut down. */
    virtual bool IsSourceStillValid() const override { return bIsValid; }
    /** Stops receiving (returns once no frame is being built) and invalidates the source. Always true. */
    virtual bool RequestSourceShutdown() override;

    /** "VMC (OSC)". */
    virtual FText GetSourceType() const override { return NSLOCTEXT("VMCLiveLink", "SourceType", "VMC (OSC)"); }
    /** The IP address the last packet came from. */
    virtual FText GetSourceMachineName() const override;
    /** Listening, receiving (frame rate, jitter, sender), no data, or why it can't listen
     *  (VMCDiagnostics::FormatStatus). */
    virtual FText GetSourceStatus() const override;

    // Settings shown in the Live Link panel (UVMCLiveLinkSourceSettings)

    /** UVMCLiveLinkSourceSettings. */
    virtual TSubclassOf<ULiveLinkSourceSettings> GetSettingsClass() const override;
    /** Shows the source's settings in the panel. The source was created from its connection
     *  string (also when a Live Link preset is applied), so its settings are the truth. */
    virtual void InitializeSettings(ULiveLinkSourceSettings* InSettings) override;
    /** Applies an edit: a new port, bind address, path, subject, sender rule or device/camera setting
     *  restarts receiving; other settings apply to the next frame. Invalid values are logged and the panel goes back to the settings in use. */
    virtual void OnSettingsChanged(ULiveLinkSourceSettings* InSettings, const FPropertyChangedEvent& PropertyChangedEvent) override;

    /** VMC.Stats for this source: message rates per address since the last call, and the addresses
     *  the plugin doesn't use. Game thread. */
    FString GetStatsReport();

    /** VMC.Stats: the report of every VMC source, to Ar. Game thread. */
    static void ReportAllStats(FOutputDevice& Ar);

    /**
     * The bone and curve names the VMC source publishing Key last sent as static data: VMC's own
     * names, before any remapper renames them (P6.2's mapping table). False if no VMC source
     * publishes Key, or it hasn't sent static data yet. Game thread.
     */
    static bool GetPublishedNames(const FLiveLinkSubjectKey& Key, TArray<FName>& OutBones, TArray<FName>& OutCurves);

    /** Whether a VMC source publishes Key (whether or not it has sent anything yet). Game thread. */
    static bool PublishesSubject(const FLiveLinkSubjectKey& Key);

#if WITH_DEV_AUTOMATION_TESTS
    /** Tests: sets what GetPublishedNames reports, as if static data with these names had been pushed. */
    void SetPublishedNamesForTest(const TArray<FName>& Bones, const TArray<FName>& Curves);
#endif

private:
    /** Everything frame building reads that the game thread owns. Immutable once published. */
    struct FSnapshot
    {
        FVMCConnectionSettings Settings;
        uint32 Version = 0; // a new version republishes static data
    };

    // ---- Game thread ----
    bool StartReceiving();   // the path Settings.bReceiveThread asks for
    void StopReceiving();    // stops either path; returns once no frame is being built
    bool StartOSC();
    void StopOSC();
    void OnOscMessageReceived(const FOSCMessage& Message, const FString& IPAddress, uint16 Port);
    bool Tick(float DeltaTime); // subject bootstrap for the receive thread
    void PublishSnapshot();
    void EnsureSubjectSettingsWithDefaults(); // create settings + attach the default remapper

    // ---- Frame-building thread (receive thread, or game thread) ----
    void OnPacket(TConstArrayView<uint8> Packet, double ArrivalSeconds, const FInternetAddr& Sender);
    bool AcceptSender(const FString& Sender); // the allowlist and sender lock (Settings)
    void NoteSender(const FString& Sender);   // the status's sender, when it changes
    void ProcessMessage(VMCProtocol::EAddress Kind, TConstArrayView<VMCProtocol::FArg> Args, double ArrivalSeconds, const FSnapshot& Snap);
    void PushStaticData(const FSnapshot& Snap);
    void PushFrame(const FSnapshot& Snap, double ArrivalSeconds);
    void PushDevice(const FVMCDevicePose& Device, double ArrivalSeconds, const FSnapshot& Snap); // if its setting is on
    void RemoveDeviceSubjects(); // game thread, while nothing is received

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
    uint32 SnapshotVersion = 0;
    TWeakObjectPtr<ULiveLinkSubjectRemapper> LastRemapper; // to republish static data when it changes
    uint32 LastRemapperRevision = 0;

    mutable FCriticalSection SnapshotLock;
    TSharedPtr<const FSnapshot> Snapshot;

    // Frame building (one thread at a time)
    TUniquePtr<FVMCFrameAssembler> Assembler;
    TUniquePtr<FVMCSenderFilter> SenderFilter; // configured by the game thread while nothing is received
    TMap<FName, FName> DeviceSubjects; // device serial -> its published subject; cleared while nothing is received
    TMap<FName, FName> CameraSubjects; // camera name -> its published subject (kept apart: a name can match a serial)
    void InitSkeleton();
    bool bStaticDirty = false;           // a new bone or curve arrived
    uint32 PublishedStaticVersion = 0;   // snapshot version of the last static publish
    bool bWarnedLegacyRoot = false;
    bool bWarnedRootScaleOffset = false;
    bool bWarnedMalformed = false;
    uint32 LastSenderHash = 0;          // the receive thread's last sender (FInternetAddr hash)
    FString LastSenderSeen;             // and its IP; the OSC path compares this directly
    TUniquePtr<FVMCMessageStats> MessageStats; // counted while frames are built, reported by VMC.Stats

    // Shared between the threads
    std::atomic<bool> bStaticSent { false };
    std::atomic<bool> bEnsuredDefaults { false };
    std::atomic<bool> bBootstrapRequested { false };

    // Frame timing, for the status (written by frame building, read by the game thread)
    mutable FCriticalSection StatsLock;
    double LastFrameSeconds = 0.0;
    double MeanFrameInterval = 0.0;   // seconds, moving average
    double MeanIntervalDeviation = 0.0; // seconds, moving average of |interval - mean|: the jitter
    FString SenderStateText;          // what /VMC/Ext/OK says, when worth showing (VMCProtocol::DescribeSenderState)
    FString LockedSender;             // the sender locked to (bLockToFirstSender), or empty
    FString LastSender;               // the IP of the last packet used, or empty
    TArray<FName> PublishedBones;     // the names in the last static data pushed
    TArray<FName> PublishedCurves;
    int32 IgnoredSenders = 0;         // senders whose packets were ignored since receiving started
    int32 NumDeviceSubjects = 0;      // device and camera subjects published
};
