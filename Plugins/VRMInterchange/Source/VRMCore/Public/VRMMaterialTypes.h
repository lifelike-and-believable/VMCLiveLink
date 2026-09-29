// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class FJsonObject;

// ---- A VRM file's materials (P4.5), read by VRM::ParseMaterials ----
//
// Texture fields are glTF image indices (textures[i].source), INDEX_NONE when absent. Colours are
// linear. MToon values use VRM 1.0 (VRMC_materials_mtoon) meanings; VRM 0.x materials are converted
// to them the way UniVRM and three-vrm migrate 0.x files.

/** glTF alphaMode. */
enum class EVRMAlphaMode : uint8
{
	/** Alpha is ignored. */
	Opaque,
	/** Pixels below AlphaCutoff are discarded. */
	Mask,
	/** Alpha blended (translucent). */
	Blend,
};

/** VRMC_materials_mtoon outlineWidthMode. */
enum class EVRMOutlineWidthMode : uint8
{
	/** No outline. */
	None,
	/** OutlineWidth is in metres. */
	WorldCoordinates,
	/** OutlineWidth is a fraction of the screen height. */
	ScreenCoordinates,
};

/** KHR_texture_transform: uv' = Rotate(uv * Scale, Rotation) + Offset, as the glTF extension defines it. */
struct FVRMTextureTransform
{
	/** Added after scaling and rotating, in UV units. */
	FVector2f Offset = FVector2f::ZeroVector;
	/** UV scale. */
	FVector2f Scale = FVector2f(1.f, 1.f);
	/** Radians, counter-clockwise in UV space. */
	float Rotation = 0.f;

	/** Whether it leaves UVs unchanged. */
	bool IsIdentity() const { return Offset.IsNearlyZero() && Scale.Equals(FVector2f(1.f, 1.f)) && FMath::IsNearlyZero(Rotation); }
};

/** VRMC_materials_mtoon, with its defaults. */
struct FVRMMToon
{
	// Shading: shading = linearstep(-1 + Toony, 1 - Toony, dot(N, L) + Shift + ShiftTexture * Scale),
	// colour = lerp(Shade, Lit, shading).
	FLinearColor ShadeColor = FLinearColor::Black;
	int32 ShadeMultiplyTexture = INDEX_NONE;
	float ShadingShift = 0.f;
	int32 ShadingShiftTexture = INDEX_NONE;
	float ShadingShiftTextureScale = 1.f;
	float ShadingToony = 0.9f;
	float GIEqualization = 0.9f;

	// Matcap: added, only when there is a texture.
	FLinearColor MatcapColor = FLinearColor::White;
	int32 MatcapTexture = INDEX_NONE;

	// Parametric rim: RimColor * pow(saturate(1 - dot(N, V) + Lift), FresnelPower).
	FLinearColor RimColor = FLinearColor::Black;
	int32 RimMultiplyTexture = INDEX_NONE;
	float RimLightingMix = 1.f;
	float RimFresnelPower = 5.f;
	float RimLift = 0.f;

	// Outline
	EVRMOutlineWidthMode OutlineWidthMode = EVRMOutlineWidthMode::None;
	/** As the file gives it: metres (WorldCoordinates) or a fraction of the screen height
	 *  (ScreenCoordinates). The outline material takes it in the same unit. */
	float OutlineWidth = 0.f;
	int32 OutlineWidthMultiplyTexture = INDEX_NONE;
	FLinearColor OutlineColor = FLinearColor::Black;
	float OutlineLightingMix = 1.f;

	/** Translucent, but writes depth (stored; the UE material doesn't use it). */
	bool bTransparentWithZWrite = false;
	/** Draw order among translucent materials (stored; not applied). */
	int32 RenderQueueOffset = 0;

	/** Whether the material draws an outline. */
	bool HasOutline() const { return OutlineWidthMode != EVRMOutlineWidthMode::None && OutlineWidth > 0.f; }
};

/** One glTF material, with its VRM extensions. */
struct FVRMParsedMaterial
{
	/** The glTF material's name. */
	FString Name;

	// glTF core (pbrMetallicRoughness and friends)
	int32 BaseColorTexture = INDEX_NONE;
	int32 NormalTexture = INDEX_NONE;
	/** normalTexture.scale (VRM 0.x: _BumpScale). */
	float NormalScale = 1.f;
	int32 MetallicRoughnessTexture = INDEX_NONE; // G=Roughness, B=Metallic
	int32 OcclusionTexture = INDEX_NONE; // R channel
	int32 EmissiveTexture = INDEX_NONE;
	FLinearColor BaseColorFactor = FLinearColor::White;
	/** RGB, already multiplied by KHR_materials_emissive_strength. */
	FLinearColor EmissiveFactor = FLinearColor::Black;
	float MetallicFactor = 1.f;
	float RoughnessFactor = 1.f;
	bool bDoubleSided = false;
	EVRMAlphaMode AlphaMode = EVRMAlphaMode::Opaque;
	float AlphaCutoff = 0.5f;

	/** The base colour texture's KHR_texture_transform (VRM 0.x: _MainTex scale and offset). Applied to every texture of the material, as UniVRM does. */
	FVRMTextureTransform UVTransform;

	/** KHR_materials_unlit, or a VRM 0.x VRM/Unlit* shader. */
	bool bUnlit = false;

	/** True when the material is MToon (VRMC_materials_mtoon, or VRM 0.x shader "VRM/MToon"). */
	bool bMToon = false;
	/** The MToon parameters; defaults unless bMToon. */
	FVRMMToon MToon;
};

namespace VRM
{
	/**
	 * Reads every glTF material of a document's top-level JSON, with KHR_texture_transform,
	 * KHR_materials_emissive_strength, KHR_materials_unlit, VRMC_materials_mtoon (VRM 1.0) and the
	 * VRM 0.x extension's materialProperties (matched to the glTF materials by index).
	 */
	VRMCORE_API TArray<FVRMParsedMaterial> ParseMaterials(const FJsonObject& Root);

	/** The VRM 0.x shading shift and toony (_ShadeShift, _ShadeToony) as VRM 1.0 values: X shift, Y toony. */
	VRMCORE_API FVector2f MigrateMToon0Shading(float ShadeShift0, float ShadeToony0);
}
