// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMTranslator.h"
#include "VRMInterchangeLog.h"
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
#include "Misc/FileHelper.h"
#include "Misc/Base64.h"

#include "StaticMeshAttributes.h"
#include "SkeletalMeshAttributes.h"
#include "BoneWeights.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

// ---- Helper forward declarations (must appear before usage)
static const cgltf_attribute* FindAttribute(const cgltf_primitive* Prim, cgltf_attribute_type Type);
static const cgltf_attribute* FindTexcoord(const cgltf_primitive* Prim, int Set);
static const cgltf_attribute* FindJoints(const cgltf_primitive* Prim, int Set);
static const cgltf_attribute* FindWeights(const cgltf_primitive* Prim, int Set);
static const cgltf_accessor* FindTargetAccessor(const cgltf_morph_target& Tgt, cgltf_attribute_type Type);

static void ReadAccessorVec3f(const cgltf_accessor& A, TArray<FVector3f>& Out);
static void ReadAccessorVec2f(const cgltf_accessor& A, TArray<FVector2f>& Out);
static void ReadIndicesUInt32(const cgltf_accessor& A, TArray<uint32>& Out);

// New validation helper (centralized checks)
static bool ValidateCgltfData(const cgltf_data* Data, FString& OutError);

// New forward for extracted image loader
static bool LoadImagesFromCgltf(const cgltf_data* Data, const FString& Filename, FVRMParsedModel& Out);

// A mesh as placed in the scene by one node. Meshes are imported once per node that uses them.
struct FVRMMeshInstance
{
    const cgltf_node* Node = nullptr;
    const cgltf_skin* Skin = nullptr;       // the node's skin, if any
    FMatrix World = FMatrix::Identity;      // node world transform in glTF space (used for rigid primitives)
    int32 RigidBone = 0;                    // bone that rigid primitives are bound to
};

// Mesh nodes of the scene, in node index order.
static TArray<FVRMMeshInstance> CollectMeshInstances(const cgltf_data* Data, const TMap<int32, int32>& NodeToBone);

// Appends every primitive of every mesh instance to Out.Mesh, in the rest pose. OutVertexToRest
// receives, per vertex, the glTF-space transform that was applied (for morph target deltas).
static bool MergeMeshInstances(const cgltf_data* Data, const TArray<FVRMMeshInstance>& Instances, const TMap<int32, int32>& NodeToBone,
    FVRMParsedModel& Out, TArray<FMatrix44f>& OutVertexToRest);

// Morph targets of the same mesh instances, in the same vertex order.
static void ParseMorphTargets(const cgltf_data* Data, const TArray<FVRMMeshInstance>& Instances, const TArray<FMatrix44f>& VertexToRest, FVRMParsedModel& Out);

// New forward for extracted material parsing
static void ParseMaterialTextures(const cgltf_data* Data, FVRMParsedModel& Out);

// New helper: reset parsed model to a known default state
static void ResetParsedModel(FVRMParsedModel& Out);

// Bone population: every joint of every skin, parents before children, unique names.
// Fills OutNodeToBone (glTF node index -> bone index).
static void PopulateBonesFromSkins(const cgltf_data* Data, FVRMParsedModel& Out, TMap<int32, int32>& OutNodeToBone);

// Simple RAII wrapper to ensure cgltf_free is always called
struct FCgltfScoped
{
    cgltf_data* Data = nullptr;
    explicit FCgltfScoped(cgltf_data* In) : Data(In) {}
    ~FCgltfScoped() { if (Data) cgltf_free(Data); }

    FCgltfScoped(const FCgltfScoped&) = delete;
    FCgltfScoped& operator=(const FCgltfScoped&) = delete;

    FCgltfScoped(FCgltfScoped&& Other) noexcept : Data(Other.Data) { Other.Data = nullptr; }
    FCgltfScoped& operator=(FCgltfScoped&& Other) noexcept
    {
        if (Data) cgltf_free(Data);
        Data = Other.Data;
        Other.Data = nullptr;
        return *this;
    }
};

// glTF->UE axis conversion helpers
static FORCEINLINE FVector3f GltfToUE_Vector(const FVector3f& V)
{
    // glTF: +X Right, +Y Up, +Z Forward
    // UE:   +X Forward, +Y Right, +Z Up
    // Map (Xg,Yg,Zg) -> (Zg, Xg, Yg)
    return FVector3f(V.Z, V.X, V.Y);
}
static FORCEINLINE FVector  GltfToUE_Vector(const FVector& V)
{
    return FVector(V.Z, V.X, V.Y);
}
static FQuat GltfToUE_Quat(const FQuat& Q)
{
    // Rue = C * Rg * C^-1, where C maps glTF basis to UE basis.
    const FMatrix C( FPlane(0,0,1,0),  // col0 -> X components
                     FPlane(1,0,0,0),  // col1 -> Y components
                     FPlane(0,1,0,0),  // col2 -> Z components
                     FPlane(0,0,0,1) );
    const FMatrix CInv = C.GetTransposed();
    const FMatrix Rg = FQuatRotationMatrix(Q);
    const FMatrix Rue = C * Rg * CInv;
    return Rue.ToQuat().GetNormalized();
}

// --- Reference pose fix helpers ---
// Apply a minimal correction so that:
// 1) Left/Right is unmirrored (mirror across Y axis),
// 2) Forward faces +Y (apply +90deg rotation around Z)
static FORCEINLINE FVector3f RefFix_Vector(const FVector3f& V)
{
    // Mirror across Y (flip sign)
    const FVector3f M(V.X, -V.Y, V.Z);
    // Rotate +90 degrees around Z: (x,y,z) -> (-y,x,z)
    return FVector3f(-M.Y, M.X, M.Z);
}
static FORCEINLINE FVector RefFix_Vector(const FVector& V)
{
    const FVector M(V.X, -V.Y, V.Z);
    return FVector(-M.Y, M.X, M.Z);
}

template<typename TWeight>
static void BindAllToRoot(TArray<TWeight>& Weights);

template<typename TWeight>
static int32 ReadJointsWeights(
    const cgltf_accessor& AJ, const cgltf_accessor& AW,
    const cgltf_skin& Skin, const cgltf_data& Data,
    const TMap<int32, int32>& NodeToBone,
    TArray<TWeight>& Out);

// Minimal data: URI -> bytes (PNG/JPEG)
static bool DecodeDataUri(const FString& Uri, TArray64<uint8>& OutBytes);

