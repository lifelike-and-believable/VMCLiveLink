// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UMaterialInterface;
class UTexture;
class UTexture2D;

/**
 * The MToon master materials (P4.5, decision D-7: basic MToon).
 *
 * M_VRM_MToon draws a VRMC_materials_mtoon material: the lit and shade colours mixed by a toon ramp
 * (shading shift and toony), the parametric rim and matcap, and emission. It is unlit: the lighting
 * comes from the level's atmosphere sun light (the directional light with "Atmosphere Sun Light"
 * on) and the sky's distant-light scattering, or from the FallbackLight parameters when the level
 * has neither. It doesn't receive shadows, and point and spot lights don't light it.
 *
 * M_VRM_MToonOutline is the outline, drawn as the skeletal mesh's overlay material: the back faces,
 * pushed out along the normal by the outline width, in world or screen units.
 *
 * Both are built in C++ (these functions), because the plugin can't author .uasset files outside the
 * editor. FindOrCreateMToonMaterials makes them in the project the first time a VRM is imported.
 */
namespace VRM::MToon
{
	/** Bumped when the graphs change; older generated materials are rebuilt in place. */
	inline constexpr int32 GraphVersion = 1;

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

	/** Where FindOrCreateMToonMaterials puts the materials it generates. */
	inline constexpr const TCHAR* GeneratedFolder = TEXT("/Game/VRMInterchange/Materials");
	inline constexpr const TCHAR* SurfaceName = TEXT("M_VRM_MToon");
	inline constexpr const TCHAR* OutlineName = TEXT("M_VRM_MToonOutline");
	inline constexpr const TCHAR* WhiteMaskName = TEXT("T_VRM_MToonWhiteMask");

	/** A 1x1 white, linear, TC_Masks texture: the default for the data textures (shading shift, outline width). */
	UTexture2D* CreateWhiteMaskTexture(UObject* Outer, FName Name, EObjectFlags Flags);

	/**
	 * Replaces a material's graph and settings with the MToon surface (or outline) graph. WhiteMask
	 * is the default for the data textures. Returns false, with OutError set, if a node class is
	 * missing from the engine.
	 */
	bool BuildSurfaceMaterial(UMaterial& Material, UTexture* WhiteMask, FString& OutError);
	bool BuildOutlineMaterial(UMaterial& Material, UTexture* WhiteMask, FString& OutError);

	/** The GraphVersion a material was built with, or 0 if it isn't a generated MToon material. */
	int32 GetGraphVersion(const UMaterialInterface* Material);

	struct FMaterials
	{
		UMaterialInterface* Surface = nullptr;
		UMaterialInterface* Outline = nullptr;
	};

	/**
	 * The MToon materials, from GeneratedFolder. Creates them the first time, and rebuilds them if
	 * they were built by an older GraphVersion. Game thread only; the new or rebuilt assets are left
	 * dirty for the user to save with the imported assets.
	 */
	FMaterials FindOrCreateMToonMaterials(FString& OutError);
}
