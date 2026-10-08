// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VMCLiveLinkSource.h"
#include "VMCLog.h"
#include "VMCHumanoid.h"
#include "VMCProtocol.h"
#include "VMCFrameAssembler.h"
#include "VMCOscParser.h"
#include "VMCSenderFilter.h"
#include "VMCSourceDiagnostics.h"
#include "VMCUdpReceiver.h"
#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"

// Live Link
#include "ILiveLinkClient.h"
#include "LiveLinkTypes.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "Roles/LiveLinkCameraRole.h"
#include "Roles/LiveLinkCameraTypes.h"
#include "Roles/LiveLinkTransformRole.h"
#include "Roles/LiveLinkTransformTypes.h"

#include "VMCLiveLinkSettings.h"
#include "VMCLiveLinkSourceSettings.h"
#include "LiveLinkSubjectSettings.h"
#include "LiveLinkSubjectRemapper.h"
#include "VMCLiveLinkRemapper.h"

// OSC (cpp-only)
#include "OSCServer.h"
#include "OSCMessage.h"
#include "OSCTypes.h"


// ---------------- Ctors ----------------

FVMCLiveLinkSource::FVMCLiveLinkSource(const FVMCConnectionSettings& InSettings, const FString& InSourceName)
    : SourceName(InSourceName), Settings(InSettings)
{
    InitSkeleton();
    PublishSnapshot();
}

namespace
{
    FVMCConnectionSettings MakeLegacySettings(int32 Port, bool bUnityToUE, bool bMetersToCm, float Yaw, FName Subject)
    {
        FVMCConnectionSettings Out;
        Out.Port = Port;
        Out.bUnityToUE = bUnityToUE;
        Out.bMetersToCm = bMetersToCm;
        Out.YawOffsetDeg = Yaw;
        Out.SubjectName = Subject;
        return Out;
    }

    // Frame timing averages: about the last 20 frames.
    constexpr double StatsSmoothing = 0.05;

    // Scene time rate for /VMC/Ext/T (the sender's clock carries no rate of its own).
    const FFrameRate SenderTimeRate(60, 1);

    // Every VMC source that exists, for VMC.Stats. Sources are created and destroyed on the game
    // thread, where the command runs, but the lock costs nothing and keeps that from mattering.
    FCriticalSection GVMCSourcesLock;
    TArray<FVMCLiveLinkSource*> GVMCSources;

    FAutoConsoleCommandWithOutputDevice GVMCStatsCommand(
        TEXT("VMC.Stats"),
        TEXT("Prints each VMC source's messages per second for each address since the last VMC.Stats, and the addresses it received that the plugin doesn't use."),
        FConsoleCommandWithOutputDeviceDelegate::CreateStatic(&FVMCLiveLinkSource::ReportAllStats));
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
FVMCLiveLinkSource::FVMCLiveLinkSource(const FString& InSourceName)
    : FVMCLiveLinkSource(FVMCConnectionSettings(), InSourceName)
{
}

FVMCLiveLinkSource::FVMCLiveLinkSource(const FString& InSourceName, int32 InPort)
    : FVMCLiveLinkSource(MakeLegacySettings(InPort, true, true, 0.f, TEXT("VMC_Subject")), InSourceName)
{
}

FVMCLiveLinkSource::FVMCLiveLinkSource(const FString& InSourceName, int32 InPort, bool bInUnityToUE, bool bInMetersToCm, float InYawDeg)
    : FVMCLiveLinkSource(MakeLegacySettings(InPort, bInUnityToUE, bInMetersToCm, InYawDeg, TEXT("VMC_Subject")), InSourceName)
{
}

FVMCLiveLinkSource::FVMCLiveLinkSource(const FString& InSourceName, int32 InPort, bool bInUnityToUE, bool bInMetersToCm, float InYawDeg, FString InSubject)
    : FVMCLiveLinkSource(MakeLegacySettings(InPort, bInUnityToUE, bInMetersToCm, InYawDeg, FName(*InSubject)), InSourceName)
{
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

void FVMCLiveLinkSource::InitSkeleton()
{
    Assembler = MakeUnique<FVMCFrameAssembler>();
    SenderFilter = MakeUnique<FVMCSenderFilter>();
    MessageStats = MakeUnique<FVMCMessageStats>();
    MessageStats->Reset(FPlatformTime::Seconds());

    FScopeLock Lock(&GVMCSourcesLock);
    GVMCSources.Add(this);
}

// ---------------- Lifecycle (game thread) ----------------

void FVMCLiveLinkSource::ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid)
{
    Client = InClient;
    SourceGuid = InSourceGuid;

    // Subject settings are bootstrapped on the first /VMC/Ext/Blend/Apply (before anything is
    // pushed), not here. Deferring lets a Live Link preset, which adds its sources before its
    // subjects, create the subject with its saved settings first; EnsureSubjectSettingsWithDefaults
    // then leaves those settings alone.
    PublishSnapshot();

    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FVMCLiveLinkSource::Tick));
    // The source stays valid when the port can't be opened, so its status says why ("Can't listen
    // on port") rather than "Stopped"; a new port in the settings starts receiving again.
    StartReceiving();
    bIsValid = true;

    UE_LOG(LogVMCLiveLink, Log, TEXT("VMC source '%s' on %s:%d (listening=%d, %s)"),
        *SourceName, *Settings.BindAddress, Settings.Port, bListening ? 1 : 0, *Settings.ToString());
}

