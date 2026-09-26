// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// VRMSpringBonesPostImportPipeline
// - Parses spring data while the import is set up; creates assets once the import's skeletal mesh
//   exists (UVRMPipelineBase::OnSkeletalMeshImported).
// - Creates assets (Spring data + optional post-process ABP), marks packages dirty,
//   and does NOT save during import. Let the editor Save/Source Control flow handle persistence.

#include "VRMSpringBonesPostImportPipeline.h"
#include "VRMSpringBoneData.h"                // runtime asset
#include "InterchangeSourceData.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "Misc/SecureHash.h"
#include "VRMDocument.h"
#include "InterchangeVRMNode.h"
#include "VRMSpringBonesParser.h"
#include "VRMSpringBonesValidation.h"
#include "VRMInterchangeLog.h"
#include "VRMInterchangeSettings.h"

#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"

#if WITH_EDITOR
void UVRMSpringBonesPostImportPipeline::PostInitProperties()
{
    Super::PostInitProperties();
    if (!HasAnyFlags(RF_ClassDefaultObject))
    {
        const UVRMInterchangeSettings* Settings = GetDefault<UVRMInterchangeSettings>();
        if (Settings)
        {
            bGenerateSpringBoneData     = Settings->bGenerateSpringBoneData;
            bOverwriteExisting          = Settings->bOverwriteExistingSpringAssets;
            bGeneratePostProcessAnimBP  = Settings->bGeneratePostProcessAnimBP;
            bAssignPostProcessABP       = Settings->bAssignPostProcessABP;
            bOverwriteExistingPostProcessABP = Settings->bOverwriteExistingPostProcessABP;
            bReusePostProcessABPOnReimport  = Settings->bReusePostProcessABPOnReimport;
        }
    }
}
#endif

// ExecutePipeline
// - Parse VRM spring data into a transient asset
// - Wait for this import's skeletal mesh (OnSkeletalMeshImported) to create the assets
void UVRMSpringBonesPostImportPipeline::ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath)
{
    Super::ExecutePipeline(BaseNodeContainer, SourceDatas, ContentBasePath);
    StagedSpringData.Reset();

    // Only this instance's flags count. The project settings seeded them (PostInitProperties), and the
    // import dialog may have changed them since; OR-ing the settings back in would ignore an unticked box.
    if (!BeginImport(SourceDatas, ContentBasePath) || !BaseNodeContainer)
    {
        UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMInterchange] Spring pipeline: No SourceData."));
        return;
    }
    const FString& Filename = GetSourceFilename();

    if (bGenerateSpringBoneData)
    {
        // The translator read the file and left its JSON and hash in a VRM node (P3.3), so the file is
        // not opened again. A container the VRM translator didn't make has no such node; then the
        // file is read here, once.
        FString LoadError;
        FString SourceHash;
        TSharedPtr<const FVRMDocument> Document;
        if (const UInterchangeVRMNode* VRMNode = UInterchangeVRMNode::Find(*BaseNodeContainer))
        {
            Document = VRMNode->MakeDocument(LoadError);
            VRMNode->GetSourceHash(SourceHash);
        }
        else
        {
            Document = FVRMDocument::LoadFile(Filename, LoadError);
            if (Document.IsValid())
            {
                SourceHash = LexToString(Document->GetSourceHash());
            }
        }

        UVRMSpringBoneData* TransientSpringData = nullptr;
        if (!Document.IsValid())
        {
            UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMInterchange] Spring pipeline: %s"), *LoadError);
        }
        else
        {
            TransientSpringData = NewObject<UVRMSpringBoneData>(GetTransientPackage(), NAME_None);
            if (!ParseAndFillDataAsset(*Document, TransientSpringData))
            {
                UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMInterchange] Spring pipeline: No spring data found in '%s'."), *Filename);
                TransientSpringData = nullptr;
            }
            else
            {
                int32 ResolvedC=0, ResolvedJ=0, ResolvedCenters=0;
                ResolveBoneNames(*Document, TransientSpringData->SpringConfig, ResolvedC, ResolvedJ, ResolvedCenters);
                // The parser already returns Unreal axes and centimetres (VRMCoordinateConversion.h).
                TransientSpringData->SourceFilename = Filename;
                TransientSpringData->SourceHash = SourceHash;
            }
        }
        StagedSpringData.Reset(TransientSpringData);
    }

    if (StagedSpringData.IsValid() || bGeneratePostProcessAnimBP)
    {
        WaitForSkeletalMesh();
    }
}

