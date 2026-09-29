// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMTranslator.h"
#include "VRMImportMessages.h"
#include "VRMInterchangeLog.h"
#include "VRMDocument.h"
#include "InterchangeVRMNode.h"
#include "VRMAvatarParser.h"
#include "InterchangeSourceData.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "InterchangeSceneNode.h"

// Use Node APIs available in UE 5.6
#include "InterchangeMeshNode.h"
#include "InterchangeSkeletalMeshFactoryNode.h"
#include "InterchangeSkeletalMeshLodDataNode.h"
#include "InterchangeSkeletonFactoryNode.h"
#include "InterchangeMaterialInstanceNode.h"
#include "InterchangeMaterialFactoryNode.h"
#include "InterchangeShaderGraphNode.h"
#include "InterchangeTexture2DNode.h"
#include "InterchangeTexture2DFactoryNode.h"
#include "InterchangeMaterialDefinitions.h"
#include "VRMMToonParameters.h"

// Include payload types here (keep header light)
#include "Mesh/InterchangeMeshPayload.h"
#include "Texture/InterchangeTexturePayloadData.h"
#include "Memory/SharedBuffer.h"

#include "ImageUtils.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"

#include "StaticMeshAttributes.h"
#include "SkeletalMeshAttributes.h"
#include "BoneWeights.h"

TArray<FString> UVRMTranslator::GetSupportedFormats() const
{
    return { TEXT("vrm;VRM Avatar") };
}

EInterchangeTranslatorAssetType UVRMTranslator::GetSupportedAssetTypes() const
{
    // We import scenes that include meshes, skeletons and textures (no animations)
    return EInterchangeTranslatorAssetType::Meshes | EInterchangeTranslatorAssetType::Textures;
}

bool UVRMTranslator::CanImportSourceData(const UInterchangeSourceData* InSourceData) const
{
    const FString Ext = FPaths::GetExtension(InSourceData->GetFilename()).ToLower();
    return (Ext == TEXT("vrm"));
}

