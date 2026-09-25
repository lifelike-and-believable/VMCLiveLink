// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VMCLiveLinkSource.h"
#include "VMCLog.h"
#include "VMCHumanoid.h"
#include "VMCProtocol.h"

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
    VMCHumanoid::BuildSkeleton(BoneNames, BoneParents);
    BoneIndexByName.Reset();
    for (int32 i = 0; i < BoneNames.Num(); ++i)
    {
        BoneIndexByName.Add(BoneNames[i], i);
    }
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

    auto WarnMalformed = [this, &Addr]()
    {
        if (!bWarnedMalformed)
        {
            UE_LOG(LogVMCLiveLink, Warning, TEXT("VMC source '%s': ignoring malformed %s message (unexpected argument count or types). Further malformed messages are ignored silently."),
                *SourceName, *Addr);
            bWarnedMalformed = true;
        }
    };

    if (Addr == TEXT("/VMC/Ext/Bone/Pos"))
    {
        VMCProtocol::FArgs Args;
        VMCProtocol::ReadArgs(Msg, Args);
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

        FScopeLock Lock(&DataGuard);
        if (!BoneIndexByName.Contains(BoneName))
        {
            // Not a Unity humanoid bone: append it (existing indices never move) under Hips.
            const int32 NewIndex = BoneNames.Add(BoneName);
            BoneParents.Add(VMCHumanoid::FallbackParentIndex);
            BoneIndexByName.Add(BoneName, NewIndex);
            UE_LOG(LogVMCLiveLink, Log, TEXT("VMC source '%s': bone '%s' is not a Unity humanoid bone; parenting it to Hips."),
                *SourceName, *BoneName.ToString());
            bForceStaticNext = true;
        }
        PendingPose.Add(BoneName, Xf);
    }
    else if (Addr == TEXT("/VMC/Ext/Root/Pos"))
    {
        VMCProtocol::FArgs Args;
        VMCProtocol::ReadArgs(Msg, Args);
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

        FScopeLock Lock(&DataGuard);
        PendingRoot = FTransform(QR, PR, FVector::OneVector);
    }
    else if (Addr == TEXT("/VMC/Ext/Blend/Val"))
    {
        VMCProtocol::FArgs Args;
        VMCProtocol::ReadArgs(Msg, Args);
        FString Name;
        float Val = 0.f;
        if (!VMCProtocol::ParseBlendVal(Args, Name, Val))
        {
            WarnMalformed();
            return;
        }

        const FName CurveName(*Name);

        FScopeLock Lock(&DataGuard);
        if (!CurveNameToIndex.Contains(CurveName))
        {
            const int32 NewIdx = CurveNamesOrdered.Add(CurveName);
            CurveNameToIndex.Add(CurveName, NewIdx);
            bStaticCurvesDirty = true; // advertise this name in static data
        }
        PendingCurves.Add(CurveName, Val);
    }
    else if (Addr == TEXT("/VMC/Ext/Blend/Apply"))
    {
        // OSC messages are dispatched on the Game Thread (see UOSCServer::PumpPacketQueue), so the
        // subject bootstrap can run synchronously here, before anything is pushed.
        if (!bEnsuredDefaults)
        {
            EnsureSubjectSettingsWithDefaults(); // also refreshes the cached maps
        }
        else
        {
            RefreshStaticMapsFromSettings();
        }

        // Decide on static data before building the frame, so a frame never carries more curve
        // values than the static data it is validated against. Push static at most once.
        bool bNeedStatic = false;
        {
            FScopeLock Lock(&DataGuard);
            bNeedStatic = !bStaticSent || bForceStaticNext || bStaticCurvesDirty;
            bForceStaticNext = false;
            bStaticCurvesDirty = false;
        }
        if (bNeedStatic)
        {
            PushStaticData(/*bForce=*/true);
        }

        PushFrame();

        if (bZeroMissingCurves)
        {
            FScopeLock Lock(&DataGuard);
            PendingCurves.Reset();
        }
    }
}

// ---------------- Live Link data push ----------------

void FVMCLiveLinkSource::PushStaticData(bool bForce)
{
    FScopeLock Lock(&DataGuard);

    const bool bHaveBones = BoneNames.Num() > 0;
    if (!Client || (!bForce && (bStaticSent || !bHaveBones)))
    {
        return;
    }

    // Make editable copies
    TArray<FName> OutBoneNames = BoneNames;
    TArray<FName> OutCurveNames = CurveNamesOrdered;

    // Apply cached maps (preserve order → indices remain valid)
    for (FName& N : OutBoneNames)  if (const FName* M = CachedBoneMap.Find(N))  N = *M;
    for (FName& C : OutCurveNames) if (const FName* M = CachedCurveMap.Find(C)) C = *M;

    // Build static packet
    FLiveLinkStaticDataStruct StaticData(FLiveLinkSkeletonStaticData::StaticStruct());
    auto& Skel = *StaticData.Cast<FLiveLinkSkeletonStaticData>();

    Skel.SetBoneNames(OutBoneNames);
    Skel.SetBoneParents(BoneParents);

    // UE 5.6: curve names live on the base static data array
    Skel.PropertyNames = OutCurveNames;

    Client->PushSubjectStaticData_AnyThread({ SourceGuid, SubjectName },
        ULiveLinkAnimationRole::StaticClass(), MoveTemp(StaticData));

    bStaticSent = true;
}

