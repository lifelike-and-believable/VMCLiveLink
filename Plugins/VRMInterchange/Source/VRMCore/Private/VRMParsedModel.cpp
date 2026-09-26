// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// Builds FVRMParsedModel from a document's cgltf data: skeleton, merged mesh, morph targets, images
// and materials. Moved from VRMTranslator.cpp (P3.3) so that cgltf stays inside VRMCore.
#include "VRMParsedModel.h"
#include "VRMDocument.h"
#include "VRMDocumentAccess.h"
#include "VRMCoreLog.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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

// glTF -> UE conversion lives in VRMCoordinateConversion.h (shared with the spring bone parser).
// Everything here converts through the parsed model's Convention(), which carries the facing.

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
    return VRM::Coord::ToUEPosition(GltfPosition, GlobalScale);
}

FVector VRM::GltfPositionToUE(const FVector& GltfPosition, float GlobalScale, VRM::Coord::EVRMVersion Version)
{
    return VRM::Coord::FVRMAxisConvention::ForVersion(Version, GlobalScale).Position(GltfPosition);
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
    const VRM::Coord::FVRMAxisConvention Convention = Out.Convention();

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
                Out.Mesh.Positions.Add(FVector3f(Convention.Position(RestPos)));

                if (NrmLocal.IsValidIndex(v))
                {
                    Out.Mesh.Normals.Add(Convention.Direction(TransformNormal(NormalXf, NrmLocal[v])));
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

bool VRM::LoadVRMFile(const FString& Filename, FVRMParsedModel& Out)
{
    FString Error;
    const TSharedPtr<const FVRMDocument> Document = FVRMDocument::LoadFile(Filename, Error);
    if (!Document.IsValid())
    {
        ResetParsedModel(Out);
        UE_LOG(LogVRMInterchange, Error, TEXT("[VRMInterchange] %s"), *Error);
        return false;
    }
    return BuildParsedModel(*Document, Out);
}

bool VRM::BuildParsedModel(const FVRMDocument& Document, FVRMParsedModel& Out)
{
    // Use centralized reset to set defaults and clear arrays
    ResetParsedModel(Out);

    const cgltf_data* Data = FVRMDocumentAccess::Gltf(Document);
    if (!Data)
    {
        UE_LOG(LogVRMInterchange, Error, TEXT("[VRMInterchange] %s"), *Document.GetGeometryError());
        return false;
    }
    const FString& Filename = Document.GetFilename();

    // Centralized validation checks
    FString ParseError;
    if (!ValidateCgltfData(Data, ParseError))
    {
        UE_LOG(LogVRMInterchange, Error, TEXT("[VRMInterchange] %s"), *ParseError);
        return false;
    }

    // Bones: the joints of every skin (names, parents, UE-space local binds, reference pose fix).
    // Also fills Out.NodeToBoneMap for spring bone resolution.
    // VRM version from the top-level extensions; it decides the facing (VRMCoordinateConversion.h).
    Out.Version = Document.GetVersion();
    if (Out.Version == VRM::Coord::EVRMVersion::Unknown)
    {
        UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] '%s' has no VRM or VRMC_vrm extension: not a VRM file, importing as generic glTF (facing +Z, like VRM 1.0)."), *Filename);
    }

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
    const VRM::Coord::FVRMAxisConvention Convention = Out.Convention();

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
        TArray<FString>& MeshNames = Out.MeshMorphNames.FindOrAdd(int32(Mesh2 - Data->meshes));

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

                if (MeshNames.Num() <= int32(ti))
                {
                    MeshNames.SetNum(int32(ti) + 1);
                }
                MeshNames[ti] = TargetName;

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
                    const FVector3f Conv = Convention.Position(Src);
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
    Out.Version = VRM::Coord::EVRMVersion::Unknown;

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
    Out.MeshMorphNames.Reset();
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
        RestPositions[bi] = Out.Convention().Position(NodeWorldMatrix(J).GetOrigin());
    }

    BuildReferencePoseBinds(RestPositions, Out.Bones);
}