bool UVRMTranslator::Translate(UInterchangeBaseNodeContainer& NodeContainer) const
{
    // What the import logs from here on is also shown on a message log page when it finishes (P6.3).
    const FString SourceFile = GetSourceData() ? GetSourceData()->GetFilename() : FString();
    VRM::ImportMessages::Begin(SourceFile);
    const VRM::ImportMessages::FScope MessageScope(SourceFile);
    // The file is read and parsed once (P3.3). The model is built from that document, and the
    // document's JSON and hash go to the pipelines in a UInterchangeVRMNode.
    ParsedModel.Reset();
    {
        FScopeLock Lock(&BasePayloadLock);
        BasePayload.Reset();
    }
    FString LoadError;
    const TSharedPtr<const FVRMDocument> Document = FVRMDocument::LoadFile(GetSourceData()->GetFilename(), LoadError);
    const TSharedRef<FVRMParsedModel> Model = MakeShared<FVRMParsedModel>();
    if (!Document.IsValid() || !VRM::BuildParsedModel(*Document, *Model))
    {
        if (!LoadError.IsEmpty())
        {
            UE_LOG(LogVRMInterchange, Error, TEXT("[VRMInterchange] %s"), *LoadError);
        }
        UE_LOG(LogVRMInterchange, Error, TEXT("[VRMInterchange] Failed to read VRM."));
        VRM::ImportMessages::Take(SourceFile, /*bIncludeUnscoped*/ false); // no pipeline will report this import
        return false;
    }
    // Read-only from here on; the payload calls share it, possibly from other threads.
    ParsedModel = Model;
    const FVRMParsedModel& Parsed = *Model;

    UInterchangeVRMNode* VRMNode = NewObject<UInterchangeVRMNode>(&NodeContainer);
    NodeContainer.SetupNode(VRMNode, MakeNodeUid(TEXT("Document")), TEXT("VRM_Document"), EInterchangeNodeContainerType::TranslatedAsset);
    VRMNode->SetFromDocument(*Document);

    // The avatar description (P4.1): humanoid map, expressions, meta. The pipelines make the asset.
    FVRMAvatarData Avatar;
    TArray<FString> AvatarWarnings;
    if (VRM::BuildAvatarData(*Document, Parsed, Avatar, &AvatarWarnings))
    {
        VRMNode->SetAvatarData(Avatar);
    }
    for (const FString& AvatarWarning : AvatarWarnings)
    {
        UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] %s"), *AvatarWarning);
    }

    // Folder subpaths
    const FString PackageSubPath = FPaths::GetBaseFilename(GetSourceData()->GetFilename());
    const FString TexturesSubPath = PackageSubPath / TEXT("Textures");
    const FString MaterialsSubPath = PackageSubPath / TEXT("Materials");

    // Scene root
    const FString SceneNodeUid = MakeNodeUid(TEXT("Scene"));
    UInterchangeSceneNode* SceneNode = NewObject<UInterchangeSceneNode>(&NodeContainer);
    NodeContainer.SetupNode(SceneNode, SceneNodeUid, TEXT("VRMScene"), EInterchangeNodeContainerType::TranslatedScene);
    SceneNode->SetCustomLocalTransform(&NodeContainer, FTransform::Identity);

    // Create skeleton root joint in the scene
    const FString RootJointUid = MakeNodeUid(TEXT("Joint_Root"));
    UInterchangeSceneNode* RootJointNode = NewObject<UInterchangeSceneNode>(&NodeContainer);
    NodeContainer.SetupNode(RootJointNode, RootJointUid, TEXT("VRM_Root"), EInterchangeNodeContainerType::TranslatedScene, SceneNodeUid);
    RootJointNode->SetCustomLocalTransform(&NodeContainer, FTransform::Identity);
    RootJointNode->SetCustomBindPoseLocalTransform(&NodeContainer, FTransform::Identity);
    RootJointNode->AddSpecializedType(UE::Interchange::FSceneNodeStaticData::GetJointSpecializeTypeString());

    // Build joint hierarchy from parsed bones (if any)
    TArray<FString> BoneUids;
    BoneUids.SetNum(Parsed.Bones.Num());
    for (int32 bi = 0; bi < Parsed.Bones.Num(); ++bi)
    {
        const FVRMParsedBone& B = Parsed.Bones[bi];
        const FString BoneUid = MakeNodeUid(*FString::Printf(TEXT("Joint_%d"), bi));
        BoneUids[bi] = BoneUid;
        UInterchangeSceneNode* BoneNode = NewObject<UInterchangeSceneNode>(&NodeContainer);
        const FString ParentUid = (B.Parent == INDEX_NONE) ? RootJointUid : BoneUids[B.Parent];
        NodeContainer.SetupNode(BoneNode, BoneUid, *B.Name, EInterchangeNodeContainerType::TranslatedScene, ParentUid);
        BoneNode->SetCustomLocalTransform(&NodeContainer, B.LocalBind);
        BoneNode->SetCustomBindPoseLocalTransform(&NodeContainer, B.LocalBind);
        BoneNode->AddSpecializedType(UE::Interchange::FSceneNodeStaticData::GetJointSpecializeTypeString());
    }

    // Textures: one node (and texture asset) per image and use. Colour space and compression depend
    // on the use (VRM::ETextureUsage), so an image a material uses both as colour and as data gets
    // two textures rather than one with the wrong settings for half its uses.
    const TArray<VRM::ETextureUsage> Usages = VRM::ComputeTextureUsages(Parsed);
    TArray<TMap<VRM::ETextureUsage, FString>> TextureNodeUids; // per image: use -> texture node uid
    TextureNodeUids.SetNum(Parsed.Images.Num());
    const VRM::ETextureUsage AllUsages[] = { VRM::ETextureUsage::Color, VRM::ETextureUsage::Normal, VRM::ETextureUsage::Data };
    for (int32 ti = 0; ti < Parsed.Images.Num(); ++ti)
    for (const VRM::ETextureUsage Usage : AllUsages)
    {
        // An unused image is still imported, as colour.
        const VRM::ETextureUsage ImageUsages = Usages[ti] == VRM::ETextureUsage::None ? VRM::ETextureUsage::Color : Usages[ti];
        if (!EnumHasAnyFlags(ImageUsages, Usage))
        {
            continue;
        }
        const TCHAR* UsageSuffix = Usage == VRM::ETextureUsage::Normal ? TEXT("_Normal") : (Usage == VRM::ETextureUsage::Data ? TEXT("_Data") : TEXT(""));
        const FString TextureKey = FString::Printf(TEXT("Tex_%d%s"), ti, UsageSuffix);
        UInterchangeTexture2DNode* TexNode = NewObject<UInterchangeTexture2DNode>(&NodeContainer);
        const FString TexUid = MakeNodeUid(*TextureKey);
        TextureNodeUids[ti].Add(Usage, TexUid);
        NodeContainer.SetupNode(TexNode, TexUid, *FString::Printf(TEXT("VRM_Tex_%d%s"), ti, UsageSuffix), EInterchangeNodeContainerType::TranslatedAsset);
        TexNode->SetPayLoadKey(TextureKey);

#if WITH_EDITORONLY_DATA
        const FString TexFactoryUid = UInterchangeTexture2DFactoryNode::GetTextureFactoryNodeUidFromTextureNodeUid(TexUid);
        UInterchangeTexture2DFactoryNode* TexFactory = Cast<UInterchangeTexture2DFactoryNode>(NodeContainer.GetFactoryNode(TexFactoryUid));
        if (!TexFactory)
        {
            TexFactory = NewObject<UInterchangeTexture2DFactoryNode>(&NodeContainer);
            TexFactory->InitializeTextureNode(TexFactoryUid, TexNode->GetDisplayLabel(), TexNode->GetDisplayLabel(), &NodeContainer);
        }
        TexFactory->SetCustomTranslatedTextureNodeUid(TexUid);
        TexFactory->AddTargetNodeUid(TexUid);
        TexNode->AddTargetNodeUid(TexFactory->GetUniqueID());
        TexFactory->SetCustomSubPath(TexturesSubPath);
#endif
    }

    // Material instances (P4.5). MToon and unlit materials use the MToon master the editor module
    // generates (VRMMToonParameters.h), glTF PBR materials use M_VRM_Master. Each master used gets
    // a character-level instance: MI_VRM_<Character> (M_VRM_Master) and MI_VRM_<Character>__MToon.
    // The material pipeline parents the per-material instances to the one with their master, so
    // shared parameters can be tuned in one place. A model with MToon outlines also gets
    // MI_VRM_<Character>__Outline, which the pipeline makes the mesh's overlay material.
    const FString CharacterName = FPaths::GetBaseFilename(GetSourceData()->GetFilename());
    const TCHAR* const MasterPath = TEXT("/VRMInterchange/Materials/M_VRM_Master");
    auto UsesMToonMaster = [](const FVRMParsedMaterial& M) { return M.bMToon || M.bUnlit; };

    auto MakeInstance = [&](const FString& Key, const FString& DisplayName, const TCHAR* ParentPath)
    {
        UInterchangeMaterialInstanceNode* Node = NewObject<UInterchangeMaterialInstanceNode>(&NodeContainer);
        NodeContainer.SetupNode(Node, MakeNodeUid(*Key), *DisplayName, EInterchangeNodeContainerType::TranslatedAsset);
        if (!Node->SetCustomParent(ParentPath))
        {
            UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] Failed to set the parent of '%s' to '%s'."), *DisplayName, ParentPath);
        }
        return Node;
    };
    auto BindTexture = [&](UInterchangeMaterialInstanceNode& Node, const TCHAR* Parameter, int32 ImageIndex, VRM::ETextureUsage Usage)
    {
        if (TextureNodeUids.IsValidIndex(ImageIndex))
        {
            if (const FString* Uid = TextureNodeUids[ImageIndex].Find(Usage))
            {
                Node.AddTextureParameterValue(Parameter, *Uid);
            }
        }
    };
    auto SetUVTransform = [](UInterchangeMaterialInstanceNode& Node, const FVRMTextureTransform& UV)
    {
        namespace P = VRM::MToon::Param;
        Node.AddScalarParameterValue(P::UVOffsetU, UV.Offset.X);
        Node.AddScalarParameterValue(P::UVOffsetV, UV.Offset.Y);
        Node.AddScalarParameterValue(P::UVScaleU, UV.Scale.X);
        Node.AddScalarParameterValue(P::UVScaleV, UV.Scale.Y);
        Node.AddScalarParameterValue(P::UVRotation, UV.Rotation);
    };

    bool bAnyMToonMaster = false;
    bool bAnyPbr = false;
    const FVRMParsedMaterial* OutlineSource = nullptr; // the MToon material with the widest outline
    for (const FVRMParsedMaterial& M : Parsed.Materials)
    {
        (UsesMToonMaster(M) ? bAnyMToonMaster : bAnyPbr) = true;
        if (M.bMToon && M.MToon.HasOutline() && (!OutlineSource || M.MToon.OutlineWidth > OutlineSource->MToon.OutlineWidth))
        {
            OutlineSource = &M;
        }
    }

    if (bAnyPbr || !bAnyMToonMaster)
    {
        MakeInstance(TEXT("MI_Character"), FString::Printf(TEXT("MI_VRM_%s"), *CharacterName), MasterPath);
    }
    if (bAnyMToonMaster)
    {
        MakeInstance(TEXT("MI_Character_MToon"), FString::Printf(TEXT("MI_VRM_%s__MToon"), *CharacterName), VRM::MToon::SurfacePath);
    }
    if (OutlineSource)
    {
        namespace P = VRM::MToon::Param;
        const FVRMMToon& T = OutlineSource->MToon;
        UInterchangeMaterialInstanceNode* Outline = MakeInstance(TEXT("MI_Character_Outline"), FString::Printf(TEXT("MI_VRM_%s__Outline"), *CharacterName), VRM::MToon::OutlinePath);
        Outline->AddScalarParameterValue(P::OutlineWidthFactor, T.OutlineWidth);
        Outline->AddScalarParameterValue(P::OutlineScreenCoordinates, T.OutlineWidthMode == EVRMOutlineWidthMode::ScreenCoordinates ? 1.f : 0.f);
        Outline->AddVectorParameterValue(P::OutlineColorFactor, T.OutlineColor);
        Outline->AddScalarParameterValue(P::OutlineLightingMixFactor, T.OutlineLightingMix);
        BindTexture(*Outline, P::OutlineWidthMultiplyTexture, T.OutlineWidthMultiplyTexture, VRM::ETextureUsage::Data);
        SetUVTransform(*Outline, OutlineSource->UVTransform);
    }

    // Per-material instances, parented to their master (the pipeline reparents them)
    TArray<FString> MaterialNodeUids;
    MaterialNodeUids.SetNum(Parsed.Materials.Num());
    for (int32 mi = 0; mi < Parsed.Materials.Num(); ++mi)
    {
        const FVRMParsedMaterial& M = Parsed.Materials[mi];
        const FString MatDisplayName = M.Name.IsEmpty() ? FString::Printf(TEXT("VRM_Mat_%d"), mi) : M.Name;
        const FString PerMIDisplayName = FString::Printf(TEXT("MI_VRM_%s_%s"), *CharacterName, *MatDisplayName);
        const FString Key = FString::Printf(TEXT("MI_%d"), mi);
        MaterialNodeUids[mi] = MakeNodeUid(*Key);

        if (!UsesMToonMaster(M))
        {
            // glTF PBR on M_VRM_Master, as before P4.5: textures only (the master has no factor parameters)
            UInterchangeMaterialInstanceNode* Node = MakeInstance(Key, PerMIDisplayName, MasterPath);
            BindTexture(*Node, TEXT("BaseColorTexture"), M.BaseColorTexture, VRM::ETextureUsage::Color);
            BindTexture(*Node, TEXT("NormalTexture"), M.NormalTexture, VRM::ETextureUsage::Normal);
            BindTexture(*Node, TEXT("ORMTexture"), M.MetallicRoughnessTexture, VRM::ETextureUsage::Data);
            BindTexture(*Node, TEXT("OcclusionTexture"), M.OcclusionTexture, VRM::ETextureUsage::Data);
            BindTexture(*Node, TEXT("EmissiveTexture"), M.EmissiveTexture, VRM::ETextureUsage::Color);
            Node->AddStaticSwitchParameterValue(TEXT("Has ORM Texture?"), M.MetallicRoughnessTexture != INDEX_NONE);
            Node->AddStaticSwitchParameterValue(TEXT("Has Emissive Texture?"), M.EmissiveTexture != INDEX_NONE);
            continue;
        }

        namespace P = VRM::MToon::Param;
        UInterchangeMaterialInstanceNode* Node = MakeInstance(Key, PerMIDisplayName, VRM::MToon::SurfacePath);

        // Shared by MToon and unlit
        BindTexture(*Node, P::BaseColorTexture, M.BaseColorTexture, VRM::ETextureUsage::Color);
        Node->AddVectorParameterValue(P::BaseColorFactor, M.BaseColorFactor);
        BindTexture(*Node, P::EmissiveTexture, M.EmissiveTexture, VRM::ETextureUsage::Color);
        Node->AddVectorParameterValue(P::EmissiveFactor, M.EmissiveFactor);
        Node->AddScalarParameterValue(P::AlphaMode, float(uint8(M.AlphaMode)));
        Node->AddScalarParameterValue(P::AlphaCutoff, M.AlphaCutoff);
        Node->AddScalarParameterValue(P::DoubleSided, M.bDoubleSided ? 1.f : 0.f);
        SetUVTransform(*Node, M.UVTransform);

        if (!M.bMToon)
        {
            // KHR_materials_unlit or a VRM 0.x Unlit shader (MToon wins when a material has both)
            Node->AddStaticSwitchParameterValue(P::UnlitShading, true);
            continue;
        }

        const FVRMMToon& T = M.MToon;
        BindTexture(*Node, P::NormalTexture, M.NormalTexture, VRM::ETextureUsage::Normal);
        Node->AddScalarParameterValue(P::NormalScale, M.NormalScale);
        BindTexture(*Node, P::ShadeMultiplyTexture, T.ShadeMultiplyTexture, VRM::ETextureUsage::Color);
        Node->AddVectorParameterValue(P::ShadeColorFactor, T.ShadeColor);
        Node->AddScalarParameterValue(P::ShadingShiftFactor, T.ShadingShift);
        Node->AddScalarParameterValue(P::ShadingToonyFactor, T.ShadingToony);
        if (T.ShadingShiftTexture != INDEX_NONE)
        {
            Node->AddStaticSwitchParameterValue(P::UseShadingShiftTexture, true);
            BindTexture(*Node, P::ShadingShiftTexture, T.ShadingShiftTexture, VRM::ETextureUsage::Data);
            Node->AddScalarParameterValue(P::ShadingShiftTextureScale, T.ShadingShiftTextureScale);
        }
        // Without a texture the matcap adds nothing (spec), whatever its factor
        BindTexture(*Node, P::MatcapTexture, T.MatcapTexture, VRM::ETextureUsage::Color);
        Node->AddVectorParameterValue(P::MatcapFactor, T.MatcapTexture != INDEX_NONE ? T.MatcapColor : FLinearColor::Black);
        BindTexture(*Node, P::RimMultiplyTexture, T.RimMultiplyTexture, VRM::ETextureUsage::Color);
        Node->AddVectorParameterValue(P::ParametricRimColorFactor, T.RimColor);
        Node->AddScalarParameterValue(P::ParametricRimFresnelPowerFactor, T.RimFresnelPower);
        Node->AddScalarParameterValue(P::ParametricRimLiftFactor, T.RimLift);
        Node->AddScalarParameterValue(P::RimLightingMixFactor, T.RimLightingMix);
    }

    // Name base color textures after their material (suffix _DIFFUSE)
