// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMTranslator.h"
#include "VRMInterchangeLog.h"
#include "VRMDocument.h"
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

// Include payload types here (keep header light)
#include "Mesh/InterchangeMeshPayload.h"
#include "Texture/InterchangeTexturePayloadData.h"

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
    // The file is read and parsed once (P3.3); the model below is built from that document.
    FString LoadError;
    const TSharedPtr<const FVRMDocument> Document = FVRMDocument::LoadFile(GetSourceData()->GetFilename(), LoadError);
    if (!Document.IsValid() || !VRM::BuildParsedModel(*Document, Parsed))
    {
        if (!LoadError.IsEmpty())
        {
            UE_LOG(LogVRMInterchange, Error, TEXT("[VRMInterchange] %s"), *LoadError);
        }
        UE_LOG(LogVRMInterchange, Error, TEXT("[VRMInterchange] Failed to read VRM."));
        return false;
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
    TexturePayloadKeys.Reset();
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
        TexturePayloadKeys.Add(TextureKey);
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

    // Create Material Instances:
    // - One character-level MI parented to the master material
    // - One MI per VRM material parented to the master material
    const FString CharacterName = FPaths::GetBaseFilename(GetSourceData()->GetFilename());

    // Character-level MI
    const FString CharacterMIDisplayName = FString::Printf(TEXT("MI_VRM_%s"), *CharacterName);
    const FString CharacterMIUid = MakeNodeUid(TEXT("MI_Character"));
    UInterchangeMaterialInstanceNode* CharacterMINode = NewObject<UInterchangeMaterialInstanceNode>(&NodeContainer);
    NodeContainer.SetupNode(CharacterMINode, CharacterMIUid, *CharacterMIDisplayName, EInterchangeNodeContainerType::TranslatedAsset);

    // Parent to master material with safe path fallback (verified API only)
    {
        bool bParentSet = false;
        const FString MasterShortPath = TEXT("/VRMInterchange/Materials/M_VRM_Master");
        const FString MasterFullPath  = TEXT("/VRMInterchange/Materials/M_VRM_Master.M_VRM_Master");

        bParentSet = CharacterMINode->SetCustomParent(MasterShortPath);
        if (!bParentSet)
        {
            bParentSet = CharacterMINode->SetCustomParent(MasterFullPath);
        }
        if (!bParentSet)
        {
            UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] Failed to set Character MI parent. Tried '%s' and '%s'"), *MasterShortPath, *MasterFullPath);
        }
    }

    // Per-material MIs parented to the master material (not the character MI)
    TArray<FString> MaterialNodeUids;
    MaterialNodeUids.SetNum(Parsed.Materials.Num());
    for (int32 mi = 0; mi < Parsed.Materials.Num(); ++mi)
    {
        const auto& M = Parsed.Materials[mi];
        const FString MatDisplayName = M.Name.IsEmpty() ? FString::Printf(TEXT("VRM_Mat_%d"), mi) : M.Name;
        const FString PerMIDisplayName = FString::Printf(TEXT("MI_VRM_%s_%s"), *CharacterName, *MatDisplayName);

        const FString MatMIUid = MakeNodeUid(*FString::Printf(TEXT("MI_%d"), mi));
        MaterialNodeUids[mi] = MatMIUid;

        UInterchangeMaterialInstanceNode* MatMINode = NewObject<UInterchangeMaterialInstanceNode>(&NodeContainer);
        NodeContainer.SetupNode(MatMINode, MatMIUid, *PerMIDisplayName, EInterchangeNodeContainerType::TranslatedAsset);

        // Assign texture parameters, each to the texture made for that use
        auto BindTexture = [&](const TCHAR* Parameter, int32 ImageIndex, VRM::ETextureUsage Usage)
        {
            if (TextureNodeUids.IsValidIndex(ImageIndex))
            {
                if (const FString* Uid = TextureNodeUids[ImageIndex].Find(Usage))
                {
                    MatMINode->AddTextureParameterValue(Parameter, *Uid);
                }
            }
        };
        BindTexture(TEXT("BaseColorTexture"), M.BaseColorTexture, VRM::ETextureUsage::Color);
        BindTexture(TEXT("NormalTexture"), M.NormalTexture, VRM::ETextureUsage::Normal);
        BindTexture(TEXT("ORMTexture"), M.MetallicRoughnessTexture, VRM::ETextureUsage::Data);
        BindTexture(TEXT("OcclusionTexture"), M.OcclusionTexture, VRM::ETextureUsage::Data);
        BindTexture(TEXT("EmissiveTexture"), M.EmissiveTexture, VRM::ETextureUsage::Color);
        // Set "Has ORM Texture?" boolean based on presence of ORM (MetallicRoughness) texture
        {
            const bool bHasORM = (M.MetallicRoughnessTexture != INDEX_NONE);
            MatMINode->AddStaticSwitchParameterValue(TEXT("Has ORM Texture?"), bHasORM);
        }
        // Set "Has Emissive Texture?" boolean based on presence of Emissive texture
        {
            const bool bHasEmissive = (M.EmissiveTexture != INDEX_NONE);
            MatMINode->AddStaticSwitchParameterValue(TEXT("Has Emissive Texture?"), bHasEmissive);
        }

        // Parent per-material MI to the master material (verified API)
        {
            bool bMatParentSet = false;
            const FString MasterShortPath = TEXT("/VRMInterchange/Materials/M_VRM_Master");
            const FString MasterFullPath  = TEXT("/VRMInterchange/Materials/M_VRM_Master.M_VRM_Master");

            bMatParentSet = MatMINode->SetCustomParent(MasterShortPath);
            if (!bMatParentSet)
            {
                bMatParentSet = MatMINode->SetCustomParent(MasterFullPath);
            }
            if (!bMatParentSet)
            {
                UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] Failed to set MI '%s' parent to master. Tried '%s' and '%s'"),
                    *PerMIDisplayName, *MasterShortPath, *MasterFullPath);
            }
        }
    }

    // Name base color textures after their material (suffix _DIFFUSE)