FVMCLiveLinkSource::~FVMCLiveLinkSource()
{
    // Callbacks are bound to this (the OSC delegate with AddRaw, the receive thread and the ticker),
    // so they must be removed even if Live Link never called RequestSourceShutdown. The OSC server
    // is a UObject, so skip it if the UObject system is already gone (very late teardown).
    {
        FScopeLock Lock(&GVMCSourcesLock);
        GVMCSources.RemoveSingleSwap(this);
    }
    Receiver.Reset();
    if (UObjectInitialized())
    {
        StopOSC();
    }
    if (TickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    }
}

bool FVMCLiveLinkSource::RequestSourceShutdown()
{
    StopReceiving();
    if (TickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
        TickerHandle.Reset();
    }
    bIsValid = false;
    Client = nullptr;
    return true;
}

bool FVMCLiveLinkSource::StartReceiving()
{
    // No path is running, so the filter and assembler aren't in use. A restart forgets the locked
    // sender and what the last sender said in /VMC/Ext/OK, so a new sender's first OK is reported.
    SenderFilter->Configure(Settings.AllowedSenders, Settings.bLockToFirstSender);
    Assembler->ClearSenderState();
    {
        FScopeLock Lock(&StatsLock);
        LockedSender.Reset();
        IgnoredSenders = 0;
        SenderStateText.Reset();
    }
    MessageStats->Reset(FPlatformTime::Seconds());
    // The frame-building thread's sender caches, so the first packet on the new path notes its sender.
    LastSenderHash = 0;
    LastSenderSeen.Reset();

    if (!Settings.bReceiveThread)
    {
        return StartOSC();
    }
    FString Error;
    Receiver = FVMCUdpReceiver::Start(Settings.BindAddress, Settings.Port,
        [this](TConstArrayView<uint8> Packet, double ArrivalSeconds, const FInternetAddr& Sender) { OnPacket(Packet, ArrivalSeconds, Sender); },
        FString::Printf(TEXT("VMC receive %s:%d"), *Settings.BindAddress, Settings.Port), Error);
    bListening = Receiver.IsValid();
    if (!bListening)
    {
        UE_LOG(LogVMCLiveLink, Error, TEXT("VMC source '%s': %s"), *SourceName, *Error);
    }
    return bListening;
}

void FVMCLiveLinkSource::StopReceiving()
{
    Receiver.Reset(); // joins the receive thread
    StopOSC();
    bListening = false;
}

FText FVMCLiveLinkSource::GetSourceStatus() const
{
    FVMCSourceStatusInputs In;
    In.bValid = bIsValid;
    In.bListening = bListening;
    In.bReceivedFrame = bStaticSent;
    In.BindAddress = Settings.BindAddress;
    In.Port = Settings.Port;
    In.bReceiveThread = Settings.bReceiveThread;
    {
        // Copy under the lock (the receive thread takes it for every frame), format after.
        FScopeLock Lock(&StatsLock);
        In.SecondsSinceLastFrame = FPlatformTime::Seconds() - LastFrameSeconds;
        In.MeanFrameInterval = MeanFrameInterval;
        In.Jitter = MeanIntervalDeviation;
        In.SenderState = SenderStateText;
        In.Sender = LastSender;
        In.bLockedToSender = !LockedSender.IsEmpty();
        In.IgnoredSenders = IgnoredSenders;
        In.DeviceSubjects = NumDeviceSubjects;
    }
    return VMCDiagnostics::FormatStatus(In);
}

bool FVMCLiveLinkSource::PublishesSubject(const FLiveLinkSubjectKey& Key)
{
    FScopeLock RegistryLock(&GVMCSourcesLock);
    return GVMCSources.ContainsByPredicate([&Key](const FVMCLiveLinkSource* Source)
    {
        return Source->SourceGuid == Key.Source && Source->Settings.SubjectName == Key.SubjectName.Name;
    });
}