#if WITH_EDITORONLY_DATA
    // A texture used several ways (ORM and AO from one image, or one image in several materials)
    // keeps the first name it gets, so the name doesn't depend on which use came last.
    TSet<FString> NamedTextureFactories;
    auto SetTexName = [&](int32 ImageIndex, VRM::ETextureUsage Usage, const FString& Base, const TCHAR* Suffix)
    {
        if (!TextureNodeUids.IsValidIndex(ImageIndex)) return;
        const FString* FoundUid = TextureNodeUids[ImageIndex].Find(Usage);
        if (!FoundUid) return;
        const FString TexUid = *FoundUid;
        const FString TexFactoryUid = UInterchangeTexture2DFactoryNode::GetTextureFactoryNodeUidFromTextureNodeUid(TexUid);
        bool bAlreadyNamed = false;
        NamedTextureFactories.Add(TexFactoryUid, &bAlreadyNamed);
        if (bAlreadyNamed) return;
        if (UInterchangeTexture2DFactoryNode* TexFactory = Cast<UInterchangeTexture2DFactoryNode>(NodeContainer.GetFactoryNode(TexFactoryUid)))
        {
            TexFactory->SetDisplayLabel(Base + Suffix);
            TexFactory->SetCustomSubPath(TexturesSubPath);
        }
    };
    for (int32 mi = 0; mi < Parsed.Materials.Num(); ++mi)
    {
        const auto& M = Parsed.Materials[mi];
        const FString MatName = M.Name.IsEmpty() ? FString::Printf(TEXT("VRM_Mat_%d"), mi) : M.Name;
        SetTexName(M.BaseColorTexture, VRM::ETextureUsage::Color, MatName, TEXT("_DIFFUSE"));
        SetTexName(M.NormalTexture, VRM::ETextureUsage::Normal, MatName, TEXT("_NORMAL"));
        SetTexName(M.MetallicRoughnessTexture, VRM::ETextureUsage::Data, MatName, TEXT("_ORM"));
        SetTexName(M.OcclusionTexture, VRM::ETextureUsage::Data, MatName, TEXT("_AO"));
        SetTexName(M.EmissiveTexture, VRM::ETextureUsage::Color, MatName, TEXT("_EMISSIVE"));
        if (M.bMToon)
        {
            SetTexName(M.MToon.ShadeMultiplyTexture, VRM::ETextureUsage::Color, MatName, TEXT("_SHADE"));
            SetTexName(M.MToon.ShadingShiftTexture, VRM::ETextureUsage::Data, MatName, TEXT("_SHADINGSHIFT"));
            SetTexName(M.MToon.MatcapTexture, VRM::ETextureUsage::Color, MatName, TEXT("_MATCAP"));
            SetTexName(M.MToon.RimMultiplyTexture, VRM::ETextureUsage::Color, MatName, TEXT("_RIM"));
            SetTexName(M.MToon.OutlineWidthMultiplyTexture, VRM::ETextureUsage::Data, MatName, TEXT("_OUTLINEWIDTH"));
        }
    }
