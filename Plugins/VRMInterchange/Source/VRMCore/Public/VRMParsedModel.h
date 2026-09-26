// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "VRMCoordinateConversion.h"

class FVRMDocument;

// ---- A VRM file's model, read from its document (VRM::BuildParsedModel) ----
struct FVRMParsedImage
{
    FString Name;
    TArray64<uint8> PNGOrJPEGBytes; // decoded later through ImageWrapper
};

struct FVRMParsedMorph
{
    FString Name;
    TArray<FVector3f> DeltaPositions;
};

struct FVRMParsedMesh
{
    TArray<FVector3f> Positions;
    TArray<FVector3f> Normals;
    TArray<FVector2f> UV0;
    TArray<uint32> Indices;

    // Per-triangle material index (Indices.Num()/3 entries). Refers to FVRMParsedModel::Materials index.
    TArray<int32> TriMaterialIndex;

    struct FWeight
    {
        uint16 BoneIndex[4] = { 0,0,0,0 };
        float  Weight[4] = { 1,0,0,0 };
    };
    TArray<FWeight> SkinWeights;

    TArray<FVRMParsedMorph> Morphs;

    int32 MaterialIndex = 0;
};

struct FVRMParsedBone
{
    FString Name;           // unique among the model's bones
    int32 Parent = INDEX_NONE;
    int32 NodeIndex = INDEX_NONE; // glTF node this bone was created from
    FTransform LocalBind;
};

struct FVRMParsedModel
{
    TArray<FVRMParsedBone> Bones;
    TArray<FVRMParsedImage> Images;

    struct FMat
    {
        FString Name;
        int32 BaseColorTexture = INDEX_NONE;
        int32 NormalTexture = INDEX_NONE;
        int32 MetallicRoughnessTexture = INDEX_NONE; // G=Roughness, B=Metallic
        int32 OcclusionTexture = INDEX_NONE; // R channel
        int32 EmissiveTexture = INDEX_NONE;
        bool bDoubleSided = false;
        int32 AlphaMode = 0; // 0 Opaque, 1 Mask, 2 Blend
        float AlphaCutoff = 0.5f;
    };
    TArray<FMat> Materials;

    // Single merged mesh for now
    FVRMParsedMesh Mesh;

    // glTF node index -> bone name, for every joint (populated during LoadVRM)
    TMap<int32, FName> NodeToBoneMap;

    float GlobalScale = 100.f;

    // VRM version, from the file's top-level extensions. Decides the facing (see VRMCoordinateConversion.h).
    VRM::Coord::EVRMVersion Version = VRM::Coord::EVRMVersion::Unknown;

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