bool FVMCLiveLinkSource::PublishesSubjectOf(const ULiveLinkSubjectRemapper* Remapper)
{
    check(IsInGameThread());
    if (!Remapper)
    {
        return false;
    }
    FScopeLock RegistryLock(&GVMCSourcesLock);
    return GVMCSources.ContainsByPredicate([Remapper](const FVMCLiveLinkSource* Source)
    {
        const ULiveLinkSubjectSettings* SubjectSettings = Source->Client
            ? Cast<ULiveLinkSubjectSettings>(Source->Client->GetSubjectSettings({ Source->SourceGuid, Source->Settings.SubjectName }))
            : nullptr;
        return SubjectSettings && SubjectSettings->Remapper == Remapper;
    });
}

bool FVMCLiveLinkSource::GetPublishedNames(const FLiveLinkSubjectKey& Key, TArray<FName>& OutBones, TArray<FName>& OutCurves)
{
    FScopeLock RegistryLock(&GVMCSourcesLock);
    for (const FVMCLiveLinkSource* Source : GVMCSources)
    {
        if (Source->SourceGuid == Key.Source && Source->Settings.SubjectName == Key.SubjectName.Name)
        {
            FScopeLock Lock(&Source->StatsLock);
            if (Source->PublishedBones.Num() == 0 && Source->PublishedCurves.Num() == 0)
            {
                return false;
            }
            OutBones = Source->PublishedBones;
            OutCurves = Source->PublishedCurves;
            return true;
        }
    }
    return false;
}

#if WITH_DEV_AUTOMATION_TESTS
void FVMCLiveLinkSource::SetPublishedNamesForTest(const TArray<FName>& Bones, const TArray<FName>& Curves)
{
    FScopeLock Lock(&StatsLock);
    PublishedBones = Bones;
    PublishedCurves = Curves;
}

void FVMCLiveLinkSource::InjectPacketForTest(TConstArrayView<uint8> Packet)
{
    check(IsInGameThread());
    const TSharedRef<FInternetAddr> Sender = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
    bool bValid = false;
    Sender->SetIp(TEXT("127.0.0.1"), bValid);
    OnPacket(Packet, FPlatformTime::Seconds(), *Sender);
}
#endif

FText FVMCLiveLinkSource::GetSourceMachineName() const
{
    FString Sender;
    {
        FScopeLock Lock(&StatsLock);
        Sender = LastSender;
    }
    return Sender.IsEmpty() ? NSLOCTEXT("VMCLiveLink", "NoSender", "No sender yet") : FText::FromString(Sender);
}

FString FVMCLiveLinkSource::GetStatsReport()
{
    return FString::Printf(TEXT("VMC source '%s' (subject %s, %s:%d, %s):\n%s"), *SourceName, *Settings.SubjectName.ToString(),
        *Settings.BindAddress, Settings.Port, *GetSourceStatus().ToString(), *MessageStats->Report(FPlatformTime::Seconds()));
}

void FVMCLiveLinkSource::ReportAllStats(FOutputDevice& Ar)
{
    FScopeLock Lock(&GVMCSourcesLock);
    if (GVMCSources.Num() == 0)
    {
        Ar.Log(TEXT("VMC.Stats: no VMC sources."));
        return;
    }
    for (FVMCLiveLinkSource* Source : GVMCSources)
    {
        Ar.Log(Source->GetStatsReport());
    }
}

bool FVMCLiveLinkSource::Tick(float DeltaTime)
{
    // The receive thread can't touch UObjects, so it asks for the subject bootstrap here.
    if (bBootstrapRequested && !bEnsuredDefaults)
    {
        EnsureSubjectSettingsWithDefaults();
    }

    // A new or edited remapper renames differently: publish a snapshot with its settings, which
    // republishes the static data through them.
    if (Client && bEnsuredDefaults)
    {
        const ULiveLinkSubjectSettings* SubjectSettings = Cast<ULiveLinkSubjectSettings>(Client->GetSubjectSettings({ SourceGuid, Settings.SubjectName }));
        ULiveLinkSubjectRemapper* Remapper = SubjectSettings ? SubjectSettings->Remapper : nullptr;
        const UVMCLiveLinkRemapper* VMCRemapper = Cast<UVMCLiveLinkRemapper>(Remapper);
        const uint32 Revision = VMCRemapper ? VMCRemapper->GetRevision() : 0;
        if (Remapper != LastRemapper.Get() || Revision != LastRemapperRevision)
        {
            LastRemapper = Remapper;
            PublishSnapshot();
            // After: marking the remapper as applied by this source can raise its revision.
            LastRemapperRevision = VMCRemapper ? VMCRemapper->GetRevision() : Revision;
        }
    }
    return true;
}