#endif

    // Mesh asset node (SKELETAL)
    const FString MeshNodeUid = MakeNodeUid(TEXT("Mesh_0"));
    UInterchangeMeshNode* MeshNode = NewObject<UInterchangeMeshNode>(&NodeContainer);
    NodeContainer.SetupNode(MeshNode, MeshNodeUid, TEXT("VRM_Mesh"), EInterchangeNodeContainerType::TranslatedAsset);

    MeshNode->SetPayLoadKey(TEXT("VRM_Mesh_0"), EInterchangeMeshPayLoadType::SKELETAL);
    MeshNode->SetSkinnedMesh(true);
    MeshNode->SetSkeletonDependencyUid(RootJointUid);

    // Assign material slots to MATERIAL NODE UIDs
    for (int32 mi = 0; mi < Parsed.Materials.Num(); ++mi)
    {
        const FString SlotName = FString::Printf(TEXT("MatSlot_%d"), mi);
        MeshNode->SetSlotMaterialDependencyUid(SlotName, MaterialNodeUids[mi]);
    }

    // Scene node that instantiates the mesh
    const FString SkelActorUid = MakeNodeUid(TEXT("SkelActor_0"));
    UInterchangeSceneNode* SkelActorNode = NewObject<UInterchangeSceneNode>(&NodeContainer);
    NodeContainer.SetupNode(SkelActorNode, SkelActorUid, TEXT("VRM_SkeletalActor"), EInterchangeNodeContainerType::TranslatedScene, SceneNodeUid);
    SkelActorNode->SetCustomAssetInstanceUid(MeshNodeUid);
    SkelActorNode->SetCustomLocalTransform(&NodeContainer, FTransform::Identity);

    // Morph target mesh nodes
    for (int32 MorphIndex = 0; MorphIndex < Parsed.Mesh.Morphs.Num(); ++MorphIndex)
    {
        const FVRMParsedMorph& PMorph = Parsed.Mesh.Morphs[MorphIndex];
        const FString MorphNodeUid = MakeNodeUid(*FString::Printf(TEXT("Morph_%d"), MorphIndex));
        const FString MorphDisplayName = PMorph.Name.IsEmpty() ? FString::Printf(TEXT("VRM_Morph_%d"), MorphIndex) : PMorph.Name;

        if (const UInterchangeMeshNode* Existing = Cast<UInterchangeMeshNode>(NodeContainer.GetNode(MorphNodeUid)))
        {
            MeshNode->SetMorphTargetDependencyUid(MorphNodeUid);
            continue;
        }

        UInterchangeMeshNode* MorphNode = NewObject<UInterchangeMeshNode>(&NodeContainer);
        NodeContainer.SetupNode(MorphNode, MorphNodeUid, *MorphDisplayName, EInterchangeNodeContainerType::TranslatedAsset);
        const FString MorphPayloadKey = FString::Printf(TEXT("VRM_Morph_%d"), MorphIndex);
        MorphNode->SetPayLoadKey(MorphPayloadKey, EInterchangeMeshPayLoadType::MORPHTARGET);
        MorphNode->SetMorphTarget(true);
        MorphNode->SetMorphTargetName(MorphDisplayName);
        MeshNode->SetMorphTargetDependencyUid(MorphNodeUid);
    }

    return true;
}