// ===== UInterchangeTranslatorBase =====

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
    if (!LoadVRM(Parsed))
    {
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

    // Textures: nodes and factories
    TexturePayloadKeys.Reset();
    TArray<FString> TextureNodeUids;
    TextureNodeUids.SetNum(Parsed.Images.Num());
    for (int32 ti = 0; ti < Parsed.Images.Num(); ++ti)
    {
        const FString TextureKey = FString::Printf(TEXT("Tex_%d"), ti);
        TexturePayloadKeys.Add(TextureKey);
        UInterchangeTexture2DNode* TexNode = NewObject<UInterchangeTexture2DNode>(&NodeContainer);
        const FString TexUid = MakeNodeUid(*TextureKey);
        TextureNodeUids[ti] = TexUid;
        NodeContainer.SetupNode(TexNode, TexUid, *FString::Printf(TEXT("VRM_Tex_%d"), ti), EInterchangeNodeContainerType::TranslatedAsset);
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

        // Assign texture parameters
        if (M.BaseColorTexture != INDEX_NONE && Parsed.Images.IsValidIndex(M.BaseColorTexture))
        {
            MatMINode->AddTextureParameterValue(TEXT("BaseColorTexture"), TextureNodeUids[M.BaseColorTexture]);
        }
        if (M.NormalTexture != INDEX_NONE && Parsed.Images.IsValidIndex(M.NormalTexture))
        {
            MatMINode->AddTextureParameterValue(TEXT("NormalTexture"), TextureNodeUids[M.NormalTexture]);
        }
        if (M.MetallicRoughnessTexture != INDEX_NONE && Parsed.Images.IsValidIndex(M.MetallicRoughnessTexture))
        {
            MatMINode->AddTextureParameterValue(TEXT("ORMTexture"), TextureNodeUids[M.MetallicRoughnessTexture]);
        }
        if (M.OcclusionTexture != INDEX_NONE && Parsed.Images.IsValidIndex(M.OcclusionTexture))
        {
            MatMINode->AddTextureParameterValue(TEXT("OcclusionTexture"), TextureNodeUids[M.OcclusionTexture]);
        }
        if (M.EmissiveTexture != INDEX_NONE && Parsed.Images.IsValidIndex(M.EmissiveTexture))
        {
            MatMINode->AddTextureParameterValue(TEXT("EmissiveTexture"), TextureNodeUids[M.EmissiveTexture]);
        }
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
    auto SetTexName = [&](int32 ImageIndex, const FString& Base, const TCHAR* Suffix)
    {
        if (!Parsed.Images.IsValidIndex(ImageIndex)) return;
        const FString TexUid = TextureNodeUids[ImageIndex];
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
        SetTexName(M.BaseColorTexture, MatName, TEXT("_DIFFUSE"));
        SetTexName(M.NormalTexture, MatName, TEXT("_NORMAL"));
        SetTexName(M.MetallicRoughnessTexture, MatName, TEXT("_ORM"));
        SetTexName(M.OcclusionTexture, MatName, TEXT("_AO"));
        SetTexName(M.EmissiveTexture, MatName, TEXT("_EMISSIVE"));
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
    using namespace UE::Interchange;

    // Expect keys like "Tex_0"
    if (!PayloadKey.StartsWith(TEXT("Tex_")))
    {
        UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] Unexpected texture payload key '%s'"), *PayloadKey);
        return {};
    }

    int32 TextureIndex = INDEX_NONE;
    {
        const FString IndexStr = PayloadKey.Mid(4);
        if (!IndexStr.IsNumeric())
        {
            UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] Invalid texture payload key '%s'"), *PayloadKey);
            return {};
        }
        TextureIndex = FCString::Atoi(*IndexStr);
    }

    if (!Parsed.Images.IsValidIndex(TextureIndex))
    {
        UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] Texture index %d out of range for payload '%s'"), TextureIndex, *PayloadKey);
        return {};
    }

    const TArray64<uint8>& CompressedBytes64 = Parsed.Images[TextureIndex].PNGOrJPEGBytes;
    if (CompressedBytes64.Num() == 0)
    {
        UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] No image bytes for texture index %d"), TextureIndex);
        return {};
    }

    const uint8* CompressedPtr = CompressedBytes64.GetData();
    const int64  CompressedSize = CompressedBytes64.Num();

    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    EImageFormat ImageFormat = ImageWrapperModule.DetectImageFormat(CompressedPtr, CompressedSize);

    if (ImageFormat == EImageFormat::Invalid)
    {
        UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] Unknown image format for texture index %d"), TextureIndex);
        return {};
    }

    TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);
    if (!Wrapper.IsValid() || !Wrapper->SetCompressed(CompressedPtr, CompressedSize))
    {
        UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] Failed to decode image for texture index %d"), TextureIndex);
        return {};
    }

    const int32 SizeX = Wrapper->GetWidth();
    const int32 SizeY = Wrapper->GetHeight();

    TArray<uint8> RGBA8;
    if (!Wrapper->GetRaw(ERGBFormat::RGBA, 8, RGBA8))
    {
        UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] GetRaw RGBA8 failed for texture index %d"), TextureIndex);
        return {};
    }

    // Convert RGBA8 -> BGRA8 in-place
    for (int32 i = 0; i + 3 < RGBA8.Num(); i += 4)
    {
        Swap(RGBA8[i + 0], RGBA8[i + 2]); // R <-> B
    }

    FImportImage ImportImage;
    ImportImage.Init2DWithParams(SizeX, SizeY, /*NumMips*/ 1, ETextureSourceFormat::TSF_BGRA8, /*bSRGB*/ true);

    TArrayView64<uint8> Dest = ImportImage.GetArrayViewOfRawData();
    check(Dest.Num() == RGBA8.Num());
    FMemory::Memcpy(Dest.GetData(), RGBA8.GetData(), RGBA8.Num());

    return ImportImage;
}

// ===== Helpers =====

FString UVRMTranslator::MakeNodeUid(const TCHAR* Suffix) const
{
    const FString Base = FPaths::GetBaseFilename(GetSourceData()->GetFilename());
    return FString::Printf(TEXT("VRM_%s_%s"), *Base, Suffix);
}

// Parse + load + validate helper (returns RAII wrapper; OutError receives human-friendly message)
static FCgltfScoped ParseCgltfFile(const FString& Filename, FString& OutError)
{
    OutError.Empty();

    FTCHARToUTF8 PathUtf8(*Filename);
    cgltf_options Options = {};
    cgltf_data* Data = nullptr;

    cgltf_result Res = cgltf_parse_file(&Options, PathUtf8.Get(), &Data);
    if (Res != cgltf_result_success || !Data)
    {
        OutError = FString::Printf(TEXT("cgltf_parse_file failed: %s"), *Filename);
        return FCgltfScoped(nullptr);
    }

    FCgltfScoped ScopedData(Data);

    Res = cgltf_load_buffers(&Options, Data, PathUtf8.Get());
    if (Res != cgltf_result_success)
    {
        OutError = FString::Printf(TEXT("cgltf_load_buffers failed: %s"), *Filename);
        return FCgltfScoped(nullptr);
    }

    Res = cgltf_validate(Data);
    if (Res != cgltf_result_success)
    {
        // Validation catches out-of-range accessors and buffer views that would otherwise be read out of bounds.
        OutError = FString::Printf(TEXT("glTF validation failed (cgltf error %d): %s"), int32(Res), *Filename);
        return FCgltfScoped(nullptr);
    }

    return ScopedData;
}

