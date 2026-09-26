// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "InterchangeTranslatorBase.h"
#include "VRMParsedModel.h"

#if __has_include("Mesh/InterchangeMeshPayloadInterface.h")
  #include "Mesh/InterchangeMeshPayloadInterface.h"
#else
  #include "InterchangeMeshPayloadInterface.h"
#endif

#if __has_include("Texture/InterchangeTexturePayloadInterface.h")
  #include "Texture/InterchangeTexturePayloadInterface.h"
#else
  #include "InterchangeTexturePayloadInterface.h"
#endif

// Forward declares to avoid including heavy headers here
namespace UE::Interchange
{
    struct FMeshPayloadData;
    struct FImportImage;
    class FAttributeStorage;
}
struct FInterchangeMeshPayLoadKey;

namespace VRM
{
    /** What a material uses an image for; decides colour space and compression (T-06). */
    enum class ETextureUsage : uint8
    {
        None   = 0,
        Color  = 1 << 0, // base colour, emissive: sRGB
        Normal = 1 << 1, // normal map: linear, normal-map compression, green flipped (glTF is +Y, UE is -Y)
        Data   = 1 << 2, // metallic-roughness, occlusion: linear masks
    };
    ENUM_CLASS_FLAGS(ETextureUsage)

    /** Every use each image has across the model's materials (one entry per image; None if unused). */
    VRMINTERCHANGE_API TArray<ETextureUsage> ComputeTextureUsages(const FVRMParsedModel& Model);

    /**
     * Decodes a PNG or JPEG into the image Interchange imports, set up for one use (a single flag):
     * sRGB and default compression for Color; linear, TC_Normalmap and the green channel flipped for
     * Normal; linear TC_Masks for Data.
     */
    VRMINTERCHANGE_API TOptional<UE::Interchange::FImportImage> DecodeTextureImage(const TArray64<uint8>& CompressedBytes, ETextureUsage Usage);
}

#include "VRMTranslator.generated.h"

UCLASS()
class UVRMTranslator : public UInterchangeTranslatorBase
    , public IInterchangeMeshPayloadInterface
    , public IInterchangeTexturePayloadInterface
{
    GENERATED_BODY()

public:
    // UInterchangeTranslatorBase
    virtual EInterchangeTranslatorType GetTranslatorType() const override { return EInterchangeTranslatorType::Scenes; }
    virtual EInterchangeTranslatorAssetType GetSupportedAssetTypes() const override;
    virtual TArray<FString> GetSupportedFormats() const override;
    virtual bool CanImportSourceData(const UInterchangeSourceData* InSourceData) const override;
    virtual bool Translate(UInterchangeBaseNodeContainer& NodeContainer) const override;

    // IInterchangeMeshPayloadInterface (UE 5.6). Only the attribute-storage overload is
    // implemented; the FTransform overload is deprecated in 5.6 and the engine no longer calls it.
    virtual TOptional<UE::Interchange::FMeshPayloadData> GetMeshPayloadData(const FInterchangeMeshPayLoadKey& PayLoadKey, const UE::Interchange::FAttributeStorage& PayloadAttributes) const override;

    // IInterchangeTexturePayloadInterface (UE 5.6)
    virtual TOptional<UE::Interchange::FImportImage> GetTexturePayloadData(const FString& PayloadKey, TOptional<FString>& AlternateTexturePath) const override;

private:
    // The model Translate built, shared read-only with the payload calls that follow it. Interchange
    // translators are const but keep their translation for the payload calls, hence mutable.
    mutable TSharedPtr<const FVRMParsedModel> ParsedModel;

    FString MakeNodeUid(const TCHAR* Suffix) const;
};
