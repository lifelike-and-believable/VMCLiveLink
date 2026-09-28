// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// Materials (P4.5): glTF core, KHR_texture_transform, KHR_materials_emissive_strength,
// KHR_materials_unlit, VRMC_materials_mtoon (VRM 1.0) and VRM 0.x materialProperties.
#include "VRMMaterialTypes.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	using FJsonArray = TArray<TSharedPtr<FJsonValue>>;

	const FJsonObject* JObject(const FJsonObject* Parent, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* Value = nullptr;
		return Parent && Parent->TryGetObjectField(Field, Value) && Value && Value->IsValid() ? Value->Get() : nullptr;
	}

	const FJsonArray* JArray(const FJsonObject* Parent, const TCHAR* Field)
	{
		const FJsonArray* Value = nullptr;
		return Parent && Parent->TryGetArrayField(Field, Value) ? Value : nullptr;
	}

	FString JString(const FJsonObject* Parent, const TCHAR* Field)
	{
		FString Value;
		if (Parent)
		{
			Parent->TryGetStringField(Field, Value);
		}
		return Value;
	}

	bool JNumber(const FJsonObject* Parent, const TCHAR* Field, float& Out)
	{
		double Value = 0.0;
		if (Parent && Parent->TryGetNumberField(Field, Value) && FMath::IsFinite(Value))
		{
			Out = float(Value);
			return true;
		}
		return false;
	}

	// Reads up to Count numbers into Out; false (Out untouched) unless there are at least Count finite numbers.
	bool JNumbers(const FJsonObject* Parent, const TCHAR* Field, float* Out, int32 Count)
	{
		const FJsonArray* Array = JArray(Parent, Field);
		if (!Array || Array->Num() < Count)
		{
			return false;
		}
		float Values[4] = {};
		for (int32 i = 0; i < Count; ++i)
		{
			double Value = 0.0;
			if (!(*Array)[i].IsValid() || !(*Array)[i]->TryGetNumber(Value) || !FMath::IsFinite(Value))
			{
				return false;
			}
			Values[i] = float(Value);
		}
		FMemory::Memcpy(Out, Values, sizeof(float) * Count);
		return true;
	}

	bool JColor3(const FJsonObject* Parent, const TCHAR* Field, FLinearColor& Out)
	{
		float V[3];
		if (!JNumbers(Parent, Field, V, 3))
		{
			return false;
		}
		Out = FLinearColor(V[0], V[1], V[2], Out.A);
		return true;
	}

	bool JColor4(const FJsonObject* Parent, const TCHAR* Field, FLinearColor& Out)
	{
		float V[4];
		if (!JNumbers(Parent, Field, V, 4))
		{
			return false;
		}
		Out = FLinearColor(V[0], V[1], V[2], V[3]);
		return true;
	}

	// glTF texture index -> image index (textures[i].source), or INDEX_NONE.
	int32 ImageOfTexture(const FJsonArray* Textures, int32 TextureIndex)
	{
		if (!Textures || !Textures->IsValidIndex(TextureIndex) || !(*Textures)[TextureIndex].IsValid())
		{
			return INDEX_NONE;
		}
		const TSharedPtr<FJsonObject>* Texture = nullptr;
		double Source = -1.0;
		if (!(*Textures)[TextureIndex]->TryGetObject(Texture) || !Texture || !Texture->IsValid()
			|| !(*Texture)->TryGetNumberField(TEXT("source"), Source) || Source < 0.0)
		{
			return INDEX_NONE;
		}
		return int32(Source);
	}

	// A textureInfo object ({ "index": n, ... }) -> image index.
	int32 ImageOfTextureInfo(const FJsonArray* Textures, const FJsonObject* Info)
	{
		float Index = -1.f;
		return JNumber(Info, TEXT("index"), Index) && Index >= 0.f ? ImageOfTexture(Textures, int32(Index)) : INDEX_NONE;
	}

	void ReadTextureTransform(const FJsonObject* Info, FVRMTextureTransform& Out)
	{
		const FJsonObject* Transform = JObject(JObject(Info, TEXT("extensions")), TEXT("KHR_texture_transform"));
		if (!Transform)
		{
			return;
		}
		float V[2];
		if (JNumbers(Transform, TEXT("offset"), V, 2)) { Out.Offset = FVector2f(V[0], V[1]); }
		if (JNumbers(Transform, TEXT("scale"), V, 2)) { Out.Scale = FVector2f(V[0], V[1]); }
		JNumber(Transform, TEXT("rotation"), Out.Rotation);
	}

	// Unity's material colours (VRM 0.x) are gamma-encoded; glTF and VRM 1.0 factors are linear.
	float SRGBToLinear(float C)
	{
		return C <= 0.04045f ? C / 12.92f : FMath::Pow((C + 0.055f) / 1.055f, 2.4f);
	}

	FLinearColor SRGBToLinear(const FLinearColor& C)
	{
		return FLinearColor(SRGBToLinear(C.R), SRGBToLinear(C.G), SRGBToLinear(C.B), C.A);
	}

	void ReadGltfCore(const FJsonObject& Material, const FJsonArray* Textures, FVRMParsedMaterial& M)
	{
		if (const FJsonObject* Pbr = JObject(&Material, TEXT("pbrMetallicRoughness")))
		{
			JColor4(Pbr, TEXT("baseColorFactor"), M.BaseColorFactor);
			JNumber(Pbr, TEXT("metallicFactor"), M.MetallicFactor);
			JNumber(Pbr, TEXT("roughnessFactor"), M.RoughnessFactor);
			const FJsonObject* BaseColorInfo = JObject(Pbr, TEXT("baseColorTexture"));
			M.BaseColorTexture = ImageOfTextureInfo(Textures, BaseColorInfo);
			ReadTextureTransform(BaseColorInfo, M.UVTransform);
			M.MetallicRoughnessTexture = ImageOfTextureInfo(Textures, JObject(Pbr, TEXT("metallicRoughnessTexture")));
		}
		M.NormalTexture = ImageOfTextureInfo(Textures, JObject(&Material, TEXT("normalTexture")));
		M.OcclusionTexture = ImageOfTextureInfo(Textures, JObject(&Material, TEXT("occlusionTexture")));
		M.EmissiveTexture = ImageOfTextureInfo(Textures, JObject(&Material, TEXT("emissiveTexture")));
		JColor3(&Material, TEXT("emissiveFactor"), M.EmissiveFactor);
		M.EmissiveFactor.A = 1.f;

		bool bDoubleSided = false;
		if (Material.TryGetBoolField(TEXT("doubleSided"), bDoubleSided))
		{
			M.bDoubleSided = bDoubleSided;
		}
		const FString AlphaMode = JString(&Material, TEXT("alphaMode"));
		M.AlphaMode = AlphaMode == TEXT("MASK") ? EVRMAlphaMode::Mask : (AlphaMode == TEXT("BLEND") ? EVRMAlphaMode::Blend : EVRMAlphaMode::Opaque);
		JNumber(&Material, TEXT("alphaCutoff"), M.AlphaCutoff);

		const FJsonObject* Extensions = JObject(&Material, TEXT("extensions"));
		float Strength = 1.f;
		if (JNumber(JObject(Extensions, TEXT("KHR_materials_emissive_strength")), TEXT("emissiveStrength"), Strength))
		{
			M.EmissiveFactor = FLinearColor(M.EmissiveFactor.R * Strength, M.EmissiveFactor.G * Strength, M.EmissiveFactor.B * Strength, 1.f);
		}
		M.bUnlit = JObject(Extensions, TEXT("KHR_materials_unlit")) != nullptr;
	}

	// VRM 1.0: VRMC_materials_mtoon on the glTF material.
	void ReadMToon1(const FJsonObject& Ext, const FJsonArray* Textures, FVRMParsedMaterial& M)
	{
		FVRMMToon& T = M.MToon;
		M.bMToon = true;
		Ext.TryGetBoolField(TEXT("transparentWithZWrite"), T.bTransparentWithZWrite);
		float Queue = 0.f;
		if (JNumber(&Ext, TEXT("renderQueueOffsetNumber"), Queue))
		{
			T.RenderQueueOffset = FMath::RoundToInt(Queue);
		}

		JColor3(&Ext, TEXT("shadeColorFactor"), T.ShadeColor);
		T.ShadeMultiplyTexture = ImageOfTextureInfo(Textures, JObject(&Ext, TEXT("shadeMultiplyTexture")));
		JNumber(&Ext, TEXT("shadingShiftFactor"), T.ShadingShift);
		const FJsonObject* ShiftInfo = JObject(&Ext, TEXT("shadingShiftTexture"));
		T.ShadingShiftTexture = ImageOfTextureInfo(Textures, ShiftInfo);
		JNumber(ShiftInfo, TEXT("scale"), T.ShadingShiftTextureScale);
		JNumber(&Ext, TEXT("shadingToonyFactor"), T.ShadingToony);
		JNumber(&Ext, TEXT("giEqualizationFactor"), T.GIEqualization);

		JColor3(&Ext, TEXT("matcapFactor"), T.MatcapColor);
		T.MatcapTexture = ImageOfTextureInfo(Textures, JObject(&Ext, TEXT("matcapTexture")));

		JColor3(&Ext, TEXT("parametricRimColorFactor"), T.RimColor);
		T.RimMultiplyTexture = ImageOfTextureInfo(Textures, JObject(&Ext, TEXT("rimMultiplyTexture")));
		JNumber(&Ext, TEXT("rimLightingMixFactor"), T.RimLightingMix);
		JNumber(&Ext, TEXT("parametricRimFresnelPowerFactor"), T.RimFresnelPower);
		JNumber(&Ext, TEXT("parametricRimLiftFactor"), T.RimLift);

		const FString WidthMode = JString(&Ext, TEXT("outlineWidthMode"));
		T.OutlineWidthMode = WidthMode == TEXT("worldCoordinates") ? EVRMOutlineWidthMode::WorldCoordinates
			: (WidthMode == TEXT("screenCoordinates") ? EVRMOutlineWidthMode::ScreenCoordinates : EVRMOutlineWidthMode::None);
		JNumber(&Ext, TEXT("outlineWidthFactor"), T.OutlineWidth);
		T.OutlineWidthMultiplyTexture = ImageOfTextureInfo(Textures, JObject(&Ext, TEXT("outlineWidthMultiplyTexture")));
		JColor3(&Ext, TEXT("outlineColorFactor"), T.OutlineColor);
		JNumber(&Ext, TEXT("outlineLightingMixFactor"), T.OutlineLightingMix);
	}

	// VRM 0.x: one materialProperties entry, for the glTF material with the same index.
	void ReadMaterialProperties0(const FJsonObject& Props, const FJsonArray* Textures, FVRMParsedMaterial& M)
	{
		const FString Shader = JString(&Props, TEXT("shader"));
		const FJsonObject* Floats = JObject(&Props, TEXT("floatProperties"));
		const FJsonObject* Vectors = JObject(&Props, TEXT("vectorProperties"));
		const FJsonObject* TexProps = JObject(&Props, TEXT("textureProperties"));

		auto Texture = [&](const TCHAR* Name)
		{
			float Index = -1.f;
			return JNumber(TexProps, Name, Index) && Index >= 0.f ? ImageOfTexture(Textures, int32(Index)) : INDEX_NONE;
		};
		auto Float = [&](const TCHAR* Name, float Default)
		{
			float Value = Default;
			JNumber(Floats, Name, Value);
			return Value;
		};
		auto Color = [&](const TCHAR* Name, const FLinearColor& Default)
		{
			FLinearColor Value = Default;
			return JColor4(Vectors, Name, Value) ? SRGBToLinear(Value) : Default;
		};

		if (Shader.StartsWith(TEXT("VRM/Unlit")))
		{
			M.bUnlit = true;
			M.AlphaMode = Shader == TEXT("VRM/UnlitCutout") ? EVRMAlphaMode::Mask
				: (Shader.StartsWith(TEXT("VRM/UnlitTransparent")) ? EVRMAlphaMode::Blend : EVRMAlphaMode::Opaque);
			if (M.AlphaMode == EVRMAlphaMode::Mask)
			{
				M.AlphaCutoff = Float(TEXT("_Cutoff"), M.AlphaCutoff);
			}
			return;
		}
		if (Shader != TEXT("VRM/MToon"))
		{
			return; // "VRM_USE_GLTFSHADER" and anything else: the glTF material as it is
		}

		M.bMToon = true;
		FVRMMToon& T = M.MToon;

		// Base colour, normal, emission and alpha come from the MToon properties.
		M.BaseColorFactor = Color(TEXT("_Color"), FLinearColor::White);
		if (const int32 Main = Texture(TEXT("_MainTex")); Main != INDEX_NONE)
		{
			M.BaseColorTexture = Main;
		}
		float MainST[4];
		if (JNumbers(Vectors, TEXT("_MainTex"), MainST, 4))
		{
			// Unity's scale and offset, with V measured from the bottom; glTF measures V from the top.
			M.UVTransform = FVRMTextureTransform();
			M.UVTransform.Scale = FVector2f(MainST[0], MainST[1]);
			M.UVTransform.Offset = FVector2f(MainST[2], 1.f - MainST[1] - MainST[3]);
		}
		if (const int32 Bump = Texture(TEXT("_BumpMap")); Bump != INDEX_NONE)
		{
			M.NormalTexture = Bump;
		}
		M.EmissiveFactor = Color(TEXT("_EmissionColor"), FLinearColor::Black);
		M.EmissiveFactor.A = 1.f;
		if (const int32 Emission = Texture(TEXT("_EmissionMap")); Emission != INDEX_NONE)
		{
			M.EmissiveTexture = Emission;
		}

		// _BlendMode: 0 opaque, 1 cutout, 2 transparent, 3 transparent with z-write
		const int32 BlendMode = FMath::RoundToInt(Float(TEXT("_BlendMode"), 0.f));
		M.AlphaMode = BlendMode == 1 ? EVRMAlphaMode::Mask : (BlendMode >= 2 ? EVRMAlphaMode::Blend : EVRMAlphaMode::Opaque);
		M.AlphaCutoff = Float(TEXT("_Cutoff"), 0.5f);
		T.bTransparentWithZWrite = BlendMode == 3;
		// _CullMode: 0 off (both sides), 1 front, 2 back
		M.bDoubleSided = FMath::RoundToInt(Float(TEXT("_CullMode"), 2.f)) == 0;

		T.ShadeColor = Color(TEXT("_ShadeColor"), FLinearColor(0.97f, 0.81f, 0.86f, 1.f));
		T.ShadeMultiplyTexture = Texture(TEXT("_ShadeTexture"));
		const FVector2f Shading = VRM::MigrateMToon0Shading(Float(TEXT("_ShadeShift"), 0.f), Float(TEXT("_ShadeToony"), 0.9f));
		T.ShadingShift = Shading.X;
		T.ShadingToony = Shading.Y;
		T.GIEqualization = FMath::Clamp(1.f - Float(TEXT("_IndirectLightIntensity"), 0.1f), 0.f, 1.f);

		T.MatcapTexture = Texture(TEXT("_SphereAdd"));
		T.MatcapColor = FLinearColor::White;

		T.RimColor = Color(TEXT("_RimColor"), FLinearColor::Black);
		T.RimMultiplyTexture = Texture(TEXT("_RimTexture"));
		T.RimLightingMix = Float(TEXT("_RimLightingMix"), 0.f);
		T.RimFresnelPower = Float(TEXT("_RimFresnelPower"), 1.f);
		T.RimLift = Float(TEXT("_RimLift"), 0.f);

		// _OutlineWidthMode: 0 none, 1 world, 2 screen. _OutlineWidth is in centimetres (world) or
		// hundredths of the screen height (screen).
		const int32 WidthMode = FMath::RoundToInt(Float(TEXT("_OutlineWidthMode"), 0.f));
		T.OutlineWidthMode = WidthMode == 1 ? EVRMOutlineWidthMode::WorldCoordinates
			: (WidthMode == 2 ? EVRMOutlineWidthMode::ScreenCoordinates : EVRMOutlineWidthMode::None);
		T.OutlineWidth = Float(TEXT("_OutlineWidth"), 0.5f) * 0.01f;
		T.OutlineWidthMultiplyTexture = Texture(TEXT("_OutlineWidthTexture"));
		T.OutlineColor = Color(TEXT("_OutlineColor"), FLinearColor::Black);
		// _OutlineColorMode: 0 fixed colour, 1 mixed with the lighting
		T.OutlineLightingMix = FMath::RoundToInt(Float(TEXT("_OutlineColorMode"), 0.f)) == 1 ? Float(TEXT("_OutlineLightingMix"), 1.f) : 0.f;

		float Queue = 0.f;
		if (JNumber(&Props, TEXT("renderQueue"), Queue) && M.AlphaMode == EVRMAlphaMode::Blend)
		{
			T.RenderQueueOffset = FMath::RoundToInt(Queue) - 3000; // Unity's transparent queue starts at 3000
		}
	}
}