// cgltf matrices are column-major for column vectors; FMatrix uses row vectors, so the same 16
// floats in row-major order give the equivalent FMatrix. Axes are unchanged (still glTF space).
static FMatrix CgltfToFMatrix(const cgltf_float M[16])
{
    FMatrix Result;
    for (int32 r = 0; r < 4; ++r)
    {
        for (int32 c = 0; c < 4; ++c)
        {
            Result.M[r][c] = M[r * 4 + c];
        }
    }
    return Result;
}

static FMatrix NodeWorldMatrix(const cgltf_node* Node)
{
    // Includes every ancestor (joint or not), node matrices and scale.
    cgltf_float M[16];
    cgltf_node_transform_world(Node, M);
    return CgltfToFMatrix(M);
}

static FVector3f TransformNormal(const FMatrix& InverseTranspose, const FVector3f& N)
{
    const FVector3f Out = FVector3f(FVector(InverseTranspose.TransformVector(FVector(N))));
    return Out.GetSafeNormal(UE_SMALL_NUMBER, FVector3f(0, 0, 1));
}

FVector VRM::GltfPositionToUE(const FVector& GltfPosition, float GlobalScale)
{
    return RefFix_Vector(GltfToUE_Vector(GltfPosition)) * GlobalScale;
}

static TArray<FVRMMeshInstance> CollectMeshInstances(const cgltf_data* Data, const TMap<int32, int32>& NodeToBone)
{
    TArray<FVRMMeshInstance> Instances;
    if (!Data)
    {
        return Instances;
    }

    // Only nodes in the scene are drawn. Without a scene, use every node.
    const cgltf_scene* Scene = Data->scene ? Data->scene : (Data->scenes_count > 0 ? &Data->scenes[0] : nullptr);
    TSet<const cgltf_node*> InScene;
    if (Scene)
    {
        TArray<const cgltf_node*> Stack;
        for (size_t i = 0; i < Scene->nodes_count; ++i)
        {
            Stack.Add(Scene->nodes[i]);
        }
        while (Stack.Num() > 0)
        {
            const cgltf_node* Node = Stack.Pop(EAllowShrinking::No);
            if (!Node || InScene.Contains(Node))
            {
                continue;
            }
            InScene.Add(Node);
            for (size_t ci = 0; ci < Node->children_count; ++ci)
            {
                Stack.Add(Node->children[ci]);
            }
        }
    }

    // Node index order keeps the vertex order stable and independent of the hierarchy.
    TSet<const cgltf_mesh*> UsedMeshes;
    for (size_t ni = 0; ni < Data->nodes_count; ++ni)
    {
        const cgltf_node* Node = &Data->nodes[ni];
        if (!Node->mesh || (Scene && !InScene.Contains(Node)))
        {
            continue;
        }
        UsedMeshes.Add(Node->mesh);

        FVRMMeshInstance& Instance = Instances.AddDefaulted_GetRef();
        Instance.Node = Node;
        Instance.Skin = Node->skin;
        Instance.World = NodeWorldMatrix(Node);

        // Rigid geometry follows the node itself when it is a joint, otherwise its nearest joint ancestor.
        for (const cgltf_node* P = Node; P; P = P->parent)
        {
            if (const int32* Bone = NodeToBone.Find(int32(P - Data->nodes)))
            {
                Instance.RigidBone = *Bone;
                break;
            }
        }
    }

    const int32 Unused = int32(Data->meshes_count) - UsedMeshes.Num();
    if (Unused > 0)
    {
        UE_LOG(LogVRMInterchange, Log, TEXT("[VRMInterchange] %d mesh(es) are not placed by any node in the scene and were not imported."), Unused);
    }
    return Instances;
}

// Per skin, per bone: the joint matrix (inverse bind, then joint world) that takes bind-space
// vertices to the rest pose. Identity for every joint when the mesh was bound in the rest pose.
struct FVRMSkinRestPose
{
    TArray<FMatrix> BoneXf;   // indexed by bone index
    bool bIsRestPose = true;
};

static FVRMSkinRestPose ComputeSkinRestPose(const cgltf_skin& Skin, const cgltf_data& Data, const TMap<int32, int32>& NodeToBone, int32 NumBones)
{
    // Tolerances in glTF units: rotation/scale terms, and metres for translation.
    constexpr double LinearTolerance = 1.e-4;
    constexpr double TranslationTolerance = 1.e-4;

    FVRMSkinRestPose Result;
    Result.BoneXf.Init(FMatrix::Identity, NumBones);
    for (size_t j = 0; j < Skin.joints_count; ++j)
    {
        const cgltf_node* Joint = Skin.joints[j];
        const int32* Bone = Joint ? NodeToBone.Find(int32(Joint - Data.nodes)) : nullptr;
        if (!Bone || !Result.BoneXf.IsValidIndex(*Bone))
        {
            continue;
        }

        // glTF defaults a missing inverseBindMatrices accessor to identity matrices.
        FMatrix InverseBind = FMatrix::Identity;
        if (Skin.inverse_bind_matrices)
        {
            cgltf_float M[16];
            if (cgltf_accessor_read_float(Skin.inverse_bind_matrices, j, M, 16))
            {
                InverseBind = CgltfToFMatrix(M);
            }
        }

        // Row vectors: apply the inverse bind first, then the joint's world transform.
        const FMatrix JointXf = InverseBind * NodeWorldMatrix(Joint);
        Result.BoneXf[*Bone] = JointXf;

        for (int32 r = 0; r < 4 && Result.bIsRestPose; ++r)
        {
            for (int32 c = 0; c < 4; ++c)
            {
                const double Tolerance = (r == 3) ? TranslationTolerance : LinearTolerance;
                if (FMath::Abs(JointXf.M[r][c] - FMatrix::Identity.M[r][c]) > Tolerance)
                {
                    Result.bIsRestPose = false;
                    break;
                }
            }
        }
    }
    return Result;
}