TSharedPtr<const FVMCLiveLinkSource::FSnapshot> FVMCLiveLinkSource::GetSnapshot() const
{
    FScopeLock Lock(&SnapshotLock);
    return Snapshot;
}

void FVMCLiveLinkSource::PublishSnapshot()
{
    TSharedRef<FSnapshot> New = MakeShared<FSnapshot>();
    New->Settings = Settings;
    New->Version = ++SnapshotVersion;
    // The subject's VMC remapper, applied here rather than by Live Link (see FSnapshot::Remap).
    // Tick publishes a new snapshot whenever the remapper or its settings change.
    if (Client)
    {
        const ULiveLinkSubjectSettings* SubjectSettings = Cast<ULiveLinkSubjectSettings>(Client->GetSubjectSettings({ SourceGuid, Settings.SubjectName }));
        if (UVMCLiveLinkRemapper* VMCRemapper = SubjectSettings ? Cast<UVMCLiveLinkRemapper>(SubjectSettings->Remapper) : nullptr)
        {
            VMCRemapper->SetAppliedBySource(true);
            New->Remap = MakeShared<const FVMCRemapConfig>(VMCRemapper->MakeConfig());
        }
    }
    FScopeLock Lock(&SnapshotLock);
    Snapshot = MoveTemp(New);
}

// ---------------- Settings (game thread) ----------------

TSubclassOf<ULiveLinkSourceSettings> FVMCLiveLinkSource::GetSettingsClass() const
{
    return UVMCLiveLinkSourceSettings::StaticClass();
}

void FVMCLiveLinkSource::InitializeSettings(ULiveLinkSourceSettings* InSettings)
{
    // The source was created from the connection string (also when a preset is applied), so its
    // settings are the truth; show them.
    if (UVMCLiveLinkSourceSettings* VMCSettings = Cast<UVMCLiveLinkSourceSettings>(InSettings))
    {
        VMCSettings->FromConnectionSettings(Settings);
    }
}

void FVMCLiveLinkSource::OnSettingsChanged(ULiveLinkSourceSettings* InSettings, const FPropertyChangedEvent& PropertyChangedEvent)
{
    UVMCLiveLinkSourceSettings* VMCSettings = Cast<UVMCLiveLinkSourceSettings>(InSettings);
    if (!VMCSettings)
    {
        return;
    }
    const FVMCConnectionSettings New = VMCSettings->ToConnectionSettings();
    TArray<FString> Errors;
    if (!New.Validate(&Errors))
    {
        UE_LOG(LogVMCLiveLink, Warning, TEXT("VMC source '%s': %s. Keeping the previous settings."), *SourceName, *FString::Join(Errors, TEXT("; ")));
        VMCSettings->FromConnectionSettings(Settings);
        return;
    }
    if (New == Settings)
    {
        return;
    }

    // A new port, address, path or subject restarts receiving, so no frame is being built while
    // the subject changes. Other settings only need a new snapshot.
    const bool bNewSubject = New.SubjectName != Settings.SubjectName;
    // New sender rules restart too, so the filter starts afresh (and forgets a locked sender).
    const bool bRestart = bNewSubject || New.Port != Settings.Port || New.BindAddress != Settings.BindAddress
        || New.bReceiveThread != Settings.bReceiveThread || New.AllowedSenders != Settings.AllowedSenders
        || New.bLockToFirstSender != Settings.bLockToFirstSender
        || New.bDeviceSubjects != Settings.bDeviceSubjects || New.bCameraSubject != Settings.bCameraSubject;
    if (bRestart)
    {
        StopReceiving();
        // They come back on the next message if still wanted (under the new subject's name).
        RemoveDeviceSubjects();
    }
    if (bNewSubject && Client)
    {
        // Move to the new subject: remove the old one and bootstrap the new one on the next frame.
        Client->RemoveSubject_AnyThread({ SourceGuid, Settings.SubjectName });
        bEnsuredDefaults = false;
        bBootstrapRequested = false;
        bStaticSent = false;
    }
    Settings = New;
    VMCSettings->ConnectionString = Settings.ToString();
    PublishSnapshot();

    if (bRestart && Client)
    {
        // Stays a valid source if the new port can't be opened; the status says why it's silent.
        StartReceiving();
    }
    UE_LOG(LogVMCLiveLink, Log, TEXT("VMC source '%s': settings changed (%s)."), *SourceName, *Settings.ToString());
}