FVector2f VRM::MigrateMToon0Shading(float ShadeShift0, float ShadeToony0)
{
	// 0.x: smoothstep(ShadeShift, lerp(1, ShadeShift, ShadeToony), dot(N, L)).
	// 1.0: linearstep(-1 + Toony, 1 - Toony, dot(N, L) + Shift).
	// Match the ramp's start and end: its width gives Toony, its centre gives Shift.
	const float Min = ShadeShift0;
	const float Max = FMath::Lerp(1.f, ShadeShift0, ShadeToony0);
	const float Toony = FMath::Clamp((2.f - (Max - Min)) * 0.5f, 0.f, 1.f);
	const float Shift = FMath::Clamp(-(Max + Min) * 0.5f, -1.f, 1.f);
	return FVector2f(Shift, Toony);
}

TArray<FVRMParsedMaterial> VRM::ParseMaterials(const FJsonObject& Root)
{
	TArray<FVRMParsedMaterial> Out;
	const FJsonArray* Materials = JArray(&Root, TEXT("materials"));
	if (!Materials)
	{
		return Out;
	}
	const FJsonArray* Textures = JArray(&Root, TEXT("textures"));
	const FJsonArray* Properties0 = JArray(JObject(JObject(&Root, TEXT("extensions")), TEXT("VRM")), TEXT("materialProperties"));

	Out.SetNum(Materials->Num());
	for (int32 i = 0; i < Materials->Num(); ++i)
	{
		FVRMParsedMaterial& M = Out[i];
		const TSharedPtr<FJsonObject>* Material = nullptr;
		const bool bHasObject = (*Materials)[i].IsValid() && (*Materials)[i]->TryGetObject(Material) && Material && Material->IsValid();
		M.Name = bHasObject ? JString(Material->Get(), TEXT("name")) : FString();
		if (M.Name.IsEmpty())
		{
			M.Name = FString::Printf(TEXT("VRM_Mat_%d"), i);
		}
		if (!bHasObject)
		{
			continue;
		}

		ReadGltfCore(**Material, Textures, M);
		if (const FJsonObject* MToon1 = JObject(JObject(Material->Get(), TEXT("extensions")), TEXT("VRMC_materials_mtoon")))
		{
			ReadMToon1(*MToon1, Textures, M);
		}
		else if (Properties0 && Properties0->IsValidIndex(i) && (*Properties0)[i].IsValid())
		{
			const TSharedPtr<FJsonObject>* Props = nullptr;
			if ((*Properties0)[i]->TryGetObject(Props) && Props && Props->IsValid())
			{
				ReadMaterialProperties0(**Props, Textures, M);
			}
		}
	}
	return Out;
}