#if WITH_EDITORONLY_DATA
    auto SetTexName = [&](int32 ImageIndex, VRM::ETextureUsage Usage, const FString& Base, const TCHAR* Suffix)
    {
        if (!TextureNodeUids.IsValidIndex(ImageIndex)) return;
        const FString* FoundUid = TextureNodeUids[ImageIndex].Find(Usage);
        if (!FoundUid) return;
        const FString TexUid = *FoundUid;
        const FString TexFactoryUid = UInterchangeTexture2DFactoryNode::GetTextureFactoryNodeUidFromTextureNodeUid(TexUid);
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
    }
#endif

    // Mesh asset node (SKELETAL)
    const FString MeshNodeUid = MakeNodeUid(TEXT("Mesh_0"));
    UInterchangeMeshNode* MeshNode = NewObject<UInterchangeMeshNode>(&NodeContainer);
    NodeContainer.SetupNode(MeshNode, MeshNodeUid, TEXT("VRM_Mesh"), EInterchangeNodeContainerType::TranslatedAsset);

    MeshPayloadKey = TEXT("VRM_Mesh_0");
    MeshNode->SetPayLoadKey(MeshPayloadKey, EInterchangeMeshPayLoadType::SKELETAL);
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
    const UE::Interchange::FAttributeStorage& PayloadAttributes) const
{
    using namespace UE::Interchange;

    FTransform MeshGlobalTransform = FTransform::Identity;
    PayloadAttributes.GetAttribute(UE::Interchange::FAttributeKey{ MeshPayload::Attributes::MeshGlobalTransform }, MeshGlobalTransform);

    const auto& PMesh = Parsed.Mesh;

    if (PayLoadKey.Type == EInterchangeMeshPayLoadType::MORPHTARGET)
    {
        const FString Unique = PayLoadKey.UniqueId;
        int32 MorphIndex = INDEX_NONE;

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

        if (MorphIndex == INDEX_NONE || !PMesh.Morphs.IsValidIndex(MorphIndex))
        {
            UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] Invalid morph payload request '%s' (index %d out of range)"), *Unique, MorphIndex);
            return TOptional<UE::Interchange::FMeshPayloadData>();
        }

        const FVRMParsedMorph& PMorph = PMesh.Morphs[MorphIndex];

        FMeshPayloadData Data;
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

        TArray<FVertexID> Vertices;
        Vertices.Reserve(PMesh.Positions.Num());

        const bool bHaveDeltas = PMorph.DeltaPositions.Num() == PMesh.Positions.Num();

        for (int32 vi = 0; vi < PMesh.Positions.Num(); ++vi)
        {
            const FVector3f BaseP = PMesh.Positions[vi];
            const FVector3f FinalP = bHaveDeltas ? (BaseP + PMorph.DeltaPositions[vi]) : BaseP;
            const FVertexID V = MD.CreateVertex();
            VertexPositions[V] = FinalP;
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

            if (PMesh.Normals.IsValidIndex(i0)) VertexInstanceNormals[VI0] = PMesh.Normals[i0];
            if (PMesh.Normals.IsValidIndex(i1)) VertexInstanceNormals[VI1] = PMesh.Normals[i1];
            if (PMesh.Normals.IsValidIndex(i2)) VertexInstanceNormals[VI2] = PMesh.Normals[i2];

            if (PMesh.UV0.IsValidIndex(i0)) VertexInstanceUVs.Set(VI0, 0, PMesh.UV0[i0]);
            if (PMesh.UV0.IsValidIndex(i1)) VertexInstanceUVs.Set(VI1, 0, PMesh.UV0[i1]);
            if (PMesh.UV0.IsValidIndex(i2)) VertexInstanceUVs.Set(VI2, 0, PMesh.UV0[i2]);

            const int32 MatIndex = PMesh.TriMaterialIndex.IsValidIndex(t) ? PMesh.TriMaterialIndex[t] : 0;
            const FPolygonGroupID PG = GetPGForMat(MatIndex);

            MD.CreateTriangle(PG, { VI0, VI1, VI2 });
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
        const int32 NumVerts = MD.Vertices().Num();
        UE::AnimationCore::FBoneWeightsSettings Settings;
        Settings.SetNormalizeType(UE::AnimationCore::EBoneWeightNormalizeType::Always);

        if (PMesh.SkinWeights.Num() == NumVerts && Data.JointNames.Num() > 0)
        {
            for (int32 vi = 0; vi < NumVerts; ++vi)
            {
                const FVRMParsedMesh::FWeight& W = PMesh.SkinWeights[vi];
                TArray<UE::AnimationCore::FBoneWeight> BW;
                BW.Reserve(4);
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

        UE_LOG(LogVRMInterchange, Verbose, TEXT("[VRMInterchange] Returning MORPHTARGET payload for index %d"), MorphIndex);
        return Data;
    }

    // Base mesh payload
    {
        FMeshPayloadData Data;
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

        TArray<FVertexID> Vertices;
        Vertices.Reserve(PMesh.Positions.Num());
        for (const FVector3f& P : PMesh.Positions)
        {
            const FVertexID V = MD.CreateVertex();
            VertexPositions[V] = P;
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

            if (PMesh.Normals.IsValidIndex(i0)) VertexInstanceNormals[VI0] = PMesh.Normals[i0];
            if (PMesh.Normals.IsValidIndex(i1)) VertexInstanceNormals[VI1] = PMesh.Normals[i1];
            if (PMesh.Normals.IsValidIndex(i2)) VertexInstanceNormals[VI2] = PMesh.Normals[i2];

            if (PMesh.UV0.IsValidIndex(i0)) VertexInstanceUVs.Set(VI0, 0, PMesh.UV0[i0]);
            if (PMesh.UV0.IsValidIndex(i1)) VertexInstanceUVs.Set(VI1, 0, PMesh.UV0[i1]);
            if (PMesh.UV0.IsValidIndex(i2)) VertexInstanceUVs.Set(VI2, 0, PMesh.UV0[i2]);

            const int32 MatIndex = PMesh.TriMaterialIndex.IsValidIndex(t) ? PMesh.TriMaterialIndex[t] : 0;
            const FPolygonGroupID PG = GetPGForMat(MatIndex);
            MD.CreateTriangle(PG, { VI0, VI1, VI2 });
        }

        FSkinWeightsVertexAttributesRef SkinWeights = SkelAttrs.GetVertexSkinWeights();

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

        UE::AnimationCore::FBoneWeightsSettings Settings;
        Settings.SetNormalizeType(UE::AnimationCore::EBoneWeightNormalizeType::Always);

        const int32 NumVerts = MD.Vertices().Num();
        if (PMesh.SkinWeights.Num() == NumVerts && Data.JointNames.Num() > 0)
        {
            for (int32 vi = 0; vi < NumVerts; ++vi)
            {
                const FVRMParsedMesh::FWeight& W = PMesh.SkinWeights[vi];
                TArray<UE::AnimationCore::FBoneWeight> BW;
                BW.Reserve(4);
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

        return Data;
    }
}

// ===== Texture Payload Interface (UE 5.6) =====
TOptional<UE::Interchange::FImportImage> UVRMTranslator::GetTexturePayloadData(const FString& PayloadKey, TOptional<FString>& /*AlternateTexturePath*/) const
{
    // Keys are "Tex_<image>" (colour), "Tex_<image>_Normal" or "Tex_<image>_Data"; see Translate.
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

        TArray<uint8> BGRA8;
        if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, BGRA8))
        {
            return {};
        }

        const bool bNormal = EnumHasAnyFlags(Usage, ETextureUsage::Normal);
        const bool bData = EnumHasAnyFlags(Usage, ETextureUsage::Data);
        if (bNormal)
        {
            // glTF normal maps are +Y (OpenGL); Unreal expects -Y (DirectX).
            for (int32 i = 1; i < BGRA8.Num(); i += 4)
            {
                BGRA8[i] = 255 - BGRA8[i];
            }
        }

        FImportImage Image;
        Image.Init2DWithParams(Wrapper->GetWidth(), Wrapper->GetHeight(), /*NumMips*/ 1, ETextureSourceFormat::TSF_BGRA8, /*bSRGB*/ !(bNormal || bData));
        Image.CompressionSettings = bNormal ? TC_Normalmap : (bData ? TC_Masks : TC_Default);

        TArrayView64<uint8> Dest = Image.GetArrayViewOfRawData();
        if (Dest.Num() != BGRA8.Num())
        {
            return {};
        }
        FMemory::Memcpy(Dest.GetData(), BGRA8.GetData(), BGRA8.Num());
        return Image;
    }
}

// ===== Helpers =====

FString UVRMTranslator::MakeNodeUid(const TCHAR* Suffix) const
{
    const FString Base = FPaths::GetBaseFilename(GetSourceData()->GetFilename());
    return FString::Printf(TEXT("VRM_%s_%s"), *Base, Suffix);
}