// ===== Mesh Payload Interface (UE 5.6) =====

TOptional<UE::Interchange::FMeshPayloadData> UVRMTranslator::GetMeshPayloadData(
    const FInterchangeMeshPayLoadKey& PayLoadKey,
    const UE::Interchange::FAttributeStorage& /*PayloadAttributes*/) const
{
    // Payloads are built on worker threads after Translate: file what they log under this import (P6.3).
    const VRM::ImportMessages::FScope MessageScope(GetSourceData() ? GetSourceData()->GetFilename() : FString());
    if (!ParsedModel.IsValid())
    {
        return {};
    }

    int32 MorphIndex = INDEX_NONE;
    if (PayLoadKey.Type == EInterchangeMeshPayLoadType::MORPHTARGET)
    {
        const FString& Unique = PayLoadKey.UniqueId;
        const FString Prefix(TEXT("VRM_Morph_"));
        if (Unique.StartsWith(Prefix))
        {
            const FString Suffix = Unique.RightChop(Prefix.Len());
            if (Suffix.IsNumeric())
            {
                MorphIndex = FCString::Atoi(*Suffix);
            }
        }
        else
        {
            int32 LastUnderscore = INDEX_NONE;
            if (Unique.FindLastChar(TEXT('_'), LastUnderscore) && LastUnderscore != INDEX_NONE)
            {
                const FString Suffix = Unique.Mid(LastUnderscore + 1);
                if (Suffix.IsNumeric())
                {
                    MorphIndex = FCString::Atoi(*Suffix);
                }
            }
        }

        UE_LOG(LogVRMInterchange, Verbose, TEXT("[VRMInterchange] Morph payload requested: Key='%s' ParsedIndex=%d"), *Unique, MorphIndex);
        if (!ParsedModel->Mesh.Morphs.IsValidIndex(MorphIndex))
        {
            UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] Invalid morph payload request '%s' (index %d out of range)"), *Unique, MorphIndex);
            return {};
        }
    }

    // The base payload is built once and copied: a morph target shares its topology (P5.5).
    TSharedPtr<const UE::Interchange::FMeshPayloadData> Base;
    {
        FScopeLock Lock(&BasePayloadLock);
        if (!BasePayload.IsValid())
        {
            TSharedRef<UE::Interchange::FMeshPayloadData> Built = MakeShared<UE::Interchange::FMeshPayloadData>();
            if (!VRM::BuildMeshPayload(*ParsedModel, INDEX_NONE, *Built))
            {
                return {};
            }
            BasePayload = Built;
        }
        Base = BasePayload;
    }
    if (MorphIndex == INDEX_NONE)
    {
        return *Base;
    }

    UE::Interchange::FMeshPayloadData Data;
    if (!VRM::BuildMeshPayload(*ParsedModel, MorphIndex, Data, Base.Get()))
    {
        return {};
    }
    return Data;
}