// ParseAndFillDataAsset
// - Parses the document's spring config into a runtime asset container
// - The container is transient here; the asset is created in OnSkeletalMeshImported
bool UVRMSpringBonesPostImportPipeline::ParseAndFillDataAsset(const FVRMDocument& Document, UVRMSpringBoneData* Dest) const
{
    if (!Dest) return false;
    const FString& Filename = Document.GetFilename();
    FVRMSpringConfig Config;
    TMap<int32, int32> NodeParent;
    TMap<int32, FVRMNodeChildren> NodeChildren;
    FString Err;
    TMap<int32, FName> NodeMap;

    if (!VRM::ParseSpringBonesFromDocument(Document, Config, NodeMap, NodeParent, NodeChildren, Err))
    {
        return false;
    }

    // Reject malformed configs (e.g. joint/collider/group indices out of range) before they ever
    // reach BuildResolvedChildren() or the runtime spring solver, both of which index into these
    // arrays without their own bounds checks.
    {
        const VRM::FVRMValidationResult Validation = VRM::ValidateSpringConfig(Config);
        for (const FString& WarningMsg : Validation.Warnings)
        {
            UE_LOG(LogVRMSpring, Warning, TEXT("[VRMInterchange] Spring pipeline: %s (%s)"), *WarningMsg, *Filename);
        }
        if (!Validation.bIsValid)
        {
            for (const FString& ErrorMsg : Validation.Errors)
            {
                UE_LOG(LogVRMSpring, Error, TEXT("[VRMInterchange] Spring pipeline: %s (%s)"), *ErrorMsg, *Filename);
            }
            UE_LOG(LogVRMSpring, Error, TEXT("[VRMInterchange] Spring pipeline: Rejecting spring bone data from '%s' due to validation errors above; skipping spring asset generation."), *Filename);
            return false;
        }
    }

    Dest->SpringConfig = MoveTemp(Config);
    if (NodeMap.Num() > 0)     { Dest->SetNodeToBoneMapping(NodeMap); }
    if (NodeParent.Num() > 0)  { Dest->NodeParent   = MoveTemp(NodeParent); }
    if (NodeChildren.Num() > 0){ Dest->NodeChildren = MoveTemp(NodeChildren); }
    if (Dest->NodeChildren.Num() > 0)
    {
        Dest->BuildResolvedChildren();
    }

    return Dest->SpringConfig.IsValid();
}

bool UVRMSpringBonesPostImportPipeline::ResolveBoneNames(const FVRMDocument& Document, FVRMSpringConfig& InOut, int32& OutResolvedColliders, int32& OutResolvedJoints, int32& OutResolvedCenters) const
{
    OutResolvedColliders = OutResolvedJoints = OutResolvedCenters = 0;
    if (!InOut.IsValid()) return false;
    for (FVRMSpringCollider& C : InOut.Colliders) if (C.BoneName.IsNone() && C.NodeIndex!=INDEX_NONE){ if(FName Nm=Document.GetNodeName(C.NodeIndex); !Nm.IsNone()){ C.BoneName=Nm; ++OutResolvedColliders; }}
    for (FVRMSpringJoint& J : InOut.Joints)    if (J.BoneName.IsNone() && J.NodeIndex!=INDEX_NONE){ if(FName Nm=Document.GetNodeName(J.NodeIndex); !Nm.IsNone()){ J.BoneName=Nm; ++OutResolvedJoints; }}
    for (FVRMSpring& S : InOut.Springs)        if (S.CenterBoneName.IsNone() && S.CenterNodeIndex!=INDEX_NONE){ if(FName Nm=Document.GetNodeName(S.CenterNodeIndex); !Nm.IsNone()){ S.CenterBoneName=Nm; ++OutResolvedCenters; }}
    return (OutResolvedColliders+OutResolvedJoints+OutResolvedCenters)>0;
}

