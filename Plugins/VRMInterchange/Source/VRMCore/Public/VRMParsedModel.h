// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "VRMCoordinateConversion.h"
#include "VRMMaterialTypes.h"

class FVRMDocument;

// ---- A VRM file's model, read from its document (VRM::BuildParsedModel) ----
// Everything here is in UE space: left-handed, Z up, centimetres, facing +Y (VRMCoordinateConversion.h).

/** An image the file embeds or references, still encoded. */
struct FVRMParsedImage
{
    /** The glTF image's name, or a generated one. */
    FString Name;
    TArray64<uint8> PNGOrJPEGBytes; // decoded later through ImageWrapper
};

/** One morph target of the merged mesh. */
struct FVRMParsedMorph
{
    /** The morph target's name on the imported mesh (unique, generated for unnamed targets). */
    FString Name;
    /** Change of each vertex's position, in cm, in mesh space (one per FVRMParsedMesh::Positions). */
    TArray<FVector3f> DeltaPositions;
    // Change of each vertex's (unit) normal, in Unreal space. Empty when the file gives no NORMAL
    // deltas for this target; the morph then keeps the base normals.
    TArray<FVector3f> DeltaNormals;
};

/** The skinned mesh: every glTF primitive merged into one vertex and index list. */
struct FVRMParsedMesh
{
    /** Vertex positions in mesh (component) space, in cm, at the bind pose. */
    TArray<FVector3f> Positions;
    /** Unit vertex normals in mesh space. */
    TArray<FVector3f> Normals;
    /** First texture coordinate set, as glTF gives it (origin at the top left, as in UE). */
    TArray<FVector2f> UV0;
    /** Triangle list, three indices into Positions per triangle, in UE winding. */
    TArray<uint32> Indices;

    // Per-triangle material index (Indices.Num()/3 entries). Refers to FVRMParsedModel::Materials index.
    TArray<int32> TriMaterialIndex;

    /** Up to four bone influences of a vertex. */
    struct FWeight
    {
        /** Indices into FVRMParsedModel::Bones. */
        uint16 BoneIndex[4] = { 0,0,0,0 };
        /** Weights, summing to 1. */
        float  Weight[4] = { 1,0,0,0 };
    };
    /** One per vertex. */
    TArray<FWeight> SkinWeights;

    /** The morph targets, each with a delta for every vertex. */
    TArray<FVRMParsedMorph> Morphs;

    /** Material of a mesh without per-triangle materials. */
    int32 MaterialIndex = 0;
};

/** One bone of the imported skeleton. */
struct FVRMParsedBone
{
    FString Name;           // unique among the model's bones
    /** Index of the parent in FVRMParsedModel::Bones, or INDEX_NONE for the root. */
    int32 Parent = INDEX_NONE;
    int32 NodeIndex = INDEX_NONE; // glTF node this bone was created from
    /** Rest transform relative to the parent bone: a translation in cm, with identity rotation
     *  (decision D-3) and unit scale. */
    FTransform LocalBind;
};

/** A VRM file's skeleton, mesh, morph targets, images and materials, in UE space. */
struct FVRMParsedModel
{
    /** The skeleton, parents before children. */
    TArray<FVRMParsedBone> Bones;
    /** The file's images, by glTF image index. */
    TArray<FVRMParsedImage> Images;

    // Materials, by glTF material index (VRM::ParseMaterials)
    using FMat = FVRMParsedMaterial;
    TArray<FMat> Materials;

    // Single merged mesh for now
    FVRMParsedMesh Mesh;

    // glTF node index -> bone name, for every joint (populated during LoadVRM)
    TMap<int32, FName> NodeToBoneMap;

    // glTF mesh index -> the imported morph target name of each of its targets, by target index
    // (what expression binds resolve against, VRM::BuildAvatarData).
    TMap<int32, TArray<FString>> MeshMorphNames;

    /** Unit scale applied to glTF positions: centimetres per metre. */
    float GlobalScale = 100.f;

    // VRM version, from the file's top-level extensions. Decides the facing (see VRMCoordinateConversion.h).
    VRM::Coord::EVRMVersion Version = VRM::Coord::EVRMVersion::Unknown;

    /** The axis convention for this model's version and scale. */
    VRM::Coord::FVRMAxisConvention Convention() const { return VRM::Coord::FVRMAxisConvention::ForVersion(Version, GlobalScale); }
};

namespace VRM
{
    /**
     * Reads a document's skeleton, merged mesh, morph targets, images and materials into Out, in
     * Unreal space. Fails if the document has no geometry (FVRMDocument::HasGeometry).
     */
    VRMCORE_API bool BuildParsedModel(const FVRMDocument& Document, FVRMParsedModel& Out);

    /** Loads a file and builds its model (what UVRMTranslator translates). Exposed for tests. */
    VRMCORE_API bool LoadVRMFile(const FString& Filename, FVRMParsedModel& Out);

    /** The importer's glTF-to-UE position conversion (axes and GlobalScale) for a VRM 1.0 or generic glTF file. Exposed for tests. */
    VRMCORE_API FVector GltfPositionToUE(const FVector& GltfPosition, float GlobalScale);

    /** The same for a file of the given version (VRM 0.x adds a 180-degree yaw). */
    VRMCORE_API FVector GltfPositionToUE(const FVector& GltfPosition, float GlobalScale, VRM::Coord::EVRMVersion Version);
}