namespace VRM
{
    bool BuildMeshPayload(const FVRMParsedModel& Parsed, int32 MorphIndex, UE::Interchange::FMeshPayloadData& Data,
        const UE::Interchange::FMeshPayloadData* Base)
    {
        const FVRMParsedMesh& PMesh = Parsed.Mesh;
        const FVRMParsedMorph* PMorph = nullptr;
        if (MorphIndex != INDEX_NONE)
        {
            if (!PMesh.Morphs.IsValidIndex(MorphIndex))
            {
                return false;
            }
            PMorph = &PMesh.Morphs[MorphIndex];
        }

        // A morph target from the base payload: same vertices, triangles, UVs and weights; only
        // the positions and (with NORMAL deltas) the normals differ. The base's vertex i is model
        // vertex i, and its vertex instance c is the corner Indices[c] (both created in order below).
        if (PMorph && Base
            && Base->MeshDescription.Vertices().Num() == PMesh.Positions.Num()
            && Base->MeshDescription.VertexInstances().Num() == PMesh.Indices.Num())
        {
            Data = *Base;
            FStaticMeshAttributes Attributes(Data.MeshDescription);
            if (PMorph->DeltaPositions.Num() == PMesh.Positions.Num())
            {
                TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
                for (int32 vi = 0; vi < PMesh.Positions.Num(); ++vi)
                {
                    Positions[FVertexID(vi)] = PMesh.Positions[vi] + PMorph->DeltaPositions[vi];
                }
            }
            if (PMorph->DeltaNormals.Num() == PMesh.Normals.Num())
            {
                TArray<FVector3f> Morphed;
                Morphed.SetNumUninitialized(PMesh.Normals.Num());
                for (int32 n = 0; n < PMesh.Normals.Num(); ++n)
                {
                    Morphed[n] = (PMesh.Normals[n] + PMorph->DeltaNormals[n]).GetSafeNormal(UE_SMALL_NUMBER, PMesh.Normals[n]);
                }
                TVertexInstanceAttributesRef<FVector3f> InstanceNormals = Attributes.GetVertexInstanceNormals();
                for (int32 c = 0; c < PMesh.Indices.Num(); ++c)
                {
                    const int32 Vertex = int32(PMesh.Indices[c]);
                    if (Morphed.IsValidIndex(Vertex))
                    {
                        InstanceNormals[FVertexInstanceID(c)] = Morphed[Vertex];
                    }
                }
            }
            return true;
        }

        FMeshDescription& MD = Data.MeshDescription;
        FStaticMeshAttributes StaticAttrs(MD);
        StaticAttrs.Register();
        FSkeletalMeshAttributes SkelAttrs(MD);
        SkelAttrs.Register();

        TVertexAttributesRef<FVector3f> VertexPositions = StaticAttrs.GetVertexPositions();
        TVertexInstanceAttributesRef<FVector3f> VertexInstanceNormals = StaticAttrs.GetVertexInstanceNormals();
        TVertexInstanceAttributesRef<FVector2f> VertexInstanceUVs = StaticAttrs.GetVertexInstanceUVs();
        TPolygonGroupAttributesRef<FName> PolyGroupMatSlotNames = StaticAttrs.GetPolygonGroupMaterialSlotNames();

        VertexInstanceUVs.SetNumChannels(1);

        // A morph target moves the vertices by its deltas. Without NORMAL deltas in the file it
        // keeps the base normals (no shading change); with them, each vertex's morphed normal is
        // worked out once, not per triangle corner.
        const bool bHaveDeltas = PMorph && PMorph->DeltaPositions.Num() == PMesh.Positions.Num();
        TArray<FVector3f> MorphedNormals;
        if (PMorph && PMorph->DeltaNormals.Num() == PMesh.Normals.Num())
        {
            MorphedNormals.SetNumUninitialized(PMesh.Normals.Num());
            for (int32 n = 0; n < PMesh.Normals.Num(); ++n)
            {
                MorphedNormals[n] = (PMesh.Normals[n] + PMorph->DeltaNormals[n]).GetSafeNormal(UE_SMALL_NUMBER, PMesh.Normals[n]);
            }
        }
        const TArray<FVector3f>& Normals = MorphedNormals.Num() > 0 ? MorphedNormals : PMesh.Normals;

        TArray<FVertexID> Vertices;
        Vertices.Reserve(PMesh.Positions.Num());
        for (int32 vi = 0; vi < PMesh.Positions.Num(); ++vi)
        {
            const FVertexID V = MD.CreateVertex();
            VertexPositions[V] = bHaveDeltas ? PMesh.Positions[vi] + PMorph->DeltaPositions[vi] : PMesh.Positions[vi];
            Vertices.Add(V);
        }

        TMap<int32, FPolygonGroupID> MatToPG;
        auto GetPGForMat = [&](int32 MatIndex)->FPolygonGroupID
        {
            if (FPolygonGroupID* Found = MatToPG.Find(MatIndex)) return *Found;
            const FPolygonGroupID PG = MD.CreatePolygonGroup();
            const FName SlotName = FName(*FString::Printf(TEXT("MatSlot_%d"), MatIndex));
            PolyGroupMatSlotNames[PG] = SlotName;
            MatToPG.Add(MatIndex, PG);
            return PG;
        };

        const int32 TriCount = PMesh.Indices.Num() / 3;
        for (int32 t = 0; t < TriCount; ++t)
        {
            const int32 i0 = PMesh.Indices[t * 3 + 0];
            const int32 i1 = PMesh.Indices[t * 3 + 1];
            const int32 i2 = PMesh.Indices[t * 3 + 2];

            const FVertexInstanceID VI0 = MD.CreateVertexInstance(Vertices[i0]);
            const FVertexInstanceID VI1 = MD.CreateVertexInstance(Vertices[i1]);
            const FVertexInstanceID VI2 = MD.CreateVertexInstance(Vertices[i2]);

            if (Normals.IsValidIndex(i0)) VertexInstanceNormals[VI0] = Normals[i0];
            if (Normals.IsValidIndex(i1)) VertexInstanceNormals[VI1] = Normals[i1];
            if (Normals.IsValidIndex(i2)) VertexInstanceNormals[VI2] = Normals[i2];

            if (PMesh.UV0.IsValidIndex(i0)) VertexInstanceUVs.Set(VI0, 0, PMesh.UV0[i0]);
            if (PMesh.UV0.IsValidIndex(i1)) VertexInstanceUVs.Set(VI1, 0, PMesh.UV0[i1]);
            if (PMesh.UV0.IsValidIndex(i2)) VertexInstanceUVs.Set(VI2, 0, PMesh.UV0[i2]);

            const int32 MatIndex = PMesh.TriMaterialIndex.IsValidIndex(t) ? PMesh.TriMaterialIndex[t] : 0;
            MD.CreateTriangle(GetPGForMat(MatIndex), { VI0, VI1, VI2 });
        }

        Data.JointNames.Reset();
        if (Parsed.Bones.Num() > 0)
        {
            for (const FVRMParsedBone& B : Parsed.Bones)
            {
                Data.JointNames.Add(B.Name);
            }
        }
        else
        {
            Data.JointNames.Add(TEXT("VRM_Root"));
        }

        FSkinWeightsVertexAttributesRef SkinWeights = SkelAttrs.GetVertexSkinWeights();
        UE::AnimationCore::FBoneWeightsSettings Settings;
        Settings.SetNormalizeType(UE::AnimationCore::EBoneWeightNormalizeType::Always);

        const int32 NumVerts = MD.Vertices().Num();
        if (PMesh.SkinWeights.Num() == NumVerts && Data.JointNames.Num() > 0)
        {
            for (int32 vi = 0; vi < NumVerts; ++vi)
            {
                const FVRMParsedMesh::FWeight& W = PMesh.SkinWeights[vi];
                // At most four influences: on the stack, not a heap allocation per vertex (P5.4)
                TArray<UE::AnimationCore::FBoneWeight, TInlineAllocator<4>> BW;
                for (int32 k = 0; k < 4; ++k)
                {
                    const float w = W.Weight[k];
                    if (w > 0.0f)
                    {
                        const int32 BoneIndex = FMath::Clamp<int32>(W.BoneIndex[k], 0, Data.JointNames.Num() - 1);
                        BW.Emplace(BoneIndex, w);
                    }
                }
                SkinWeights.Set(FVertexID(vi), UE::AnimationCore::FBoneWeights::Create(BW, Settings));
            }
        }
        else
        {
            const UE::AnimationCore::FBoneWeight RootInfluence(0, 1.0f);
            const UE::AnimationCore::FBoneWeights RootBinding = UE::AnimationCore::FBoneWeights::Create({ RootInfluence });
            for (const FVertexID VertexID : MD.Vertices().GetElementIDs())
            {
                SkinWeights.Set(VertexID, RootBinding);
            }
        }
        return true;
    }
}

