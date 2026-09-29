// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "VRMMToonParameters.h"

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