static bool MergeMeshInstances(const cgltf_data* Data, const TArray<FVRMMeshInstance>& Instances, const TMap<int32, int32>& NodeToBone,
    FVRMParsedModel& Out, TArray<FMatrix44f>& OutVertexToRest)
{
    if (!Data) return false;

    TMap<const cgltf_skin*, FVRMSkinRestPose> SkinRestPoses;

    int32 VertexBase = 0;
    for (const FVRMMeshInstance& Instance : Instances)
    {
        const cgltf_mesh* Mesh = Instance.Node->mesh;
        const int32 NodeIndex = int32(Instance.Node - Data->nodes);
        const FMatrix NodeNormalXf = Instance.World.Inverse().GetTransposed();
        const bool bNodeMirrors = Instance.World.Determinant() < 0.0;

        for (size_t pi = 0; pi < Mesh->primitives_count; ++pi)
        {
            const cgltf_primitive* Prim = &Mesh->primitives[pi];

            // POSITION
            TArray<FVector3f> PosLocal;
            if (const cgltf_attribute* A = FindAttribute(Prim, cgltf_attribute_type_position))
            {
                if (!A->data) { continue; }
                ReadAccessorVec3f(*A->data, PosLocal);
            }
            else { continue; }

            // NORMAL
            TArray<FVector3f> NrmLocal;
            if (const cgltf_attribute* A = FindAttribute(Prim, cgltf_attribute_type_normal))
            {
                if (A->data) { ReadAccessorVec3f(*A->data, NrmLocal); }
            }

            // TEXCOORD_0
            TArray<FVector2f> UVLocal;
            if (const cgltf_attribute* A = FindTexcoord(Prim, 0))
            {
                if (A->data) { ReadAccessorVec2f(*A->data, UVLocal); }
            }
            if (UVLocal.Num() == 0)
            {
                UVLocal.SetNumZeroed(PosLocal.Num());
            }

            const int32 VertCount = PosLocal.Num();

            // Indices for this primitive
            TArray<uint32> IndLocal;
            if (Prim->indices) { ReadIndicesUInt32(*Prim->indices, IndLocal); }
            else
            {
                IndLocal.SetNum(VertCount);
                for (int32 i = 0; i < VertCount; ++i) { IndLocal[i] = i; }
            }

            // JOINTS/WEIGHTS. A primitive is skinned only when its node has a skin and the
            // primitive carries matching JOINTS_0/WEIGHTS_0; everything else is rigid.
            TArray<FVRMParsedMesh::FWeight> Weights;
            bool bSkinned = false;
            if (Instance.Skin)
            {
                const cgltf_attribute* AJ = FindJoints(Prim, 0);
                const cgltf_attribute* AW = FindWeights(Prim, 0);
                if (AJ && AW && AJ->data && AW->data && int32(AJ->data->count) == VertCount && int32(AW->data->count) == VertCount)
                {
                    Weights.SetNumZeroed(VertCount);
                    const int32 InvalidInfluences = ReadJointsWeights(*AJ->data, *AW->data, *Instance.Skin, *Data, NodeToBone, Weights);
                    if (InvalidInfluences > 0)
                    {
                        UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] Node %d primitive %d: %d joint influences reference joints outside the skin and were dropped."),
                            NodeIndex, int32(pi), InvalidInfluences);
                    }
                    bSkinned = true;
                }
                if (FindJoints(Prim, 1) || FindWeights(Prim, 1))
                {
                    UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] Node %d primitive %d has more than 4 joint influences per vertex (JOINTS_1/WEIGHTS_1); only the first 4 are imported."),
                        NodeIndex, int32(pi));
                }
            }

            // Rest pose. glTF ignores a skinned node's own transform: skinned vertices are in bind
            // space and reach the rest pose through their joint matrices, which are identity when
            // the mesh was bound in the rest pose (the usual case). Rigid vertices use the node's
            // world transform.
            const FVRMSkinRestPose* SkinRest = nullptr;
            if (bSkinned)
            {
                SkinRest = SkinRestPoses.Find(Instance.Skin);
                if (!SkinRest)
                {
                    SkinRest = &SkinRestPoses.Add(Instance.Skin, ComputeSkinRestPose(*Instance.Skin, *Data, NodeToBone, Out.Bones.Num()));
                    if (!SkinRest->bIsRestPose)
                    {
                        UE_LOG(LogVRMInterchange, Log, TEXT("[VRMInterchange] Skin %d: the inverse bind matrices differ from the node rest pose; skinned vertices were moved into the rest pose."),
                            int32(Instance.Skin - Data->skins));
                    }
                }
            }

            Out.Mesh.Positions.Reserve(Out.Mesh.Positions.Num() + VertCount);
            Out.Mesh.Normals.Reserve(Out.Mesh.Normals.Num() + VertCount);
            Out.Mesh.UV0.Reserve(Out.Mesh.UV0.Num() + VertCount);
            Out.Mesh.SkinWeights.Reserve(Out.Mesh.SkinWeights.Num() + VertCount);
            OutVertexToRest.Reserve(OutVertexToRest.Num() + VertCount);

            for (int32 v = 0; v < VertCount; ++v)
            {
                FMatrix VertexXf = FMatrix::Identity;
                FMatrix NormalXf = FMatrix::Identity;
                if (!bSkinned)
                {
                    VertexXf = Instance.World;
                    NormalXf = NodeNormalXf;
                }
                else if (!SkinRest->bIsRestPose)
                {
                    // Linear blend of the joint matrices, as a renderer would skin the rest pose.
                    FMatrix Blended(ForceInitToZero);
                    for (int32 k = 0; k < 4; ++k)
                    {
                        if (Weights[v].Weight[k] > 0.f && SkinRest->BoneXf.IsValidIndex(Weights[v].BoneIndex[k]))
                        {
                            Blended += SkinRest->BoneXf[Weights[v].BoneIndex[k]] * Weights[v].Weight[k];
                        }
                    }
                    VertexXf = Blended;
                    NormalXf = Blended.Inverse().GetTransposed();
                }

                const FVector RestPos = FVector(VertexXf.TransformPosition(FVector(PosLocal[v])));
                Out.Mesh.Positions.Add(FVector3f(VRM::GltfPositionToUE(RestPos, Out.GlobalScale)));

                if (NrmLocal.IsValidIndex(v))
                {
                    Out.Mesh.Normals.Add(RefFix_Vector(GltfToUE_Vector(TransformNormal(NormalXf, NrmLocal[v]))));
                }
                else
                {
                    Out.Mesh.Normals.Add(FVector3f(0, 0, 1));
                }

                Out.Mesh.UV0.Add(UVLocal[v]);
                OutVertexToRest.Add(FMatrix44f(VertexXf));

                if (bSkinned)
                {
                    Out.Mesh.SkinWeights.Add(Weights[v]);
                }
                else
                {
                    FVRMParsedMesh::FWeight W; W.BoneIndex[0] = uint16(Instance.RigidBone); W.Weight[0] = 1.f;
                    Out.Mesh.SkinWeights.Add(W);
                }
            }

            // Append indices with offset and record material index for each triangle. A mirroring
            // node transform turns the triangles inside out, so restore their winding.
            const int32 IndexBase = VertexBase;
            Out.Mesh.Indices.Reserve(Out.Mesh.Indices.Num() + IndLocal.Num());
            int32 MaterialIndex = INDEX_NONE;
            if (Prim->material)
            {
                MaterialIndex = int32(Prim->material - Data->materials);
            }
            const int32 LocalTriCount = IndLocal.Num() / 3;
            const bool bFlipWinding = !bSkinned && bNodeMirrors;
            for (int32 t = 0; t < LocalTriCount; ++t)
            {
                Out.Mesh.Indices.Add(IndexBase + int32(IndLocal[t * 3 + 0]));
                Out.Mesh.Indices.Add(IndexBase + int32(IndLocal[t * 3 + (bFlipWinding ? 2 : 1)]));
                Out.Mesh.Indices.Add(IndexBase + int32(IndLocal[t * 3 + (bFlipWinding ? 1 : 2)]));
                Out.Mesh.TriMaterialIndex.Add(FMath::Max(0, MaterialIndex));
            }

            VertexBase += VertCount;
        }
    }

    return true;
}

