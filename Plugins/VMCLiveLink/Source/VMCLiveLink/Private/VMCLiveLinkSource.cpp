// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VMCLiveLinkSource.h"
#include "VMCLog.h"
#include "VMCHumanoid.h"
#include "VMCProtocol.h"
#include "VMCFrameAssembler.h"
#include "VMCOscParser.h"
#include "VMCUdpReceiver.h"

// Live Link
#include "ILiveLinkClient.h"
#include "LiveLinkTypes.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"

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
    bIsValid = StartReceiving();

    UE_LOG(LogVMCLiveLink, Log, TEXT("VMC source '%s' listening on %s:%d (valid=%d, %s)"),
        *SourceName, *Settings.BindAddress, Settings.Port, bIsValid ? 1 : 0, *Settings.ToString());
}

FVMCLiveLinkSource::~FVMCLiveLinkSource()
{
    // Callbacks are bound to this (the OSC delegate with AddRaw, the receive thread and the ticker),
    // so they must be removed even if Live Link never called RequestSourceShutdown. The OSC server
    // is a UObject, so skip it if the UObject system is already gone (very late teardown).
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
    if (!Settings.bReceiveThread)
    {
        return StartOSC();
    }
    FString Error;
    Receiver = FVMCUdpReceiver::Start(Settings.BindAddress, Settings.Port,
        [this](TConstArrayView<uint8> Packet, double ArrivalSeconds) { OnPacket(Packet, ArrivalSeconds); },
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
    if (!bIsValid)
    {
        return NSLOCTEXT("VMCLiveLink", "Status_Stopped", "Stopped");
    }
    if (!bListening)
    {
        return FText::Format(NSLOCTEXT("VMCLiveLink", "Status_NotListening", "Can't listen on port {0}"), FText::AsNumber(Settings.Port, &FNumberFormattingOptions::DefaultNoGrouping()));
    }
    if (!bStaticSent)
    {
        return NSLOCTEXT("VMCLiveLink", "Status_Waiting", "Waiting for first frame");
    }

    double Last = 0.0, Interval = 0.0, Jitter = 0.0;
    {
        FScopeLock Lock(&StatsLock);
        Last = LastFrameSeconds;
        Interval = MeanFrameInterval;
        Jitter = MeanIntervalDeviation;
    }
    const double Silent = FPlatformTime::Seconds() - Last;
    if (Silent > 1.0)
    {
        return FText::Format(NSLOCTEXT("VMCLiveLink", "Status_NoData", "No data for {0} s"), FText::AsNumber(FMath::FloorToInt(Silent)));
    }
    FNumberFormattingOptions OneDecimal;
    OneDecimal.SetMinimumFractionalDigits(1).SetMaximumFractionalDigits(1);
    return FText::Format(NSLOCTEXT("VMCLiveLink", "Status_Receiving", "Receiving: {0} fps, jitter {1} ms ({2})"),
        FText::AsNumber(Interval > 0.0 ? 1.0 / Interval : 0.0, &OneDecimal),
        FText::AsNumber(Jitter * 1000.0, &OneDecimal),
        Settings.bReceiveThread ? NSLOCTEXT("VMCLiveLink", "Path_Thread", "receive thread") : NSLOCTEXT("VMCLiveLink", "Path_Game", "game thread"));
}

bool FVMCLiveLinkSource::Tick(float DeltaTime)
{
    // The receive thread can't touch UObjects, so it asks for the subject bootstrap here.
    if (bBootstrapRequested && !bEnsuredDefaults)
    {
        EnsureSubjectSettingsWithDefaults();
    }

    // A new or edited remapper renames differently: republish the static data so Live Link runs
    // it through the new worker.
    if (Client && bEnsuredDefaults)
    {
        const ULiveLinkSubjectSettings* SubjectSettings = Cast<ULiveLinkSubjectSettings>(Client->GetSubjectSettings({ SourceGuid, Settings.SubjectName }));
        ULiveLinkSubjectRemapper* Remapper = SubjectSettings ? SubjectSettings->Remapper : nullptr;
        const UVMCLiveLinkRemapper* VMCRemapper = Cast<UVMCLiveLinkRemapper>(Remapper);
        const uint32 Revision = VMCRemapper ? VMCRemapper->GetRevision() : 0;
        if (Remapper != LastRemapper.Get() || Revision != LastRemapperRevision)
        {
            LastRemapper = Remapper;
            LastRemapperRevision = Revision;
            PublishSnapshot();
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
    const bool bRestart = bNewSubject || New.Port != Settings.Port || New.BindAddress != Settings.BindAddress
        || New.bReceiveThread != Settings.bReceiveThread;
    if (bRestart)
    {
        StopReceiving();
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
    const VMCProtocol::EAddress Kind = VMCProtocol::ClassifyAddress(Msg.GetAddress().GetFullPath());
    VMCProtocol::FArgs Args;
    VMCProtocol::ReadArgs(Msg, Args);
    if (const TSharedPtr<const FSnapshot> Snap = GetSnapshot())
    {
        ProcessMessage(Kind, Args, FPlatformTime::Seconds(), *Snap);
    }
}

// ---------------- Receive-thread path ----------------

void FVMCLiveLinkSource::OnPacket(TConstArrayView<uint8> Packet, double ArrivalSeconds)
{
    const TSharedPtr<const FSnapshot> Snap = GetSnapshot();
    if (!Snap)
    {
        return;
    }
    const bool bOk = VMCOscParser::ParsePacket(Packet, [this, ArrivalSeconds, &Snap](FAnsiStringView Address, TConstArrayView<VMCProtocol::FArg> Args)
    {
        ProcessMessage(VMCProtocol::ClassifyAddress(Address), Args, ArrivalSeconds, *Snap);
    });
    if (!bOk && !bWarnedMalformed)
    {
        UE_LOG(LogVMCLiveLink, Warning, TEXT("VMC source '%s': ignoring a malformed OSC packet (%d bytes). Further malformed input is ignored silently."),
            *SourceName, Packet.Num());
        bWarnedMalformed = true;
    }
}

// ---------------- Frame building (receive thread, or game thread) ----------------

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
    if (!Result.NewBone.IsNone())
    {
        UE_LOG(LogVMCLiveLink, Log, TEXT("VMC source '%s': bone '%s' is not a Unity humanoid bone; parenting it to Hips."),
            *SourceName, *Result.NewBone.ToString());
    }
    bStaticDirty |= Result.bStaticChanged;

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
    Client->PushSubjectStaticData_AnyThread({ SourceGuid, Snap.Settings.SubjectName },
        ULiveLinkAnimationRole::StaticClass(), Assembler->MakeStaticData(nullptr, nullptr)); // VMC names; the remapper renames
    PublishedStaticVersion = Snap.Version;
    bStaticSent = true;
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
