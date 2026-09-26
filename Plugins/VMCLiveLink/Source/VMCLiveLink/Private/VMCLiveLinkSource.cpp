// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VMCLiveLinkSource.h"
#include "VMCLog.h"
#include "VMCHumanoid.h"
#include "VMCProtocol.h"
#include "VMCFrameAssembler.h"

// Live Link
#include "ILiveLinkClient.h"
#include "LiveLinkTypes.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"

#include "VMCLiveLinkSettings.h"
#include "LiveLinkSubjectSettings.h"
#include "LiveLinkSubjectRemapper.h"
#include "VMCLiveLinkRemapper.h"

// OSC (cpp-only)
#include "OSCServer.h"
#include "OSCMessage.h"
#include "OSCTypes.h"

// Math
#include "Math/RotationMatrix.h"
#include "Engine/SkeletalMesh.h" // for BuildRefOffsetsFromMesh

// ---------------- Ctors & status ----------------

FVMCLiveLinkSource::FVMCLiveLinkSource(const FString& InSourceName)
    : SourceName(InSourceName), ListenPort(39539), bUnityToUE(true), bMetersToCm(true), YawOffsetDeg(0.f)
{
    InitSkeleton();
}

FVMCLiveLinkSource::FVMCLiveLinkSource(const FString& InSourceName, int32 InPort)
    : SourceName(InSourceName), ListenPort(InPort), bUnityToUE(true), bMetersToCm(true), YawOffsetDeg(0.f)
{
    InitSkeleton();
}

FVMCLiveLinkSource::FVMCLiveLinkSource(const FString& InSourceName, int32 InPort, bool bInUnityToUE, bool bInMetersToCm, float InYawDeg)
    : SourceName(InSourceName), ListenPort(InPort), bUnityToUE(bInUnityToUE), bMetersToCm(bInMetersToCm), YawOffsetDeg(InYawDeg)
{
    InitSkeleton();
}

FVMCLiveLinkSource::FVMCLiveLinkSource(const FString& InSourceName, int32 InPort, bool bInUnityToUE, bool bInMetersToCm, float InYawDeg, FString InSubject)
    : SourceName(InSourceName), ListenPort(InPort), bUnityToUE(bInUnityToUE), bMetersToCm(bInMetersToCm), YawOffsetDeg(InYawDeg), SubjectName(InSubject)

{
    InitSkeleton();
}

void FVMCLiveLinkSource::InitSkeleton()
{
    Assembler = MakeUnique<FVMCFrameAssembler>();
}

void FVMCLiveLinkSource::ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid)
{
    Client = InClient;
    SourceGuid = InSourceGuid;
    bIsValid = StartOSC();
    // Subject settings are bootstrapped on the first /VMC/Ext/Blend/Apply (before anything is
    // pushed), not here. Deferring lets a Live Link preset, which adds its sources before its
    // subjects, create the subject with its saved settings first; EnsureSubjectSettingsWithDefaults
    // then leaves those settings alone.
    // Warm caches and republish static once with mapped names
    RefreshStaticMapsFromSettings();
    bForceStaticNext = true;

    UE_LOG(LogVMCLiveLink, Log, TEXT("VMC source '%s' listening on %d (valid=%d, unity2ue=%d, m_to_cm=%d, yaw=%.1f)"),
        *SourceName, ListenPort, bIsValid ? 1 : 0, bUnityToUE ? 1 : 0, bMetersToCm ? 1 : 0, YawOffsetDeg);
}

FVMCLiveLinkSource::~FVMCLiveLinkSource()
{
    // The OSC delegate is bound with AddRaw(this), so it must be removed even if Live Link
    // never called RequestSourceShutdown. Skip this if the UObject system is already gone
    // (very late teardown), since the OSC server is a UObject.
    if (UObjectInitialized())
    {
        StopOSC();
    }
}

bool FVMCLiveLinkSource::RequestSourceShutdown()
{
    StopOSC();
    bIsValid = false;
    Client = nullptr;
    return true;
}