// ---- cgltf-based file load ----
bool UVRMTranslator::LoadVRM(FVRMParsedModel& Out) const
{
    return VRM::LoadVRMFile(GetSourceData()->GetFilename(), Out);
}

bool VRM::LoadVRMFile(const FString& Filename, FVRMParsedModel& Out)
{
    // Use centralized reset to set defaults and clear arrays
    ResetParsedModel(Out);

    // Use helper to parse/load/validate the file and obtain RAII-managed cgltf_data
    FString ParseError;
    FCgltfScoped ScopedData = ParseCgltfFile(Filename, ParseError);
    if (!ScopedData.Data)
    {
        UE_LOG(LogVRMInterchange, Error, TEXT("[VRMInterchange] %s"), *ParseError);
        return false;
    }
    cgltf_data* Data = ScopedData.Data;

    // Centralized validation checks
    if (!ValidateCgltfData(Data, ParseError))
    {
        UE_LOG(LogVRMInterchange, Error, TEXT("[VRMInterchange] %s"), *ParseError);
        return false;
    }

    // Bones: the joints of every skin (names, parents, UE-space local binds, reference pose fix).
    // Also fills Out.NodeToBoneMap for spring bone resolution.
    TMap<int32, int32> NodeToBone;
    PopulateBonesFromSkins(Data, Out, NodeToBone);
    if (Data->skins_count > 0 && Out.Bones.Num() == 0)
    {
        // Preserve previous failure behavior when a skin exists but no joints were produced
        return false;
    }

    // Every mesh node's primitives, placed in the rest pose the skeleton uses
    const TArray<FVRMMeshInstance> Instances = CollectMeshInstances(Data, NodeToBone);
    TArray<FMatrix44f> VertexToRest;
    if (!MergeMeshInstances(Data, Instances, NodeToBone, Out, VertexToRest))
    {
        UE_LOG(LogVRMInterchange, Error, TEXT("[VRMInterchange] Failed to merge mesh primitives."));
        return false;
    }

    // Morph targets - must run after the primitives are merged, over the same instances
    ParseMorphTargets(Data, Instances, VertexToRest, Out);

    // Images: use extracted helper
    if (Data->images_count > 0)
    {
        LoadImagesFromCgltf(Data, Filename, Out);
    }

    // Materials: record at least base-color texture indices if available (optional)
    ParseMaterialTextures(Data, Out);
    return true;
}

// New validation helper (centralized checks)
static bool ValidateCgltfData(const cgltf_data* Data, FString& OutError)
{
    OutError.Empty();
    if (!Data)
    {
        OutError = TEXT("No glTF data (null).");
        return false;
    }

    if (Data->meshes_count == 0 || Data->nodes_count == 0)
    {
        OutError = TEXT("No meshes or nodes in file.");
        return false;
    }

    // Ensure every mesh has at least one primitive and each primitive has a POSITION attribute.
    for (size_t mi = 0; mi < Data->meshes_count; ++mi)
    {
        const cgltf_mesh* Mesh = &Data->meshes[mi];
        if (Mesh->primitives_count == 0)
        {
            OutError = FString::Printf(TEXT("Mesh %d contains no primitives."), int(mi));
            return false;
        }

        for (size_t pi = 0; pi < Mesh->primitives_count; ++pi)
        {
            const cgltf_primitive* Prim = &Mesh->primitives[pi];

            bool bHasPosition = false;
            for (size_t ai = 0; ai < Prim->attributes_count; ++ai)
            {
                if (Prim->attributes[ai].type == cgltf_attribute_type_position)
                {
                    bHasPosition = true;
                    break;
                }
            }

            if (!bHasPosition)
            {
                OutError = FString::Printf(TEXT("Primitive %d.%d missing POSITION attribute."), int(mi), int(pi));
                return false;
            }
        }
    }

    return true;
}

// Attribute finders
static const cgltf_attribute* FindAttribute(const cgltf_primitive* Prim, cgltf_attribute_type Type)
{
    for (size_t i = 0; i < Prim->attributes_count; ++i)
        if (Prim->attributes[i].type == Type) return &Prim->attributes[i];
    return nullptr;
}
static const cgltf_attribute* FindTexcoord(const cgltf_primitive* Prim, int Set)
{
    for (size_t i = 0; i < Prim->attributes_count; ++i)
        if (Prim->attributes[i].type == cgltf_attribute_type_texcoord && (int)Prim->attributes[i].index == Set)
            return &Prim->attributes[i];
    return nullptr;
}
static const cgltf_attribute* FindJoints(const cgltf_primitive* Prim, int Set)
{
    for (size_t i = 0; i < Prim->attributes_count; ++i)
        if (Prim->attributes[i].type == cgltf_attribute_type_joints && (int)Prim->attributes[i].index == Set)
            return &Prim->attributes[i];
    return nullptr;
}
static const cgltf_attribute* FindWeights(const cgltf_primitive* Prim, int Set)
{
    for (size_t i = 0; i < Prim->attributes_count; ++i)
        if (Prim->attributes[i].type == cgltf_attribute_type_weights && (int)Prim->attributes[i].index == Set)
            return &Prim->attributes[i];
    return nullptr;
}
static const cgltf_accessor* FindTargetAccessor(const cgltf_morph_target& Tgt, cgltf_attribute_type Type)
{
    for (size_t i = 0; i < Tgt.attributes_count; ++i)
        if (Tgt.attributes[i].type == Type) return Tgt.attributes[i].data;
    return nullptr;
}