// ===== Texture Payload Interface (UE 5.6) =====
TOptional<UE::Interchange::FImportImage> UVRMTranslator::GetTexturePayloadData(const FString& PayloadKey, TOptional<FString>& /*AlternateTexturePath*/) const
{
    // Keys are "Tex_<image>" (colour), "Tex_<image>_Normal" or "Tex_<image>_Data"; see Translate.
    const VRM::ImportMessages::FScope MessageScope(GetSourceData() ? GetSourceData()->GetFilename() : FString());
    if (!ParsedModel.IsValid())
    {
        return {};
    }
    const FVRMParsedModel& Parsed = *ParsedModel;
    FString Rest;
    if (!PayloadKey.StartsWith(TEXT("Tex_")))
    {
        UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] Unexpected texture payload key '%s'"), *PayloadKey);
        return {};
    }
    Rest = PayloadKey.Mid(4);

    VRM::ETextureUsage Usage = VRM::ETextureUsage::Color;
    if (Rest.RemoveFromEnd(TEXT("_Normal")))
    {
        Usage = VRM::ETextureUsage::Normal;
    }
    else if (Rest.RemoveFromEnd(TEXT("_Data")))
    {
        Usage = VRM::ETextureUsage::Data;
    }
    if (!Rest.IsNumeric())
    {
        UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] Invalid texture payload key '%s'"), *PayloadKey);
        return {};
    }
    const int32 TextureIndex = FCString::Atoi(*Rest);
    if (!Parsed.Images.IsValidIndex(TextureIndex))
    {
        UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] Texture index %d out of range for payload '%s'"), TextureIndex, *PayloadKey);
        return {};
    }

    TOptional<UE::Interchange::FImportImage> Image = VRM::DecodeTextureImage(Parsed.Images[TextureIndex].PNGOrJPEGBytes, Usage);
    if (!Image.IsSet())
    {
        UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] Could not decode texture index %d ('%s')"), TextureIndex, *PayloadKey);
    }
    return Image;
}