// ---------------- Game-thread path: the OSC plugin ----------------

bool FVMCLiveLinkSource::StartOSC()
{
    if (OscServer.IsValid())
    {
        return true;
    }

    OscServer = TStrongObjectPtr<UOSCServer>(NewObject<UOSCServer>());
    if (!OscServer.IsValid())
    {
        UE_LOG(LogVMCLiveLink, Error, TEXT("Failed to create UOSCServer"));
        return false;
    }

    OscServer->OnOscMessageReceivedNative.AddRaw(this, &FVMCLiveLinkSource::OnOscMessageReceived);

    if (!OscServer->SetAddress(Settings.BindAddress, (uint16)Settings.Port))
    {
        UE_LOG(LogVMCLiveLink, Error, TEXT("VMC source '%s': can't listen on %s:%d (address not on this machine, or port in use?)"),
            *SourceName, *Settings.BindAddress, Settings.Port);
        OscServer->OnOscMessageReceivedNative.RemoveAll(this);
        OscServer.Reset();
        return false;
    }

    OscServer->Listen();
    bListening = true;
    return true;
}

void FVMCLiveLinkSource::StopOSC()
{
    if (OscServer.IsValid())
    {
        OscServer->OnOscMessageReceivedNative.RemoveAll(this);
        OscServer->Stop();
        OscServer.Reset();
    }
}

void FVMCLiveLinkSource::OnOscMessageReceived(const FOSCMessage& Msg, const FString& FromIP, uint16 FromPort)
{
    // Dispatched on the game thread (UOSCServer::PumpPacketQueue), once per engine frame for
    // everything that arrived since the last one.
    if (SenderFilter->IsActive() && !AcceptSender(FromIP))
    {
        return;
    }
    if (FromIP != LastSenderSeen)
    {
        NoteSender(FromIP);
    }
    MessageStats->CountPacket(); // the OSC plugin hands over messages, not packets: count each
    const FString Address = Msg.GetAddress().GetFullPath();
    const VMCProtocol::EAddress Kind = VMCProtocol::ClassifyAddress(Address);
    if (Kind == VMCProtocol::EAddress::Other)
    {
        MessageStats->CountUnknown(Address);
    }
    else
    {
        MessageStats->CountMessage(Kind);
    }
    VMCProtocol::FArgs Args;
    VMCProtocol::ReadArgs(Msg, Args);
    if (const TSharedPtr<const FSnapshot> Snap = GetSnapshot())
    {
        ProcessMessage(Kind, Args, FPlatformTime::Seconds(), *Snap);
    }
}

// ---------------- Receive-thread path ----------------

void FVMCLiveLinkSource::OnPacket(TConstArrayView<uint8> Packet, double ArrivalSeconds, const FInternetAddr& Sender)
{
    if (SenderFilter->IsActive() && !AcceptSender(Sender.ToString(/*bAppendPort*/ false)))
    {
        return;
    }
    // Only turn the address into text when the sender changes (packets arrive hundreds of times a second).
    const uint32 SenderHash = Sender.GetTypeHash();
    if (SenderHash != LastSenderHash || LastSenderSeen.IsEmpty())
    {
        LastSenderHash = SenderHash;
        NoteSender(Sender.ToString(/*bAppendPort*/ false));
    }
    const TSharedPtr<const FSnapshot> Snap = GetSnapshot();
    if (!Snap)
    {
        return;
    }
    MessageStats->CountPacket();
    const bool bOk = VMCOscParser::ParsePacket(Packet, [this, ArrivalSeconds, &Snap](FAnsiStringView Address, TConstArrayView<VMCProtocol::FArg> Args)
    {
        const VMCProtocol::EAddress Kind = VMCProtocol::ClassifyAddress(Address);
        if (Kind == VMCProtocol::EAddress::Other)
        {
            MessageStats->CountUnknown(FString(Address));
        }
        else
        {
            MessageStats->CountMessage(Kind);
        }
        ProcessMessage(Kind, Args, ArrivalSeconds, *Snap);
    });
    if (!bOk && !bWarnedMalformed)
    {
        UE_LOG(LogVMCLiveLink, Warning, TEXT("VMC source '%s': ignoring a malformed OSC packet (%d bytes). Further malformed input is ignored silently."),
            *SourceName, Packet.Num());
        bWarnedMalformed = true;
    }
}

// ---------------- Frame building (receive thread, or game thread) ----------------

void FVMCLiveLinkSource::NoteSender(const FString& Sender)
{
    LastSenderSeen = Sender;
    FScopeLock Lock(&StatsLock);
    LastSender = Sender;
}