// Accessor readers
static void ReadAccessorVec3f(const cgltf_accessor& A, TArray<FVector3f>& Out)
{
    Out.SetNumUninitialized(int32(A.count));
    float v[3] = {0,0,0};
    for (int32 i = 0; i < (int32)A.count; ++i)
    {
        cgltf_accessor_read_float(&A, i, v, 3);
        Out[i] = FVector3f(v[0], v[1], v[2]);
    }
}
static void ReadAccessorVec2f(const cgltf_accessor& A, TArray<FVector2f>& Out)
{
    Out.SetNumUninitialized(int32(A.count));
    float v[2] = {0,0};
    for (int32 i = 0; i < (int32)A.count; ++i)
    {
        cgltf_accessor_read_float(&A, i, v, 2);
        Out[i] = FVector2f(v[0], v[1]);
    }
}
static void ReadIndicesUInt32(const cgltf_accessor& A, TArray<uint32>& Out)
{
    Out.SetNumUninitialized(int32(A.count));
    for (int32 i = 0; i < (int32)A.count; ++i)
    {
        cgltf_uint v[1] = {0};
        cgltf_accessor_read_uint(&A, i, v, 1);
        Out[i] = (uint32)v[0];
    }
}

template<typename TWeight>
static void BindAllToRoot(TArray<TWeight>& Weights)
{
    for (auto& W : Weights)
    {
        W.BoneIndex[0] = 0; W.Weight[0] = 1.f;
        for (int k = 1; k < 4; ++k) { W.BoneIndex[k] = 0; W.Weight[k] = 0.f; }
    }
}

template<typename TWeight>
static int32 ReadJointsWeights(
    const cgltf_accessor& AJ, const cgltf_accessor& AW,
    const cgltf_skin& Skin, const cgltf_data& Data,
    const TMap<int32, int32>& NodeToBone,
    TArray<TWeight>& Out)
{
    const int32 Count = FMath::Min3(int32(AJ.count), int32(AW.count), Out.Num());
    int32 Invalid = 0;

    for (int32 i = 0; i < Count; ++i)
    {
        uint32 J[4] = { 0,0,0,0 };
        cgltf_accessor_read_uint(&AJ, i, (cgltf_uint*)J, 4);

        float W[4] = { 0,0,0,0 };
        cgltf_accessor_read_float(&AW, i, W, 4);

        auto& Dst = Out[i];
        for (int k = 0; k < 4; ++k)
        {
            // JOINTS_n values index into this skin's joints list, not into the node array.
            int32 Bone = INDEX_NONE;
            if (J[k] < Skin.joints_count && Skin.joints[J[k]])
            {
                if (const int32* Found = NodeToBone.Find(int32(Skin.joints[J[k]] - Data.nodes)))
                {
                    Bone = *Found;
                }
            }
            if (Bone == INDEX_NONE)
            {
                if (W[k] > 0.f)
                {
                    ++Invalid;
                }
                W[k] = 0.f;
                Bone = 0;
            }
            Dst.BoneIndex[k] = uint16(Bone);
        }

        const float Sum = W[0] + W[1] + W[2] + W[3];
        if (Sum > 1e-8f)
        {
            for (int k = 0; k < 4; ++k) { Dst.Weight[k] = W[k] / Sum; }
        }
        else
        {
            // No usable influence: bind to the first bone.
            Dst.BoneIndex[0] = 0; Dst.Weight[0] = 1.f;
            for (int k = 1; k < 4; ++k) { Dst.BoneIndex[k] = 0; Dst.Weight[k] = 0.f; }
        }
    }
    return Invalid;
}

static bool DecodeDataUri(const FString& Uri, TArray64<uint8>& OutBytes)
{
    int32 Comma = INDEX_NONE;
    if (!Uri.FindChar(TEXT(','), Comma)) return false;
    const FString Base64 = Uri.Mid(Comma + 1);

    TArray<uint8> Temp;
    if (!FBase64::Decode(Base64, Temp))
    {
        return false;
    }
    OutBytes.SetNumUninitialized(Temp.Num());
    if (Temp.Num() > 0)
    {
        FMemory::Memcpy(OutBytes.GetData(), Temp.GetData(), Temp.Num());
    }
    return true;
}

// Extracted image loader
static bool LoadImagesFromCgltf(const cgltf_data* Data, const FString& Filename, FVRMParsedModel& Out)
{
    if (!Data) return false;

    if (Data->images_count > 0)
    {
        Out.Images.SetNum(int32(Data->images_count));
        for (int32 ii = 0; ii < int32(Data->images_count); ++ii)
        {
            const cgltf_image* Img = &Data->images[ii];
            FVRMParsedImage P;
            P.Name = Img->name ? FString(UTF8_TO_TCHAR(Img->name)) : FString::Printf(TEXT("Image_%d"), ii);

            if (Img->buffer_view)
            {
                const cgltf_buffer_view* View = Img->buffer_view;
                const cgltf_buffer* Buffer = View->buffer;
                if (Buffer && Buffer->data && View->offset + View->size <= Buffer->size)
                {
                    const uint8* Ptr = (const uint8*)Buffer->data + View->offset;
                    const size_t Size = View->size;
                    P.PNGOrJPEGBytes.SetNumUninitialized(Size);
                    FMemory::Memcpy(P.PNGOrJPEGBytes.GetData(), Ptr, Size);
                }
                else
                {
                    UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] Image %d references a buffer view that is missing or out of range; skipping."), ii);
                }
            }
            else if (Img->uri)
            {
                const FString Uri = UTF8_TO_TCHAR(Img->uri);
                if (Uri.StartsWith(TEXT("data:")))
                {
                    DecodeDataUri(Uri, P.PNGOrJPEGBytes);
                }
                else
                {
                    const FString Dir = FPaths::GetPath(Filename);
                    const FString ImgPath = FPaths::ConvertRelativePathToFull(Dir / Uri);
                    FFileHelper::LoadFileToArray(P.PNGOrJPEGBytes, *ImgPath);
                }
            }
            Out.Images[ii] = MoveTemp(P);
        }
    }
    return true;
}