namespace VRM
{
    TArray<ETextureUsage> ComputeTextureUsages(const FVRMParsedModel& Model)
    {
        TArray<ETextureUsage> Usages;
        Usages.Init(ETextureUsage::None, Model.Images.Num());
        auto Mark = [&Usages](int32 ImageIndex, ETextureUsage Usage)
        {
            if (Usages.IsValidIndex(ImageIndex))
            {
                Usages[ImageIndex] |= Usage;
            }
        };
        for (const FVRMParsedModel::FMat& M : Model.Materials)
        {
            Mark(M.BaseColorTexture, ETextureUsage::Color);
            Mark(M.EmissiveTexture, ETextureUsage::Color);
            Mark(M.NormalTexture, ETextureUsage::Normal);
            Mark(M.MetallicRoughnessTexture, ETextureUsage::Data);
            Mark(M.OcclusionTexture, ETextureUsage::Data);
            if (M.bMToon)
            {
                Mark(M.MToon.ShadeMultiplyTexture, ETextureUsage::Color);
                Mark(M.MToon.MatcapTexture, ETextureUsage::Color);
                Mark(M.MToon.RimMultiplyTexture, ETextureUsage::Color);
                Mark(M.MToon.ShadingShiftTexture, ETextureUsage::Data);
                Mark(M.MToon.OutlineWidthMultiplyTexture, ETextureUsage::Data);
            }
        }
        return Usages;
    }

    TOptional<UE::Interchange::FImportImage> DecodeTextureImage(const TArray64<uint8>& CompressedBytes, ETextureUsage Usage)
    {
        using namespace UE::Interchange;
        if (CompressedBytes.Num() == 0)
        {
            return {};
        }

        IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
        const EImageFormat ImageFormat = ImageWrapperModule.DetectImageFormat(CompressedBytes.GetData(), CompressedBytes.Num());
        if (ImageFormat == EImageFormat::Invalid)
        {
            return {};
        }
        TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);
        if (!Wrapper.IsValid() || !Wrapper->SetCompressed(CompressedBytes.GetData(), CompressedBytes.Num()))
        {
            return {};
        }

        const bool bNormal = EnumHasAnyFlags(Usage, ETextureUsage::Normal);
        const bool bData = EnumHasAnyFlags(Usage, ETextureUsage::Data);

        // 16-bit PNGs keep their precision as RGBA16 for linear uses (normal maps, data); colour
        // textures are sRGB, which RGBA16 sources don't take, so they stay BGRA8 (P5.6).
        const bool bKeep16 = (bNormal || bData) && ImageFormat == EImageFormat::PNG && Wrapper->GetBitDepth() == 16;
        const ERGBFormat RawFormat = bKeep16 ? ERGBFormat::RGBA : ERGBFormat::BGRA;
        const int32 BitDepth = bKeep16 ? 16 : 8;

        // Decoded once, then handed to the image without a copy (P5.6).
        TArray64<uint8> Raw;
        if (!Wrapper->GetRaw(RawFormat, BitDepth, Raw))
        {
            return {};
        }

        if (bNormal)
        {
            // glTF normal maps are +Y (OpenGL); Unreal expects -Y (DirectX).
            if (bKeep16)
            {
                uint16* Channels = reinterpret_cast<uint16*>(Raw.GetData());
                const int64 Count = Raw.Num() / int64(sizeof(uint16));
                for (int64 i = 1; i < Count; i += 4)
                {
                    Channels[i] = uint16(65535 - Channels[i]);
                }
            }
            else
            {
                for (int64 i = 1; i < Raw.Num(); i += 4)
                {
                    Raw[i] = uint8(255 - Raw[i]);
                }
            }
        }

        FImportImage Image;
        Image.Init2DWithParams(Wrapper->GetWidth(), Wrapper->GetHeight(), /*NumMips*/ 1,
            bKeep16 ? ETextureSourceFormat::TSF_RGBA16 : ETextureSourceFormat::TSF_BGRA8,
            /*bSRGB*/ !(bNormal || bData), /*bShouldAllocateRawData*/ false);
        Image.CompressionSettings = bNormal ? TC_Normalmap : (bData ? TC_Masks : TC_Default);
        const int64 Expected = int64(Wrapper->GetWidth()) * Wrapper->GetHeight() * (bKeep16 ? 8 : 4);
        if (Raw.Num() != Expected)
        {
            return {};
        }
        Image.RawData = MakeUniqueBufferFromArray(MoveTemp(Raw));
        return Image;
    }
}

// ===== Helpers =====

FString UVRMTranslator::MakeNodeUid(const TCHAR* Suffix) const
{
    const FString Base = FPaths::GetBaseFilename(GetSourceData()->GetFilename());
    return FString::Printf(TEXT("VRM_%s_%s"), *Base, Suffix);
}