bool FVMCLiveLinkSource::AcceptSender(const FString& Sender)
{
    switch (SenderFilter->Check(Sender))
    {
    case FVMCSenderFilter::EResult::Accepted:
        return true;
    case FVMCSenderFilter::EResult::Locked:
    {
        UE_LOG(LogVMCLiveLink, Log, TEXT("VMC source '%s': using only %s (Lock to First Sender)."), *SourceName, *Sender);
        FScopeLock Lock(&StatsLock);
        LockedSender = Sender;
        return true;
    }
    case FVMCSenderFilter::EResult::RejectedNew:
    {
        const FString& Locked = SenderFilter->GetLockedSender();
        UE_LOG(LogVMCLiveLink, Log, TEXT("VMC source '%s': ignoring packets from %s (%s)."), *SourceName, *Sender,
            Locked.IsEmpty() ? TEXT("not an allowed sender") : *FString::Printf(TEXT("locked to %s"), *Locked));
        FScopeLock Lock(&StatsLock);
        ++IgnoredSenders;
        return false;
    }
    default:
        return false;
    }
}

void FVMCLiveLinkSource::ProcessMessage(VMCProtocol::EAddress Kind, TConstArrayView<VMCProtocol::FArg> Args, double ArrivalSeconds, const FSnapshot& Snap)
{
    const FVMCFrameAssembler::FMessageResult Result = Assembler->ApplyMessage(Kind, Args, Snap.Settings);

    if (Result.bMalformed && !bWarnedMalformed)
    {
        UE_LOG(LogVMCLiveLink, Warning, TEXT("VMC source '%s': ignoring a malformed VMC message (unexpected argument count or types). Further malformed input is ignored silently."),
            *SourceName);
        bWarnedMalformed = true;
    }
    if (Result.bLegacyRoot && !bWarnedLegacyRoot)
    {
        UE_LOG(LogVMCLiveLink, Log, TEXT("VMC source '%s': /VMC/Ext/Root/Pos arrived without a name (7 floats). Accepting it, but the VMC protocol sends a name first."),
            *SourceName);
        bWarnedLegacyRoot = true;
    }
    if (Result.bRootScaleOffset && !bWarnedRootScaleOffset)
    {
        // VMC v2.1 scale/offset is for mixed-reality calibration; not applied yet.
        UE_LOG(LogVMCLiveLink, Log, TEXT("VMC source '%s': sender provides VMC v2.1 root scale and offset; they are currently ignored."),
            *SourceName);
        bWarnedRootScaleOffset = true;
    }
    if (Result.bSenderStateChanged)
    {
        const FString Text = VMCProtocol::DescribeSenderState(*Assembler->GetSenderState());
        UE_LOG(LogVMCLiveLink, Log, TEXT("VMC source '%s': sender %s."), *SourceName, Text.IsEmpty() ? TEXT("is ready") : *Text);
        FScopeLock Lock(&StatsLock);
        SenderStateText = Text;
    }
    if (!Result.NewBone.IsNone())
    {
        UE_LOG(LogVMCLiveLink, Log, TEXT("VMC source '%s': bone '%s' is not a Unity humanoid bone; parenting it to Hips."),
            *SourceName, *Result.NewBone.ToString());
    }
    bStaticDirty |= Result.bStaticChanged;
    if (Result.Device.IsSet())
    {
        PushDevice(*Result.Device, ArrivalSeconds, Snap);
    }

    if (!Result.bApply)
    {
        return;
    }

    // The subject is bootstrapped before anything is pushed. On the game thread that happens here,
    // as before; the receive thread asks the game thread and drops frames until it's done.
    const FSnapshot* Current = &Snap;
    TSharedPtr<const FSnapshot> Refreshed;
    if (IsInGameThread())
    {
        if (!bEnsuredDefaults)
        {
            EnsureSubjectSettingsWithDefaults();
            Refreshed = GetSnapshot();
            Current = Refreshed.Get();
        }
    }
    else if (!bEnsuredDefaults)
    {
        bBootstrapRequested = true;
        Assembler->EndFrame(Snap.Settings.bZeroMissingCurves);
        return;
    }

    // Static data before the frame, so a frame never carries more curve values than the static
    // data it is validated against.
    if (!bStaticSent || bStaticDirty || Current->Version != PublishedStaticVersion)
    {
        bStaticDirty = false;
        PushStaticData(*Current);
    }
    PushFrame(*Current, ArrivalSeconds);
    Assembler->EndFrame(Current->Settings.bZeroMissingCurves);
}