void UVRMSpringBonesPostImportPipeline::ValidateBoneNamesAgainstSkeleton(const USkeleton* Skeleton, const FVRMSpringConfig& Config) const
{
    if (!Skeleton) return;

    const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
    TSet<FName> Valid; for(int32 i=0;i<RefSkel.GetNum();++i) Valid.Add(RefSkel.GetBoneName(i));

    auto Report=[&](const TArray<FName>& Missing,const TCHAR* What)
    {
        if(Missing.Num()>0)
        {
            UE_LOG(LogVRMSpring,Warning,TEXT("[VRMInterchange] Spring pipeline: %d %s not on skeleton '%s'"),Missing.Num(),What,*Skeleton->GetPathName());
        }
    };

    TArray<FName> MC, MJ, MCent;
    for(const FVRMSpringCollider& C:Config.Colliders) if(!C.BoneName.IsNone()&&!Valid.Contains(C.BoneName)) MC.AddUnique(C.BoneName);
    for(const FVRMSpringJoint& J:Config.Joints) if(!J.BoneName.IsNone()&&!Valid.Contains(J.BoneName)) MJ.AddUnique(J.BoneName);
    for(const FVRMSpring& S:Config.Springs) if(!S.CenterBoneName.IsNone()&&!Valid.Contains(S.CenterBoneName)) MCent.AddUnique(S.CenterBoneName);
    Report(MC,TEXT("collider BoneName(s)"));
    Report(MJ,TEXT("joint BoneName(s)"));
    Report(MCent,TEXT("center BoneName(s)"));
}

// Tries to set SpringConfig (object or soft object) on the ABP CDO and mark dirty.
bool UVRMSpringBonesPostImportPipeline::SetSpringConfigOnAnimBlueprint(UObject* AnimBlueprintObj, UVRMSpringBoneData* SpringData) const
{
    if(!AnimBlueprintObj||!SpringData) return false;

    if(UAnimBlueprint* ABP=Cast<UAnimBlueprint>(AnimBlueprintObj))
    {
        if(!ABP->GeneratedClass){ FKismetEditorUtilities::CompileBlueprint(ABP);} 
        UAnimBlueprintGeneratedClass* GenClass=Cast<UAnimBlueprintGeneratedClass>(ABP->GeneratedClass);
        if(!GenClass) return false;
        UObject* CDO=GenClass->GetDefaultObject();
        if(!CDO) return false;

        if(FObjectProperty* ObjProp=FindFProperty<FObjectProperty>(GenClass,TEXT("SpringConfig")))
        {
            ObjProp->SetObjectPropertyValue_InContainer(CDO,SpringData);
        }
        else if(FSoftObjectProperty* SoftProp=FindFProperty<FSoftObjectProperty>(GenClass,TEXT("SpringConfig")))
        {
            SoftProp->SetObjectPropertyValue_InContainer(CDO,SpringData);
        }
        else
        {
            return false;
        }
        CDO->Modify();
        CDO->MarkPackageDirty();
        ABP->MarkPackageDirty();
        return true;
    }

    if(UClass* BPClass=Cast<UClass>(AnimBlueprintObj))
    {
        UObject* CDO=BPClass->GetDefaultObject();
        if(!CDO) return false;

        if(FObjectProperty* ObjProp=FindFProperty<FObjectProperty>(BPClass,TEXT("SpringConfig")))
        {
            ObjProp->SetObjectPropertyValue_InContainer(CDO,SpringData);
        }
        else if(FSoftObjectProperty* SoftProp=FindFProperty<FSoftObjectProperty>(BPClass,TEXT("SpringConfig")))
        {
            SoftProp->SetObjectPropertyValue_InContainer(CDO,SpringData);
        }
        else
        {
            return false;
        }

        CDO->Modify();
        CDO->MarkPackageDirty();
        return true;
    }

    return false;
}

bool UVRMSpringBonesPostImportPipeline::AssignPostProcessABPToMesh(USkeletalMesh* SkelMesh, UObject* AnimBlueprintObj) const
{
    if(!SkelMesh||!AnimBlueprintObj) return false;
    UClass* BPClass=nullptr;
    if(UAnimBlueprint* ABP=Cast<UAnimBlueprint>(AnimBlueprintObj))
    {
        FKismetEditorUtilities::CompileBlueprint(ABP);
        BPClass=ABP->GeneratedClass;
    }
    else
    {
        BPClass=Cast<UClass>(AnimBlueprintObj);
    }
    if(!BPClass) return false;
    SkelMesh->SetPostProcessAnimBlueprint(BPClass);
    SkelMesh->MarkPackageDirty();
    return true;
}