// Extracted helper: parse material textures and flags into Out.Materials
static void ParseMaterialTextures(const cgltf_data* Data, FVRMParsedModel& Out)
{
    Out.Materials.Reset();
    if (!Data || Data->materials_count == 0)
    {
        return;
    }

    for (int32 mi = 0; mi < int32(Data->materials_count); ++mi)
    {
        const cgltf_material* Mat = &Data->materials[mi];

        FVRMParsedModel::FMat M;
        M.Name = Mat->name ? FString(UTF8_TO_TCHAR(Mat->name)) : FString::Printf(TEXT("VRM_Mat_%d"), mi);

        // Defaults
        M.BaseColorTexture = INDEX_NONE;
        M.NormalTexture = INDEX_NONE;
        M.MetallicRoughnessTexture = INDEX_NONE;
        M.OcclusionTexture = INDEX_NONE;
        M.EmissiveTexture = INDEX_NONE;
        M.bDoubleSided = false;
        M.AlphaMode = 0;
        M.AlphaCutoff = 0.5f;

        // PBR textures
        if (Mat->has_pbr_metallic_roughness)
        {
            if (Mat->pbr_metallic_roughness.base_color_texture.texture &&
                Mat->pbr_metallic_roughness.base_color_texture.texture->image)
            {
                M.BaseColorTexture = int32(Mat->pbr_metallic_roughness.base_color_texture.texture->image - Data->images);
            }
            if (Mat->pbr_metallic_roughness.metallic_roughness_texture.texture &&
                Mat->pbr_metallic_roughness.metallic_roughness_texture.texture->image)
            {
                M.MetallicRoughnessTexture = int32(Mat->pbr_metallic_roughness.metallic_roughness_texture.texture->image - Data->images);
            }
        }

        // Normal
        if (Mat->normal_texture.texture && Mat->normal_texture.texture->image)
        {
            M.NormalTexture = int32(Mat->normal_texture.texture->image - Data->images);
        }

        // Occlusion
        if (Mat->occlusion_texture.texture && Mat->occlusion_texture.texture->image)
        {
            M.OcclusionTexture = int32(Mat->occlusion_texture.texture->image - Data->images);
        }

        // Emissive
        if (Mat->emissive_texture.texture && Mat->emissive_texture.texture->image)
        {
            M.EmissiveTexture = int32(Mat->emissive_texture.texture->image - Data->images);
        }

        // Double-sided
        M.bDoubleSided = Mat->double_sided != 0;

        // Alpha mode and cutoff
        if (Mat->alpha_mode == cgltf_alpha_mode_mask)
        {
            M.AlphaMode = 1;
            M.AlphaCutoff = (float)Mat->alpha_cutoff;
        }
        else if (Mat->alpha_mode == cgltf_alpha_mode_blend)
        {
            M.AlphaMode = 2;
        }

        Out.Materials.Add(M);
    }
}

static void ParseMorphTargets(const cgltf_data* Data, const TArray<FVRMMeshInstance>& Instances, const TArray<FMatrix44f>& VertexToRest, FVRMParsedModel& Out)
{
    if (!Data) return;

    const int32 TotalVertices = Out.Mesh.Positions.Num();
    if (TotalVertices <= 0) return;

    // Map target name -> global morph index
    TMap<FString, int32> NameToIndex;
    // Keep ordered list of names to create Out.Mesh.Morphs in deterministic order
    TArray<FString> OrderedNames;

    // First pass: discover all target names (if available) and build mapping.
    for (const FVRMMeshInstance& Instance : Instances)
    {
        const cgltf_mesh* Mesh2 = Instance.Node->mesh;
        const bool bHaveMeshNames = (Mesh2 && Mesh2->target_names && Mesh2->target_names_count > 0);

        for (size_t pi2 = 0; pi2 < Mesh2->primitives_count; ++pi2)
        {
            const cgltf_primitive* Prim2 = &Mesh2->primitives[pi2];
            for (size_t ti = 0; ti < Prim2->targets_count; ++ti)
            {
                FString TargetName;
                if (bHaveMeshNames && ti < Mesh2->target_names_count && Mesh2->target_names[ti])
                {
                    TargetName = FString(UTF8_TO_TCHAR(Mesh2->target_names[ti])).TrimStartAndEnd();
                }
                // If no name available, use deterministic index-based fallback so unnamed targets still group by index
                if (TargetName.IsEmpty())
                {
                    TargetName = FString::Printf(TEXT("morph_%d"), int32(ti));
                }

                if (!NameToIndex.Contains(TargetName))
                {
                    const int32 NewIdx = OrderedNames.Num();
                    OrderedNames.Add(TargetName);
                    NameToIndex.Add(TargetName, NewIdx);
                }
            }
        }
    }

    // If no targets discovered, nothing to do.
    if (OrderedNames.Num() == 0) return;

    // Allocate global morphs and zero the delta arrays
    Out.Mesh.Morphs.SetNum(OrderedNames.Num());
    for (int32 mi = 0; mi < OrderedNames.Num(); ++mi)
    {
        Out.Mesh.Morphs[mi].Name = OrderedNames[mi];
        Out.Mesh.Morphs[mi].DeltaPositions.SetNumZeroed(TotalVertices);
    }

    // Second pass: read per-primitive deltas and merge into the global morph identified by name (or fallback index-name)
    int32 VertexBase2 = 0;
    for (const FVRMMeshInstance& Instance : Instances)
    {
        const cgltf_mesh* Mesh2 = Instance.Node->mesh;
        const int32 NodeIndex = int32(Instance.Node - Data->nodes);
        const bool bHaveMeshNames = (Mesh2 && Mesh2->target_names && Mesh2->target_names_count > 0);

        for (size_t pi2 = 0; pi2 < Mesh2->primitives_count; ++pi2)
        {
            const cgltf_primitive* Prim2 = &Mesh2->primitives[pi2];

            // POSITION accessor to determine this primitive's vertex count. Primitives without
            // positions were skipped by MergeMeshInstances, so skip them here too.
            int32 PrimVertCount = 0;
            if (const cgltf_attribute* Apos = FindAttribute(Prim2, cgltf_attribute_type_position))
            {
                if (Apos->data)
                {
                    PrimVertCount = (int32)Apos->data->count;
                }
            }
            if (PrimVertCount == 0)
            {
                continue;
            }

            for (size_t ti = 0; ti < Prim2->targets_count; ++ti)
            {
                // Determine global morph name/key for this primitive target
                FString TargetName;
                if (bHaveMeshNames && ti < Mesh2->target_names_count && Mesh2->target_names[ti])
                {
                    TargetName = FString(UTF8_TO_TCHAR(Mesh2->target_names[ti])).TrimStartAndEnd();
                }
                if (TargetName.IsEmpty())
                {
                    TargetName = FString::Printf(TEXT("morph_%d"), int32(ti));
                }

                const int32* FoundGlobal = NameToIndex.Find(TargetName);
                if (!FoundGlobal)
                {
                    // Shouldn't happen, but guard
                    continue;
                }
                const int32 GlobalMorphIndex = *FoundGlobal;

                const cgltf_morph_target& Tgt = Prim2->targets[ti];
                const cgltf_accessor* PosAcc = FindTargetAccessor(Tgt, cgltf_attribute_type_position);
                if (!PosAcc || !PosAcc->count)
                {
                    continue;
                }

                TArray<FVector3f> DeltaLocal;
                ReadAccessorVec3f(*PosAcc, DeltaLocal);

                if (DeltaLocal.Num() != PrimVertCount)
                {
                    UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] Morph target vertex count mismatch (node %d primitive %d): %d vs %d. Skipping."), NodeIndex, (int)pi2, DeltaLocal.Num(), PrimVertCount);
                    continue;
                }

                for (int32 v = 0; v < PrimVertCount; ++v)
                {
                    const int32 GlobalIndex = VertexBase2 + v;
                    // Deltas are offsets, so they take only the linear part of the vertex's rest transform.
                    const FVector3f Src = VertexToRest.IsValidIndex(GlobalIndex) ? FVector3f(VertexToRest[GlobalIndex].TransformVector(DeltaLocal[v])) : DeltaLocal[v];
                    const FVector3f Conv = RefFix_Vector(GltfToUE_Vector(Src)) * Out.GlobalScale;
                    if (Out.Mesh.Morphs.IsValidIndex(GlobalMorphIndex) && Out.Mesh.Morphs[GlobalMorphIndex].DeltaPositions.IsValidIndex(GlobalIndex))
                    {
                        Out.Mesh.Morphs[GlobalMorphIndex].DeltaPositions[GlobalIndex] = Conv;
                    }
                }
            }

            VertexBase2 += PrimVertCount;
        }
    }
}