void FVMCLiveLinkSource::PushStaticData(const FSnapshot& Snap)
{
    if (!Client)
    {
        return;
    }
    // VMC's names, renamed by the subject's VMC remapper. The frames pushed until the next static
    // data go through the same worker, which resolved what they need (rest translations, added
    // curves) for this static data.
    FLiveLinkStaticDataStruct Static = Assembler->MakeStaticData(nullptr, nullptr);
    FrameRemapper.Reset();
    if (Snap.Remap)
    {
        FrameRemapper = MakeShared<FVMCLiveLinkRemapperWorker>(*Snap.Remap);
        FrameRemapper->RemapStaticData(Static);
    }
    Client->PushSubjectStaticData_AnyThread({ SourceGuid, Snap.Settings.SubjectName },
        ULiveLinkAnimationRole::StaticClass(), MoveTemp(Static));
    PublishedStaticVersion = Snap.Version;
    bStaticSent = true;

    FScopeLock Lock(&StatsLock);
    const TConstArrayView<FName> Bones = Assembler->GetBoneNames();
    const TConstArrayView<FName> Curves = Assembler->GetCurveNames();
    PublishedBones.Reset();
    PublishedBones.Append(Bones.GetData(), Bones.Num());
    PublishedCurves.Reset();
    PublishedCurves.Append(Curves.GetData(), Curves.Num());
}

void FVMCLiveLinkSource::PushFrame(const FSnapshot& Snap, double ArrivalSeconds)
{
    if (!Client)
    {
        return;
    }
    // Bones without a streamed translation are left at zero; the remapper's worker gives them the
    // target skeleton's rest translation.
    FVMCFrameAssembler::FFrameOptions Options;
    Options.bPreferIncomingTranslations = Snap.Settings.bPreferIncomingTranslations;
    Options.bUseRefOffsets = false;

    FLiveLinkFrameDataStruct Frame = Assembler->MakeFrameData(Options);
    if (FrameRemapper)
    {
        FrameRemapper->RemapFrameData(FLiveLinkStaticDataStruct(), Frame); // uses what it resolved at static time
    }
    FLiveLinkBaseFrameData& Base = *Frame.Cast<FLiveLinkAnimationFrameData>();
    // When the packet arrived (receive thread), or when the game thread got to it.
    Base.WorldTime = FLiveLinkWorldTime(ArrivalSeconds);
    if (const TOptional<float> SenderTime = Assembler->GetSenderTime())
    {
        Base.MetaData.SceneTime = FQualifiedFrameTime(FFrameTime::FromDecimal(double(*SenderTime) * SenderTimeRate.AsDecimal()), SenderTimeRate);
    }
    Client->PushSubjectFrameData_AnyThread({ SourceGuid, Snap.Settings.SubjectName }, MoveTemp(Frame));

    FScopeLock Lock(&StatsLock);
    if (LastFrameSeconds > 0.0)
    {
        const double Interval = ArrivalSeconds - LastFrameSeconds;
        MeanFrameInterval = MeanFrameInterval > 0.0 ? FMath::Lerp(MeanFrameInterval, Interval, StatsSmoothing) : Interval;
        MeanIntervalDeviation = FMath::Lerp(MeanIntervalDeviation, FMath::Abs(Interval - MeanFrameInterval), StatsSmoothing);
    }
    LastFrameSeconds = ArrivalSeconds;
}