void FVMCLiveLinkSource::PushFrame()
{
    if (!Client) return;

    // Snapshot state under lock
    TArray<FName>            LocalBoneNames;
    TMap<FName, FTransform>  LocalPose;
    FTransform               LocalRoot = FTransform::Identity;
    TArray<FName>            LocalCurveNames;
    TMap<FName, int32>       LocalCurveNameToIndex;
    TMap<FName, float>       LocalCurves;

    TMap<FName, FName>       LocalBoneMap;                 // source → mapped
    TMap<FName, FVector>     LocalRefOffsets;              // mapped name → ref local translation
    bool                     bLocalUseRefOffsets = true;
    bool                     bLocalPreferIncoming = false;
    bool                     bLocalHaveRefOffsets = false;

    {
        FScopeLock Lock(&DataGuard);
        LocalBoneNames = BoneNames;
        LocalPose = PendingPose;
        LocalRoot = PendingRoot;
        LocalCurveNames = CurveNamesOrdered;
        LocalCurveNameToIndex = CurveNameToIndex;
        LocalCurves = PendingCurves;

        LocalBoneMap = CachedBoneMap;
        LocalRefOffsets = RefLocalTranslationByName;
        bLocalUseRefOffsets = bUseRefOffsets;
        bLocalPreferIncoming = bPreferIncomingTranslations;
        bLocalHaveRefOffsets = bHaveRefOffsets;
    }

    // Build frame payload
    FLiveLinkFrameDataStruct Frame(FLiveLinkAnimationFrameData::StaticStruct());
    auto& Anim = *Frame.Cast<FLiveLinkAnimationFrameData>();
    FLiveLinkBaseFrameData& Base = static_cast<FLiveLinkBaseFrameData&>(Anim);

    const int32 NumBones = LocalBoneNames.Num();
    const int32 NumCurves = LocalCurveNames.Num();

    Anim.Transforms.SetNum(NumBones);
    Base.PropertyValues.SetNum(NumCurves);
    for (int32 i = 0; i < NumCurves; ++i) Base.PropertyValues[i] = 0.f;

    auto MapBoneName = [&](const FName& Src)->FName
        {
            if (const FName* M = LocalBoneMap.Find(Src)) return *M;
            return Src;
        };

    // Fill transforms as LOCAL (parent-space) per Live Link Animation Role
    for (int32 i = 0; i < NumBones; ++i)
    {
        const FName SrcName = LocalBoneNames[i];
        const FTransform* In = LocalPose.Find(SrcName);
        FTransform X = FTransform::Identity;

        if (i == 0)
        {
            // Root: a Bone/Pos entry named "root" if the sender provides one, otherwise the
            // dedicated /VMC/Ext/Root/Pos stream.
            X = In ? *In : LocalRoot;
        }
        else
        {
            if (In)
            {
                X.SetRotation(In->GetRotation());
            }

            // Hips always uses the streamed translation: it carries the body's height and
            // movement relative to the root. Other bones use it only when the stream is trusted
            // to send proper local translations; otherwise the reference skeleton's offsets.
            const bool bIsHips = (i == VMCHumanoid::HipsSkeletonIndex);
            bool bHaveTranslation = false;
            if (In && (bIsHips || bLocalPreferIncoming))
            {
                X.SetTranslation(In->GetTranslation());
                bHaveTranslation = bIsHips || !X.GetTranslation().IsNearlyZero();
            }
            if (!bHaveTranslation && bLocalUseRefOffsets && bLocalHaveRefOffsets)
            {
                if (const FVector* Off = LocalRefOffsets.Find(MapBoneName(SrcName)))
                {
                    X.SetTranslation(*Off);
                }
            }
        }

        Anim.Transforms[i] = X;
    }

    // Curves → PropertyValues using fixed order
    for (const TPair<FName, float>& KV : LocalCurves)
    {
        if (const int32* Idx = LocalCurveNameToIndex.Find(KV.Key))
        {
            const int32 I = *Idx;
            if (Base.PropertyValues.IsValidIndex(I))
            {
                Base.PropertyValues[I] = KV.Value;
            }
        }
    }

    Client->PushSubjectFrameData_AnyThread({ SourceGuid, SubjectName }, MoveTemp(Frame));
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

    // Pull maps + reference mesh
    TMap<FName, FName> NewBone, NewCurve;
    USkeletalMesh* RefMesh = nullptr;

    if (NowRemapper)
    {
        NewBone = NowRemapper->BoneNameMap;

        if (const UVMCLiveLinkRemapper* My = Cast<UVMCLiveLinkRemapper>(NowRemapper))
        {
            NewCurve = My->CurveNameMap;
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

    const uint32 NewHash = HashMaps(NewBone, NewCurve);
    if (NewHash != CachedMapsHash)
    {
        CachedMapsHash = NewHash;
        CachedBoneMap = MoveTemp(NewBone);
        CachedCurveMap = MoveTemp(NewCurve);
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