FText FVMCLiveLinkSource::GetSourceStatus() const
{
    const bool bReady = bIsValid && bStaticSent;
    return bIsValid
        ? (bReady
            ? NSLOCTEXT("VMCLiveLink", "Status_Receiving", "Receiving data")
            : NSLOCTEXT("VMCLiveLink", "Status_Waiting", "Waiting for first frame"))
        : NSLOCTEXT("VMCLiveLink", "Status_Stopped", "Stopped");
}

// ---------------- OSC lifecycle ----------------

bool FVMCLiveLinkSource::StartOSC()
{
    if (OscServer.IsValid())
        return true;

    OscServer = TStrongObjectPtr<UOSCServer>(NewObject<UOSCServer>());
    if (!OscServer.IsValid())
    {
        UE_LOG(LogVMCLiveLink, Error, TEXT("Failed to create UOSCServer"));
        return false;
    }

    OscServer->OnOscMessageReceivedNative.AddRaw(this, &FVMCLiveLinkSource::OnOscMessageReceived);

    if (!OscServer->SetAddress(TEXT("0.0.0.0"), (uint16)ListenPort))
    {
        UE_LOG(LogVMCLiveLink, Error, TEXT("UOSCServer SetAddress failed for port %d"), ListenPort);
        OscServer->OnOscMessageReceivedNative.RemoveAll(this);
        OscServer.Reset();
        return false;
    }

    OscServer->Listen();
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

// ---------------- OSC message handler ----------------

void FVMCLiveLinkSource::OnOscMessageReceived(const FOSCMessage& Msg, const FString& FromIP, uint16 FromPort)
{
    const FString Addr = Msg.GetAddress().GetFullPath();
    const VMCProtocol::EAddress Kind = VMCProtocol::ClassifyAddress(Addr);
    if (Kind != VMCProtocol::EAddress::BonePos && Kind != VMCProtocol::EAddress::RootPos
        && Kind != VMCProtocol::EAddress::BlendVal && Kind != VMCProtocol::EAddress::BlendApply)
    {
        return; // not used (yet): time, availability, devices, camera, ...
    }

    auto WarnMalformed = [this, &Addr]()
    {
        if (!bWarnedMalformed)
        {
            UE_LOG(LogVMCLiveLink, Warning, TEXT("VMC source '%s': ignoring malformed %s message (unexpected argument count or types). Further malformed messages are ignored silently."),
                *SourceName, *Addr);
            bWarnedMalformed = true;
        }
    };

    VMCProtocol::FArgs Args;
    if (Kind != VMCProtocol::EAddress::BlendApply)
    {
        VMCProtocol::ReadArgs(Msg, Args);
    }

    switch (Kind)
    {
    case VMCProtocol::EAddress::BonePos:
    {
        VMCProtocol::FPose Pose;
        if (!VMCProtocol::ParseBonePos(Args, Pose))
        {
            WarnMalformed();
            return;
        }
        const FName BoneName(*Pose.Name);
        const FTransform Xf(
            VMCProtocol::ToUERotation(Pose.Rotation, bUnityToUE),
            VMCProtocol::ToUEPosition(Pose.Position, bUnityToUE, bMetersToCm),
            FVector::OneVector);
        if (Assembler->SetBone(BoneName, Xf))
        {
            UE_LOG(LogVMCLiveLink, Log, TEXT("VMC source '%s': bone '%s' is not a Unity humanoid bone; parenting it to Hips."),
                *SourceName, *BoneName.ToString());
            bStaticDirty = true;
        }
        break;
    }
    case VMCProtocol::EAddress::RootPos:
    {
        VMCProtocol::FPose Pose;
        bool bLegacyForm = false;
        if (!VMCProtocol::ParseRootPos(Args, Pose, bLegacyForm))
        {
            WarnMalformed();
            return;
        }
        if (bLegacyForm && !bWarnedLegacyRoot)
        {
            UE_LOG(LogVMCLiveLink, Log, TEXT("VMC source '%s': /VMC/Ext/Root/Pos arrived without a name (7 floats). Accepting it, but the VMC protocol sends a name first."),
                *SourceName);
            bWarnedLegacyRoot = true;
        }
        if (Pose.bHasScaleAndOffset && !bWarnedRootScaleOffset)
        {
            // VMC v2.1 scale/offset is for mixed-reality calibration; not applied yet.
            UE_LOG(LogVMCLiveLink, Log, TEXT("VMC source '%s': sender provides VMC v2.1 root scale and offset; they are currently ignored."),
                *SourceName);
            bWarnedRootScaleOffset = true;
        }

        FVector PR = VMCProtocol::ToUEPosition(Pose.Position, bUnityToUE, bMetersToCm);
        FQuat   QR = VMCProtocol::ToUERotation(Pose.Rotation, bUnityToUE);

        // Apply extra yaw offset about UE Z
        if (!FMath::IsNearlyZero(YawOffsetDeg))
        {
            const FQuat YawDelta(FVector::UpVector, FMath::DegreesToRadians(YawOffsetDeg));
            QR = YawDelta * QR;
            PR = YawDelta.RotateVector(PR);
        }
        Assembler->SetRoot(FTransform(QR, PR, FVector::OneVector));
        break;
    }
    case VMCProtocol::EAddress::BlendVal:
    {
        FString Name;
        float Val = 0.f;
        if (!VMCProtocol::ParseBlendVal(Args, Name, Val))
        {
            WarnMalformed();
            return;
        }
        if (Assembler->SetCurve(FName(*Name), Val))
        {
            bStaticDirty = true; // advertise this name in static data
        }
        break;
    }
    case VMCProtocol::EAddress::BlendApply:
    {
        // Dispatched on the game thread (see the class comment), so the subject bootstrap can run
        // synchronously here, before anything is pushed.
        if (!bEnsuredDefaults)
        {
            EnsureSubjectSettingsWithDefaults(); // also refreshes the cached maps
        }
        else
        {
            RefreshStaticMapsFromSettings();
        }

        // Static data before the frame, so a frame never carries more curve values than the
        // static data it is validated against.
        if (!bStaticSent || bForceStaticNext || bStaticDirty)
        {
            bForceStaticNext = false;
            bStaticDirty = false;
            PushStaticData();
        }
        PushFrame();
        Assembler->EndFrame(bZeroMissingCurves);
        break;
    }
    default:
        break;
    }
}

// ---------------- Live Link data push ----------------

void FVMCLiveLinkSource::PushStaticData()
{
    if (!Client)
    {
        return;
    }
    Client->PushSubjectStaticData_AnyThread({ SourceGuid, SubjectName },
        ULiveLinkAnimationRole::StaticClass(), Assembler->MakeStaticData(&CachedBoneMap, &CachedCurveMap));
    bStaticSent = true;
}

void FVMCLiveLinkSource::PushFrame()
{
    if (!Client)
    {
        return;
    }
    FVMCFrameAssembler::FFrameOptions Options;
    Options.bPreferIncomingTranslations = bPreferIncomingTranslations;
    Options.bUseRefOffsets = bUseRefOffsets && bHaveRefOffsets;
    Options.RefOffsets = &RefLocalTranslationByName;
    Options.BoneMap = &CachedBoneMap;
    Client->PushSubjectFrameData_AnyThread({ SourceGuid, SubjectName }, Assembler->MakeFrameData(Options));
}

uint32 FVMCLiveLinkSource::HashMaps(const TMap<FName, FName>& A, const TMap<FName, FName>& B)
{
    uint32 H = 1469598103u; // FNV-ish seed
    auto Mix = [&](const TMap<FName, FName>& M)
        {
            for (const auto& P : M)
            {
                H = HashCombine(H, GetTypeHash(P.Key));
                H = HashCombine(H, GetTypeHash(P.Value));
            }
        };
    Mix(A); Mix(B);
    return H;
}

void FVMCLiveLinkSource::BuildRefOffsetsFromMesh(USkeletalMesh* Mesh)
{
    RefLocalTranslationByName.Empty();
    bHaveRefOffsets = false;
    if (!Mesh) return;

    const FReferenceSkeleton& RS = Mesh->GetRefSkeleton();
    const TArray<FTransform>& RefPose = RS.GetRefBonePose(); // local (parent-space)
    const int32 Num = RS.GetNum();
    for (int32 i = 0; i < Num; ++i)
    {
        const FName Bone = RS.GetBoneName(i);
        RefLocalTranslationByName.Add(Bone, RefPose[i].GetTranslation());
    }
    bHaveRefOffsets = true;
}

// Pull remapper + maps + ReferenceSkeleton from subject settings
void FVMCLiveLinkSource::RefreshStaticMapsFromSettings()
{
    if (!Client) return;

    UObject* SettingsObj = Client->GetSubjectSettings({ SourceGuid, SubjectName });
    ULiveLinkSubjectSettings* Settings = Cast<ULiveLinkSubjectSettings>(SettingsObj);
    ULiveLinkSubjectRemapper* NowRemapper = Settings ? Settings->Remapper : nullptr;

    const bool bRemapperChanged = (LastSeenRemapper.Get() != NowRemapper);
    if (bRemapperChanged)
    {
        LastSeenRemapper = NowRemapper;
        bForceStaticNext = true; // names may change
    }

    // Maps + reference mesh. Runs every Apply, so the maps are hashed in place and only copied
    // when they changed.
    static const TMap<FName, FName> EmptyMap;
    const TMap<FName, FName>* NewBone = &EmptyMap;
    const TMap<FName, FName>* NewCurve = &EmptyMap;
    USkeletalMesh* RefMesh = nullptr;

    if (NowRemapper)
    {
        NewBone = &NowRemapper->BoneNameMap;

        if (const UVMCLiveLinkRemapper* My = Cast<UVMCLiveLinkRemapper>(NowRemapper))
        {
            NewCurve = &My->CurveNameMap;
            RefMesh = My->ReferenceSkeleton.LoadSynchronous();
        }
    }

    //  - Rebuild offsets if mesh changed or cache invalid
    const bool bMeshChanged = (LastRefMeshBuiltFrom.Get() != RefMesh);
    const bool bNeverBuilt = !bHaveRefOffsets || RefLocalTranslationByName.Num() == 0;
    const bool bCountMismatch = RefMesh && (RefLocalTranslationByName.Num() != RefMesh->GetRefSkeleton().GetNum());

    if (RefMesh && (bMeshChanged || bNeverBuilt || bCountMismatch))
    {
        BuildRefOffsetsFromMesh(RefMesh);
        LastRefMeshBuiltFrom = RefMesh;
    }

    const uint32 NewHash = HashMaps(*NewBone, *NewCurve);
    if (NewHash != CachedMapsHash)
    {
        CachedMapsHash = NewHash;
        CachedBoneMap = *NewBone;
        CachedCurveMap = *NewCurve;
        bForceStaticNext = true; // names changed → republish once
    }
}

void FVMCLiveLinkSource::EnsureSubjectSettingsWithDefaults()
{
    if (!Client || bEnsuredDefaults)
        return;

    check(IsInGameThread());

    const FLiveLinkSubjectKey Key{ SourceGuid, SubjectName };

    if (Client->GetSubjectSettings(Key) != nullptr)
    {
        // The subject already exists (for example, created by a Live Link preset or configured by
        // the user). Keep its settings, including its remapper choice, as they are.
        UE_LOG(LogVMCLiveLink, Log, TEXT("VMC subject '%s' already exists; keeping its settings."), *SubjectName.ToString());
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

    // Warm caches and make sure the next Apply publishes remapped names
    RefreshStaticMapsFromSettings();
    bForceStaticNext = true;
    bEnsuredDefaults = true;
}