void FVMCLiveLinkSource::PushDevice(const FVMCDevicePose& Device, double ArrivalSeconds, const FSnapshot& Snap)
{
    if (!Client || !(Device.bCamera ? Snap.Settings.bCameraSubject : Snap.Settings.bDeviceSubjects))
    {
        return;
    }
    TMap<FName, FName>& Subjects = Device.bCamera ? CameraSubjects : DeviceSubjects;
    const TMap<FName, FName>& Others = Device.bCamera ? DeviceSubjects : CameraSubjects;
    FName Subject;
    if (const FName* Found = Subjects.Find(Device.Name))
    {
        Subject = *Found;
    }
    else
    {
        // First pose from this device: its subject's static data, once. A name another device or
        // camera already publishes under gets a number, so no two ever share a subject.
        const FName Base = VMCProtocol::MakeDeviceSubjectName(Snap.Settings.SubjectName, Device.Name);
        Subject = Base;
        for (int32 Suffix = 2; Subjects.FindKey(Subject) || Others.FindKey(Subject); ++Suffix)
        {
            Subject = FName(*FString::Printf(TEXT("%s_%d"), *Base.ToString(), Suffix));
        }
        if (Device.bCamera)
        {
            FLiveLinkStaticDataStruct Static(FLiveLinkCameraStaticData::StaticStruct());
            Static.Cast<FLiveLinkCameraStaticData>()->bIsFieldOfViewSupported = true;
            Client->PushSubjectStaticData_AnyThread({ SourceGuid, Subject }, ULiveLinkCameraRole::StaticClass(), MoveTemp(Static));
        }
        else
        {
            FLiveLinkStaticDataStruct Static(FLiveLinkTransformStaticData::StaticStruct());
            Client->PushSubjectStaticData_AnyThread({ SourceGuid, Subject }, ULiveLinkTransformRole::StaticClass(), MoveTemp(Static));
        }
        Subjects.Add(Device.Name, Subject);
        UE_LOG(LogVMCLiveLink, Log, TEXT("VMC source '%s': publishing %s '%s' as subject '%s'."),
            *SourceName, Device.bCamera ? TEXT("camera") : TEXT("device"), *Device.Name.ToString(), *Subject.ToString());
        FScopeLock Lock(&StatsLock);
        NumDeviceSubjects = DeviceSubjects.Num() + CameraSubjects.Num();
    }

    FLiveLinkFrameDataStruct Frame(Device.bCamera ? FLiveLinkCameraFrameData::StaticStruct() : FLiveLinkTransformFrameData::StaticStruct());
    FLiveLinkTransformFrameData* Data = Frame.Cast<FLiveLinkTransformFrameData>(); // the camera's derives from it
    if (Device.bCamera)
    {
        Frame.Cast<FLiveLinkCameraFrameData>()->FieldOfView = Device.FieldOfView;
    }
    Data->Transform = Device.Transform;
    // Timed like the main subject's frames (PushFrame), so the subjects stay in step.
    Data->WorldTime = FLiveLinkWorldTime(ArrivalSeconds);
    if (const TOptional<float> SenderTime = Assembler->GetSenderTime())
    {
        Data->MetaData.SceneTime = FQualifiedFrameTime(FFrameTime::FromDecimal(double(*SenderTime) * SenderTimeRate.AsDecimal()), SenderTimeRate);
    }
    Client->PushSubjectFrameData_AnyThread({ SourceGuid, Subject }, MoveTemp(Frame));
}

void FVMCLiveLinkSource::RemoveDeviceSubjects()
{
    if (Client)
    {
        for (const TMap<FName, FName>* Subjects : { &DeviceSubjects, &CameraSubjects })
        {
            for (const TPair<FName, FName>& Pair : *Subjects)
            {
                Client->RemoveSubject_AnyThread({ SourceGuid, Pair.Value });
            }
        }
    }
    DeviceSubjects.Reset();
    CameraSubjects.Reset();
    FScopeLock Lock(&StatsLock);
    NumDeviceSubjects = 0;
}

// ---------------- Subject (game thread) ----------------

void FVMCLiveLinkSource::EnsureSubjectSettingsWithDefaults()
{
    if (!Client || bEnsuredDefaults)
        return;

    check(IsInGameThread());

    const FLiveLinkSubjectKey Key{ SourceGuid, Settings.SubjectName };

    if (Client->GetSubjectSettings(Key) != nullptr)
    {
        // The subject already exists (for example, created by a Live Link preset or configured by
        // the user). Keep its settings, including its remapper choice, as they are.
        UE_LOG(LogVMCLiveLink, Log, TEXT("VMC subject '%s' already exists; keeping its settings."), *Settings.SubjectName.ToString());
    }
    else
    {
        // Bootstrap the subject with our default remapper.
        FLiveLinkSubjectPreset Preset;
        Preset.Key = Key;
        Preset.Role = ULiveLinkAnimationRole::StaticClass();

        ULiveLinkSubjectSettings* NewSettings = NewObject<ULiveLinkSubjectSettings>(GetTransientPackage());

        const UVMCLiveLinkSettings* Proj = GetDefault<UVMCLiveLinkSettings>();
        UClass* RemapperClass = (Proj && !Proj->DefaultRemapperClass.IsNull())
            ? Proj->DefaultRemapperClass.LoadSynchronous()
            : UVMCLiveLinkRemapper::StaticClass();
        if (!RemapperClass)
        {
            RemapperClass = UVMCLiveLinkRemapper::StaticClass();
        }

        NewSettings->Remapper = NewObject<ULiveLinkSubjectRemapper>(NewSettings, RemapperClass);
        Preset.Settings = NewSettings;

        // This creates the subject + settings in the client now (not "eventually")
        Client->CreateSubject(Preset);
        Client->SetSubjectEnabled(Preset.Key, true);
    }

    // Make sure the next frame publishes static data for the (new) subject
    PublishSnapshot();
    bEnsuredDefaults = true;
}