// OnSkeletalMeshImported
// - Called once with this import's skeletal mesh, after the import dialog was accepted.
// - Create (or update) the spring data and the optional ABP; assign the ABP to the mesh if requested.
// - Do not save packages; leave them dirty for the editor Save/SCC flow.
void UVRMSpringBonesPostImportPipeline::OnSkeletalMeshImported(USkeletalMesh* SkelMesh, bool bIsAReimport)
{
    USkeleton* Skeleton = SkelMesh->GetSkeleton();
    const FString CharacterName = SkelMesh->GetName();

    // 1) Spring data asset
    UVRMSpringBoneData* SpringDataAsset = nullptr;
    if (StagedSpringData.IsValid())
    {
        ValidateBoneNamesAgainstSkeleton(Skeleton, StagedSpringData->SpringConfig);

        const FString SpringDataFolder = SubFolder.IsEmpty() ? GetCharacterFolder() : GetCharacterFolder() / SubFolder;
        bool bReused = false;
        SpringDataAsset = Cast<UVRMSpringBoneData>(CreateOrReuseAsset(UVRMSpringBoneData::StaticClass(), SpringDataFolder,
            CharacterName + TEXT("_SpringData"), bOverwriteExisting, bReused));
        if (SpringDataAsset)
        {
            // Replaced in place when reused, so anim nodes and ABPs pointing at it keep working.
            SpringDataAsset->SpringConfig = StagedSpringData->SpringConfig;
            SpringDataAsset->NodeParent   = StagedSpringData->NodeParent;
            SpringDataAsset->NodeChildren = StagedSpringData->NodeChildren;
            SpringDataAsset->SetNodeToBoneMapping(StagedSpringData->NodeToBoneMap);
            if (SpringDataAsset->NodeChildren.Num() > 0)
            {
                SpringDataAsset->BuildResolvedChildren();
            }
            SpringDataAsset->SourceFilename = StagedSpringData->SourceFilename;
            SpringDataAsset->SourceHash     = StagedSpringData->SourceHash;
            SpringDataAsset->bNeedsReimport = false; // freshly parsed, so current (FVRMSpringDataCustomVersion)
            if (bReused)
            {
                // A new effective hash makes running spring nodes pick up the replaced data.
                ++SpringDataAsset->EditRevision;
            }
            SpringDataAsset->MarkPackageDirty();
        }
    }

    // 2) ABP create/reuse and assignment
    if (bGeneratePostProcessAnimBP && Skeleton)
    {
        const TCHAR* TemplatePath = TEXT("/VRMInterchange/Animation/ABP_VRMSpringBones_Template.ABP_VRMSpringBones_Template");
        bool bReused = false;
        UAnimBlueprint* ABP = Cast<UAnimBlueprint>(DuplicateTemplateAsset(TemplatePath, GetCharacterFolder() / AnimationSubFolder,
            FString::Printf(TEXT("PP_ABP_VRMSpringBones_%s"), *CharacterName),
            bOverwriteExistingPostProcessABP || bReusePostProcessABPOnReimport, bReused));
        if (ABP)
        {
            if (!bReused)
            {
                ABP->TargetSkeleton = Skeleton;
                FKismetEditorUtilities::CompileBlueprint(ABP);
            }
            if (SpringDataAsset && !SetSpringConfigOnAnimBlueprint(ABP, SpringDataAsset))
            {
                UE_LOG(LogVRMSpring, Warning, TEXT("[VRMInterchange] Spring pipeline: '%s' has no SpringConfig variable to set."), *ABP->GetPathName());
            }
            if (bAssignPostProcessABP)
            {
                AssignPostProcessABPToMesh(SkelMesh, ABP);
            }
            ABP->MarkPackageDirty();
        }
    }

    StagedSpringData.Reset();
    UE_LOG(LogVRMSpring, Log, TEXT("[VRMInterchange] Spring pipeline: assets for '%s' created (not saved)."), *CharacterName);
}