// Implementation: reset parsed model to defaults and clear all arrays
static void ResetParsedModel(FVRMParsedModel& Out)
{
    // Default global scale used throughout the translator
    Out.GlobalScale = 100.0f;

    Out.Materials.Reset();
    Out.Images.Reset();

    Out.Mesh.Positions.Reset();
    Out.Mesh.Normals.Reset();
    Out.Mesh.UV0.Reset();
    Out.Mesh.Indices.Reset();
    Out.Mesh.SkinWeights.Reset();
    Out.Mesh.TriMaterialIndex.Reset();
    Out.Mesh.Morphs.Reset();

    Out.Bones.Reset();
    Out.NodeToBoneMap.Reset();
}

// Populate bones from the joints of every skin: unique names, parent indices and local binds converted
// to UE space. Also performs the "reference pose fix" that zeroes rotations and corrects global positions.
/**
 * Reference pose (decision D-3 in the refactor plan, still open). Every bone gets identity
 * rotation and unit scale, and a local translation equal to the offset of its rest position from
 * its parent's. The original joint rotations are not kept. Mesh vertices are placed in the same
 * rest pose (see MergeMeshInstances), so mesh and skeleton agree. VMC senders stream local bone
 * rotations relative to a T-pose, which is why live retargeting relies on this.
 */
static void BuildReferencePoseBinds(const TArray<FVector>& RestPositions, TArray<FVRMParsedBone>& Bones)
{
    for (int32 i = 0; i < Bones.Num(); ++i)
    {
        const int32 Parent = Bones[i].Parent;
        const FVector ParentPos = Bones.IsValidIndex(Parent) ? RestPositions[Parent] : FVector::ZeroVector;
        Bones[i].LocalBind = FTransform(FQuat::Identity, RestPositions[i] - ParentPos, FVector::OneVector);
    }
}

static void PopulateBonesFromSkins(const cgltf_data* Data, FVRMParsedModel& Out, TMap<int32, int32>& OutNodeToBone)
{
    Out.Bones.Reset();
    Out.NodeToBoneMap.Reset();
    OutNodeToBone.Reset();
    if (!Data || Data->skins_count == 0)
    {
        return;
    }

    // Union of all skins' joints. Files often have one skin per skinned mesh, each listing only
    // the joints that mesh uses, in its own order.
    TSet<const cgltf_node*> JointNodes;
    for (size_t si = 0; si < Data->skins_count; ++si)
    {
        const cgltf_skin& Skin = Data->skins[si];
        for (size_t ji = 0; ji < Skin.joints_count; ++ji)
        {
            if (Skin.joints[ji])
            {
                JointNodes.Add(Skin.joints[ji]);
            }
        }
    }

    // Order bones by a pre-order walk of the node hierarchy so every parent precedes its children
    // (UE skeletons require that).
    TArray<const cgltf_node*> Ordered;
    Ordered.Reserve(JointNodes.Num());
    TFunction<void(const cgltf_node*)> Visit = [&](const cgltf_node* Node)
    {
        if (JointNodes.Contains(Node))
        {
            Ordered.Add(Node);
        }
        for (size_t ci = 0; ci < Node->children_count; ++ci)
        {
            Visit(Node->children[ci]);
        }
    };
    for (size_t ni = 0; ni < Data->nodes_count; ++ni)
    {
        if (!Data->nodes[ni].parent)
        {
            Visit(&Data->nodes[ni]);
        }
    }

    // Unique names. Bone names compare case-insensitively (FName), so duplicates are found that way.
    // Unnamed joints become Node_<glTF node index>; repeats get _1, _2, ... in bone order.
    TSet<FName> UsedNames;
    Out.Bones.SetNum(Ordered.Num());
    TArray<FVector> RestPositions;
    RestPositions.SetNum(Ordered.Num());
    for (int32 bi = 0; bi < Ordered.Num(); ++bi)
    {
        const cgltf_node* J = Ordered[bi];
        const int32 NodeIndex = int32(J - Data->nodes);
        OutNodeToBone.Add(NodeIndex, bi);

        FVRMParsedBone& B = Out.Bones[bi];
        B.NodeIndex = NodeIndex;

        const FString BaseName = (J->name && J->name[0]) ? FString(UTF8_TO_TCHAR(J->name)) : FString::Printf(TEXT("Node_%d"), NodeIndex);
        FString Name = BaseName;
        for (int32 Suffix = 1; UsedNames.Contains(FName(*Name)); ++Suffix)
        {
            Name = FString::Printf(TEXT("%s_%d"), *BaseName, Suffix);
        }
        if (Name != BaseName)
        {
            UE_LOG(LogVRMInterchange, Log, TEXT("[VRMInterchange] Joint node %d '%s' renamed to '%s' to keep bone names unique."), NodeIndex, *BaseName, *Name);
        }
        UsedNames.Add(FName(*Name));
        B.Name = Name;
        Out.NodeToBoneMap.Add(NodeIndex, FName(*Name));

        // Parent: nearest ancestor that is also a joint (already in Out.Bones thanks to the ordering).
        B.Parent = INDEX_NONE;
        for (const cgltf_node* P = J->parent; P; P = P->parent)
        {
            if (const int32* ParentBone = OutNodeToBone.Find(int32(P - Data->nodes)))
            {
                B.Parent = *ParentBone;
                break;
            }
        }

        // Rest pose position from the full node hierarchy: non-joint ancestors (an "Armature"
        // node, for example), rotations, scale and node matrices all apply.
        RestPositions[bi] = VRM::GltfPositionToUE(NodeWorldMatrix(J).GetOrigin(), Out.GlobalScale);
    }

    BuildReferencePoseBinds(RestPositions, Out.Bones);
}
