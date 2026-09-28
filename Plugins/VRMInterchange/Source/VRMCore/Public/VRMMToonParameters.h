// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/**
 * Names shared by the MToon materials (P4.5): the editor module builds M_VRM_MToon and
 * M_VRM_MToonOutline with these parameters (VRMMToonMaterial.h), and the translator sets them on
 * the material instances it creates.
 */
namespace VRM::MToon
{
	/** Material parameter names. The instances the importer creates set these. */
	namespace Param
	{
		// Surface
		inline constexpr const TCHAR* BaseColorTexture = TEXT("BaseColorTexture");
		inline constexpr const TCHAR* BaseColorFactor = TEXT("BaseColorFactor");
		inline constexpr const TCHAR* ShadeMultiplyTexture = TEXT("ShadeMultiplyTexture");
		inline constexpr const TCHAR* ShadeColorFactor = TEXT("ShadeColorFactor");
		inline constexpr const TCHAR* NormalTexture = TEXT("NormalTexture");
		inline constexpr const TCHAR* NormalScale = TEXT("NormalScale");
		inline constexpr const TCHAR* UseShadingShiftTexture = TEXT("UseShadingShiftTexture");
		inline constexpr const TCHAR* ShadingShiftTexture = TEXT("ShadingShiftTexture");
		inline constexpr const TCHAR* ShadingShiftTextureScale = TEXT("ShadingShiftTextureScale");
		inline constexpr const TCHAR* ShadingShiftFactor = TEXT("ShadingShiftFactor");
		inline constexpr const TCHAR* ShadingToonyFactor = TEXT("ShadingToonyFactor");
		inline constexpr const TCHAR* EmissiveTexture = TEXT("EmissiveTexture");
		inline constexpr const TCHAR* EmissiveFactor = TEXT("EmissiveFactor");
		inline constexpr const TCHAR* MatcapTexture = TEXT("MatcapTexture");
		inline constexpr const TCHAR* MatcapFactor = TEXT("MatcapFactor");
		inline constexpr const TCHAR* RimMultiplyTexture = TEXT("RimMultiplyTexture");
		inline constexpr const TCHAR* ParametricRimColorFactor = TEXT("ParametricRimColorFactor");
		inline constexpr const TCHAR* ParametricRimFresnelPowerFactor = TEXT("ParametricRimFresnelPowerFactor");
		inline constexpr const TCHAR* ParametricRimLiftFactor = TEXT("ParametricRimLiftFactor");
		inline constexpr const TCHAR* RimLightingMixFactor = TEXT("RimLightingMixFactor");
		/** Static switch: lit colour plus emission, no lighting (KHR_materials_unlit, VRM 0.x Unlit shaders). */
		inline constexpr const TCHAR* UnlitShading = TEXT("UnlitShading");
		/** Masked materials keep pixels whose alpha reaches this. */
		inline constexpr const TCHAR* AlphaCutoff = TEXT("AlphaCutoff");
		/** EVRMAlphaMode (0 opaque, 1 mask, 2 blend) and double-sidedness (0 or 1). The graph doesn't use them: the
		 * importer turns them into the instance's blend mode and two-sided overrides. */
		inline constexpr const TCHAR* AlphaMode = TEXT("AlphaMode");
		inline constexpr const TCHAR* DoubleSided = TEXT("DoubleSided");

		// Outline
		inline constexpr const TCHAR* OutlineWidthMultiplyTexture = TEXT("OutlineWidthMultiplyTexture");
		inline constexpr const TCHAR* OutlineWidthFactor = TEXT("OutlineWidthFactor");
		inline constexpr const TCHAR* OutlineScreenCoordinates = TEXT("OutlineScreenCoordinates");
		inline constexpr const TCHAR* OutlineColorFactor = TEXT("OutlineColorFactor");
		inline constexpr const TCHAR* OutlineLightingMixFactor = TEXT("OutlineLightingMixFactor");

		// Both: KHR_texture_transform of the base colour texture, applied to every texture
		inline constexpr const TCHAR* UVOffsetU = TEXT("UVOffsetU");
		inline constexpr const TCHAR* UVOffsetV = TEXT("UVOffsetV");
		inline constexpr const TCHAR* UVScaleU = TEXT("UVScaleU");
		inline constexpr const TCHAR* UVScaleV = TEXT("UVScaleV");
		inline constexpr const TCHAR* UVRotation = TEXT("UVRotation");

		// Both: the light used when the level has no atmosphere sun light
		inline constexpr const TCHAR* FallbackLightDirection = TEXT("FallbackLightDirection");
		inline constexpr const TCHAR* FallbackLightColor = TEXT("FallbackLightColor");

		// Both: GraphVersion of the graph, to find generated materials that need rebuilding
		inline constexpr const TCHAR* GraphVersion = TEXT("MToonGraphVersion");
	}

	/** Where the editor module generates the materials (VRM::MToon::FindOrCreateMToonMaterials). */
	inline constexpr const TCHAR* GeneratedFolder = TEXT("/Game/VRMInterchange/Materials");
	inline constexpr const TCHAR* SurfaceName = TEXT("M_VRM_MToon");
	inline constexpr const TCHAR* OutlineName = TEXT("M_VRM_MToonOutline");
	inline constexpr const TCHAR* WhiteMaskName = TEXT("T_VRM_MToonWhiteMask");

	/** Object paths of the generated materials, for material instance parents. */
	inline constexpr const TCHAR* SurfacePath = TEXT("/Game/VRMInterchange/Materials/M_VRM_MToon.M_VRM_MToon");
	inline constexpr const TCHAR* OutlinePath = TEXT("/Game/VRMInterchange/Materials/M_VRM_MToonOutline.M_VRM_MToonOutline");
}
