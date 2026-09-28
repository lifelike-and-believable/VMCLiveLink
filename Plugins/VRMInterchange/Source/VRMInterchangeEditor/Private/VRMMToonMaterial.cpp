// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMMToonMaterial.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionCameraVectorWS.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionDistance.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionTwoSidedSign.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

// Named, not anonymous, so unity builds can't mix these up with other files' helpers.
namespace VRMMToonMaterialPrivate
{
	using namespace VRM::MToon;

	// Output indices of texture samples and vector parameters
	constexpr int32 OutRGB = 0;
	constexpr int32 OutR = 1;
	constexpr int32 OutA = 4;

	// Atmosphere nodes, looked up by class so a renamed engine class fails the build of the graph
	// (and the test) instead of the compile.
	const TCHAR* const SkyLightDirectionClass = TEXT("/Script/Engine.MaterialExpressionSkyAtmosphereLightDirection");
	const TCHAR* const SkyLightIlluminanceClass = TEXT("/Script/Engine.MaterialExpressionSkyAtmosphereLightIlluminance");
	const TCHAR* const SkyAmbientClass = TEXT("/Script/Engine.MaterialExpressionSkyAtmosphereDistantLightScatteredLuminance");

	// ---- HLSL of the custom nodes ----

	// KHR_texture_transform: uv * scale, rotated counter-clockwise, plus offset.
	const TCHAR* const UVCode = TEXT(
		"float2 S = UV * float2(ScaleU, ScaleV);\n"
		"float C = cos(Rotation);\n"
		"float Sn = sin(Rotation);\n"
		"return float2(C * S.x - Sn * S.y, Sn * S.x + C * S.y) + float2(OffsetU, OffsetV);\n");

	// normalTexture.scale on a tangent-space normal.
	const TCHAR* const NormalScaleCode = TEXT(
		"return normalize(float3(TangentNormal.xy * Scale, TangentNormal.z));\n");

	// The spec's matcap UV, from the view direction and normal, in Unreal's Z-up, left-handed world
	// and with V measured from the top of the texture.
	const TCHAR* const MatcapUVCode = TEXT(
		"float3 Vn = normalize(V);\n"
		"float3 X = normalize(float3(Vn.y, -Vn.x, 0.0) + float3(0.0, 1e-5, 0.0));\n"
		"float3 Y = cross(X, Vn);\n"
		"float3 Nn = normalize(N);\n"
		"return float2(dot(X, Nn) * 0.495 + 0.5, 0.5 - dot(Y, Nn) * 0.495);\n");

	// The light: the atmosphere sun light, as radiance on a white Lambert surface (illuminance / pi),
	// or the fallback when the level has none.
	const TCHAR* const LightColorCode = TEXT(
		"bool UseFallback = dot(L, L) < 0.25 || max(Illuminance.r, max(Illuminance.g, Illuminance.b)) <= 0.0;\n"
		"float3 LightColor = UseFallback ? FallbackColor : Illuminance * 0.31830989;\n");

	// MToon 1.0: toon ramp between shade and lit, direct light, uniform GI, rim and matcap, emission.
	const FString ShadeCode = FString(LightColorCode) + TEXT(
		"float3 Ln = normalize(UseFallback ? FallbackDirection : L);\n"
		"float3 Nn = normalize(N);\n"
		"float Shading = dot(Nn, Ln) + Shift + ShiftTexture * ShiftScale;\n"
		"float Lo = -1.0 + Toony;\n"
		"float Hi = 1.0 - Toony;\n"
		"Shading = saturate((Shading - Lo) / max(Hi - Lo, 1e-4));\n"
		"float3 Color = lerp(Shade, Lit, Shading) * LightColor;\n"
		"Color += Ambient * Lit;\n"
		"float3 Vn = normalize(V);\n"
		"float RimShape = pow(saturate(1.0 - dot(Nn, Vn) + RimLift), max(RimPower, 1e-5));\n"
		"float3 Rim = (Matcap + RimShape * RimColor) * RimMask;\n"
		"Rim *= lerp(float3(1.0, 1.0, 1.0), LightColor + Ambient, RimMix);\n"
		"return Color + Rim + Emissive;\n");

	// Outline colour, mixed with the light.
	const FString OutlineColorCode = FString(LightColorCode) + TEXT(
		"return Color * lerp(float3(1.0, 1.0, 1.0), LightColor + Ambient, Mix);\n");

	// Outline offset along the normal, in centimetres: world width is in metres, screen width is a
	// fraction of the screen height at the vertex's distance.
	const TCHAR* const OutlineOffsetCode = TEXT(
		"float Width = Factor * WidthTexture;\n"
		"float World = Width * 100.0;\n"
		"float Screen = Width * 2.0 * Distance / max(ResolvedView.ViewToClip[1][1], 1e-4);\n"
		"return normalize(Normal) * lerp(World, Screen, ScreenMode);\n");

	// Back faces only: the front faces of the pushed-out mesh are clipped.
	const TCHAR* const BackFaceCode = TEXT("return saturate(-Sign);\n");

	// ---- Graph building ----

	struct FInputLink
	{
		const TCHAR* Name;
		UMaterialExpression* Expression;
		int32 OutputIndex;
	};

	class FGraph
	{
	public:
		FGraph(UMaterial& InMaterial) : Material(InMaterial) {}

		template <class T>
		T* Add(int32 X, int32 Y)
		{
			return Cast<T>(UMaterialEditingLibrary::CreateMaterialExpression(&Material, T::StaticClass(), X, Y));
		}

		UMaterialExpression* AddByClass(const TCHAR* ClassPath, int32 X, int32 Y)
		{
			UClass* Class = FindObject<UClass>(nullptr, ClassPath);
			if (!Class || !Class->IsChildOf(UMaterialExpression::StaticClass()))
			{
				Missing.Add(ClassPath);
				return nullptr;
			}
			return UMaterialEditingLibrary::CreateMaterialExpression(&Material, Class, X, Y);
		}

		UMaterialExpression* Scalar(const TCHAR* Name, float Default, const TCHAR* Group, int32 X, int32 Y)
		{
			UMaterialExpressionScalarParameter* P = Add<UMaterialExpressionScalarParameter>(X, Y);
			P->ParameterName = Name;
			P->DefaultValue = Default;
			P->Group = Group;
			return P;
		}

		UMaterialExpression* Vector(const TCHAR* Name, const FLinearColor& Default, const TCHAR* Group, int32 X, int32 Y)
		{
			UMaterialExpressionVectorParameter* P = Add<UMaterialExpressionVectorParameter>(X, Y);
			P->ParameterName = Name;
			P->DefaultValue = Default;
			P->Group = Group;
			return P;
		}

		UMaterialExpression* Texture(const TCHAR* Name, UTexture* Default, EMaterialSamplerType Sampler, UMaterialExpression* UV, const TCHAR* Group, int32 X, int32 Y)
		{
			UMaterialExpressionTextureSampleParameter2D* P = Add<UMaterialExpressionTextureSampleParameter2D>(X, Y);
			P->ParameterName = Name;
			P->Texture = Default;
			P->SamplerType = Sampler;
			P->Group = Group;
			if (UV)
			{
				P->Coordinates.Connect(0, UV);
			}
			return P;
		}

		UMaterialExpression* Multiply(UMaterialExpression* A, int32 OutA, UMaterialExpression* B, int32 OutB, int32 X, int32 Y)
		{
			UMaterialExpressionMultiply* M = Add<UMaterialExpressionMultiply>(X, Y);
			M->A.Connect(OutA, A);
			M->B.Connect(OutB, B);
			return M;
		}

		UMaterialExpression* Custom(const TCHAR* Description, const FString& Code, ECustomMaterialOutputType OutputType, TArrayView<const FInputLink> Inputs, int32 X, int32 Y)
		{
			UMaterialExpressionCustom* C = Add<UMaterialExpressionCustom>(X, Y);
			C->Description = Description;
			C->Code = Code;
			C->OutputType = OutputType;
			C->Inputs.Reset();
			for (const FInputLink& Link : Inputs)
			{
				FCustomInput& In = C->Inputs.AddDefaulted_GetRef();
				In.InputName = Link.Name;
				if (Link.Expression)
				{
					In.Input.Connect(Link.OutputIndex, Link.Expression);
				}
			}
			return C;
		}

		/** TexCoord 0 through the KHR_texture_transform parameters. */
		UMaterialExpression* TransformedUV(int32 X, int32 Y)
		{
			UMaterialExpression* UV = Add<UMaterialExpressionTextureCoordinate>(X, Y);
			const FInputLink Inputs[] = {
				{ TEXT("UV"), UV, 0 },
				{ TEXT("OffsetU"), Scalar(Param::UVOffsetU, 0.f, TEXT("UV"), X, Y + 80), 0 },
				{ TEXT("OffsetV"), Scalar(Param::UVOffsetV, 0.f, TEXT("UV"), X, Y + 160), 0 },
				{ TEXT("ScaleU"), Scalar(Param::UVScaleU, 1.f, TEXT("UV"), X, Y + 240), 0 },
				{ TEXT("ScaleV"), Scalar(Param::UVScaleV, 1.f, TEXT("UV"), X, Y + 320), 0 },
				{ TEXT("Rotation"), Scalar(Param::UVRotation, 0.f, TEXT("UV"), X, Y + 400), 0 },
			};
			return Custom(TEXT("VRM UV transform"), UVCode, CMOT_Float2, Inputs, X + 300, Y);
		}

		/** The atmosphere light's direction and illuminance, the sky ambient, and the fallback light. */
		struct FLight
		{
			UMaterialExpression* Direction = nullptr;
			UMaterialExpression* Illuminance = nullptr;
			UMaterialExpression* Ambient = nullptr;
			UMaterialExpression* FallbackDirection = nullptr;
			UMaterialExpression* FallbackColor = nullptr;
		};

		FLight Light(int32 X, int32 Y)
		{
			FLight L;
			L.Direction = AddByClass(SkyLightDirectionClass, X, Y);
			L.Illuminance = AddByClass(SkyLightIlluminanceClass, X, Y + 80);
			L.Ambient = AddByClass(SkyAmbientClass, X, Y + 160);
			L.FallbackDirection = Vector(Param::FallbackLightDirection, FLinearColor(0.3f, -0.5f, 0.8f, 0.f), TEXT("Light"), X, Y + 240);
			L.FallbackColor = Vector(Param::FallbackLightColor, FLinearColor::White, TEXT("Light"), X, Y + 320);
			return L;
		}

		/** Resets the graph and the material outputs. */
		void Reset()
		{
			UMaterialEditingLibrary::DeleteAllMaterialExpressions(&Material);
			if (UMaterialEditorOnlyData* Ed = Material.GetEditorOnlyData())
			{
				Ed->BaseColor.Expression = nullptr;
				Ed->EmissiveColor.Expression = nullptr;
				Ed->Opacity.Expression = nullptr;
				Ed->OpacityMask.Expression = nullptr;
				Ed->Normal.Expression = nullptr;
				Ed->WorldPositionOffset.Expression = nullptr;
			}
		}

		bool Finish(FString& OutError)
		{
			if (Missing.Num() > 0)
			{
				OutError = FString::Printf(TEXT("Material node classes missing from the engine: %s"), *FString::Join(Missing, TEXT(", ")));
				return false;
			}
			UMaterialEditingLibrary::RecompileMaterial(&Material);
			return true;
		}

		UMaterial& Material;
		TArray<FString> Missing;
	};

	UTexture* EngineTexture(const TCHAR* Path)
	{
		return LoadObject<UTexture>(nullptr, Path);
	}

	void CommonSettings(UMaterial& Material)
	{
		Material.MaterialDomain = MD_Surface;
		Material.SetShadingModel(MSM_Unlit);
		Material.bUsedWithSkeletalMesh = true;
		Material.bUsedWithMorphTargets = true;
	}
}

UTexture2D* VRM::MToon::CreateWhiteMaskTexture(UObject* Outer, FName Name, EObjectFlags Flags)
{
	UTexture2D* Texture = NewObject<UTexture2D>(Outer, Name, Flags);
	const FColor White = FColor::White;
	Texture->Source.Init(1, 1, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(&White));
	Texture->SRGB = false;
	Texture->CompressionSettings = TC_Masks;
	Texture->MipGenSettings = TMGS_NoMipmaps;
	Texture->PostEditChange();
	return Texture;
}

bool VRM::MToon::BuildSurfaceMaterial(UMaterial& Material, UTexture* WhiteMask, FString& OutError)
{
	using namespace VRMMToonMaterialPrivate;
	UTexture* White = EngineTexture(TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	UTexture* FlatNormal = EngineTexture(TEXT("/Engine/EngineMaterials/DefaultNormal.DefaultNormal"));
	if (!White || !FlatNormal || !WhiteMask)
	{
		OutError = TEXT("Default textures for the MToon material are missing.");
		return false;
	}

	FGraph G(Material);
	G.Reset();
	CommonSettings(Material);
	Material.BlendMode = BLEND_Opaque; // instances override the blend mode and two-sidedness

	UMaterialExpression* UV = G.TransformedUV(-2400, 0);

	// Lit and shade colours
	UMaterialExpression* BaseTex = G.Texture(Param::BaseColorTexture, White, SAMPLERTYPE_Color, UV, TEXT("Lit"), -1800, -600);
	UMaterialExpression* BaseFactor = G.Vector(Param::BaseColorFactor, FLinearColor::White, TEXT("Lit"), -1800, -400);
	UMaterialExpression* Lit = G.Multiply(BaseTex, OutRGB, BaseFactor, OutRGB, -1500, -600);
	UMaterialExpression* Alpha = G.Multiply(BaseTex, OutA, BaseFactor, OutA, -1500, -450);
	UMaterialExpression* ShadeTex = G.Texture(Param::ShadeMultiplyTexture, White, SAMPLERTYPE_Color, UV, TEXT("Shade"), -1800, -250);
	UMaterialExpression* ShadeFactor = G.Vector(Param::ShadeColorFactor, FLinearColor::Black, TEXT("Shade"), -1800, -50);
	UMaterialExpression* Shade = G.Multiply(ShadeTex, OutRGB, ShadeFactor, OutRGB, -1500, -250);

	// Normal: the normal map in world space, facing the viewer on back faces
	UMaterialExpression* NormalTex = G.Texture(Param::NormalTexture, FlatNormal, SAMPLERTYPE_Normal, UV, TEXT("Normal"), -1800, 150);
	const FInputLink NormalScaleInputs[] = {
		{ TEXT("TangentNormal"), NormalTex, OutRGB },
		{ TEXT("Scale"), G.Scalar(Param::NormalScale, 1.f, TEXT("Normal"), -1800, 350), 0 },
	};
	UMaterialExpression* ScaledNormal = G.Custom(TEXT("VRM normal scale"), NormalScaleCode, CMOT_Float3, NormalScaleInputs, -1500, 150);
	UMaterialExpressionTransform* ToWorld = G.Add<UMaterialExpressionTransform>(-1250, 150);
	ToWorld->Input.Connect(0, ScaledNormal);
	ToWorld->TransformSourceType = TRANSFORMSOURCE_Tangent;
	ToWorld->TransformType = TRANSFORM_World;
	UMaterialExpression* Normal = G.Multiply(ToWorld, 0, G.Add<UMaterialExpressionTwoSidedSign>(-1250, 250), 0, -1000, 150);

	// Shading shift
	UMaterialExpressionStaticSwitchParameter* UseShift = G.Add<UMaterialExpressionStaticSwitchParameter>(-1500, 450);
	UseShift->ParameterName = Param::UseShadingShiftTexture;
	UseShift->DefaultValue = false;
	UseShift->Group = TEXT("Shading");
	UseShift->A.Connect(OutR, G.Texture(Param::ShadingShiftTexture, WhiteMask, SAMPLERTYPE_Masks, UV, TEXT("Shading"), -1800, 450));
	UMaterialExpressionConstant* Zero = G.Add<UMaterialExpressionConstant>(-1800, 650);
	Zero->R = 0.f;
	UseShift->B.Connect(0, Zero);

	// Rim and matcap
	UMaterialExpression* View = G.Add<UMaterialExpressionCameraVectorWS>(-1250, 800);
	const FInputLink MatcapUVInputs[] = { { TEXT("V"), View, 0 }, { TEXT("N"), Normal, 0 } };
	UMaterialExpression* MatcapUV = G.Custom(TEXT("VRM matcap UV"), MatcapUVCode, CMOT_Float2, MatcapUVInputs, -1000, 800);
	UMaterialExpression* MatcapTex = G.Texture(Param::MatcapTexture, White, SAMPLERTYPE_Color, MatcapUV, TEXT("Rim"), -750, 800);
	UMaterialExpression* Matcap = G.Multiply(MatcapTex, OutRGB, G.Vector(Param::MatcapFactor, FLinearColor::Black, TEXT("Rim"), -750, 1000), OutRGB, -500, 800);
	UMaterialExpression* RimMask = G.Texture(Param::RimMultiplyTexture, White, SAMPLERTYPE_Color, UV, TEXT("Rim"), -750, 1150);

	// Emission
	UMaterialExpression* EmissiveTex = G.Texture(Param::EmissiveTexture, White, SAMPLERTYPE_Color, UV, TEXT("Emission"), -1800, 1300);
	UMaterialExpression* Emissive = G.Multiply(EmissiveTex, OutRGB, G.Vector(Param::EmissiveFactor, FLinearColor::Black, TEXT("Emission"), -1800, 1500), OutRGB, -1500, 1300);

	const FGraph::FLight Light = G.Light(-1000, -900);

	const FInputLink ShadeInputs[] = {
		{ TEXT("Lit"), Lit, 0 },
		{ TEXT("Shade"), Shade, 0 },
		{ TEXT("N"), Normal, 0 },
		{ TEXT("V"), View, 0 },
		{ TEXT("L"), Light.Direction, 0 },
		{ TEXT("Illuminance"), Light.Illuminance, 0 },
		{ TEXT("Ambient"), Light.Ambient, 0 },
		{ TEXT("FallbackDirection"), Light.FallbackDirection, OutRGB },
		{ TEXT("FallbackColor"), Light.FallbackColor, OutRGB },
		{ TEXT("ShiftTexture"), UseShift, 0 },
		{ TEXT("ShiftScale"), G.Scalar(Param::ShadingShiftTextureScale, 1.f, TEXT("Shading"), -750, 300), 0 },
		{ TEXT("Shift"), G.Scalar(Param::ShadingShiftFactor, 0.f, TEXT("Shading"), -750, 380), 0 },
		{ TEXT("Toony"), G.Scalar(Param::ShadingToonyFactor, 0.9f, TEXT("Shading"), -750, 460), 0 },
		{ TEXT("Matcap"), Matcap, 0 },
		{ TEXT("RimColor"), G.Vector(Param::ParametricRimColorFactor, FLinearColor::Black, TEXT("Rim"), -500, 1000), OutRGB },
		{ TEXT("RimMask"), RimMask, OutRGB },
		{ TEXT("RimMix"), G.Scalar(Param::RimLightingMixFactor, 1.f, TEXT("Rim"), -500, 1150), 0 },
		{ TEXT("RimPower"), G.Scalar(Param::ParametricRimFresnelPowerFactor, 5.f, TEXT("Rim"), -500, 1230), 0 },
		{ TEXT("RimLift"), G.Scalar(Param::ParametricRimLiftFactor, 0.f, TEXT("Rim"), -500, 1310), 0 },
		{ TEXT("Emissive"), Emissive, 0 },
		{ TEXT("Version"), G.Scalar(Param::GraphVersion, float(GraphVersion), TEXT("Internal"), -500, 1450), 0 },
	};
	UMaterialExpression* Color = G.Custom(TEXT("VRM MToon shading"), ShadeCode, CMOT_Float3, ShadeInputs, -200, 0);

	UMaterialEditorOnlyData* Ed = Material.GetEditorOnlyData();
	Ed->EmissiveColor.Connect(0, Color);
	Ed->Opacity.Connect(0, Alpha);
	Ed->OpacityMask.Connect(0, Alpha);
	return G.Finish(OutError);
}

bool VRM::MToon::BuildOutlineMaterial(UMaterial& Material, UTexture* WhiteMask, FString& OutError)
{
	using namespace VRMMToonMaterialPrivate;
	if (!WhiteMask)
	{
		OutError = TEXT("Default texture for the MToon outline material is missing.");
		return false;
	}

	FGraph G(Material);
	G.Reset();
	CommonSettings(Material);
	Material.BlendMode = BLEND_Masked;
	Material.TwoSided = true;
	Material.OpacityMaskClipValue = 0.5f;

	UMaterialExpression* UV = G.TransformedUV(-1800, 0);

	// Offset along the vertex normal
	UMaterialExpressionWorldPosition* Position = G.Add<UMaterialExpressionWorldPosition>(-1200, -300);
	Position->WorldPositionShaderOffset = WPT_ExcludeAllShaderOffsets;
	UMaterialExpressionDistance* Distance = G.Add<UMaterialExpressionDistance>(-900, -300);
	Distance->A.Connect(0, G.Add<UMaterialExpressionCameraPositionWS>(-1200, -400));
	Distance->B.Connect(0, Position);
	const FInputLink OffsetInputs[] = {
		{ TEXT("Normal"), G.Add<UMaterialExpressionVertexNormalWS>(-900, -500), 0 },
		{ TEXT("Factor"), G.Scalar(Param::OutlineWidthFactor, 0.f, TEXT("Outline"), -900, -150), 0 },
		{ TEXT("WidthTexture"), G.Texture(Param::OutlineWidthMultiplyTexture, WhiteMask, SAMPLERTYPE_Masks, UV, TEXT("Outline"), -1200, 0), OutR },
		{ TEXT("Distance"), Distance, 0 },
		{ TEXT("ScreenMode"), G.Scalar(Param::OutlineScreenCoordinates, 0.f, TEXT("Outline"), -900, -50), 0 },
	};
	UMaterialExpression* Offset = G.Custom(TEXT("VRM outline offset"), OutlineOffsetCode, CMOT_Float3, OffsetInputs, -500, -300);

	// Back faces only
	const FInputLink BackFaceInputs[] = { { TEXT("Sign"), G.Add<UMaterialExpressionTwoSidedSign>(-900, 300), 0 } };
	UMaterialExpression* BackFace = G.Custom(TEXT("VRM back faces"), BackFaceCode, CMOT_Float1, BackFaceInputs, -500, 300);

	// Colour
	const FGraph::FLight Light = G.Light(-1200, 500);
	const FInputLink ColorInputs[] = {
		{ TEXT("Color"), G.Vector(Param::OutlineColorFactor, FLinearColor::Black, TEXT("Outline"), -900, 450), OutRGB },
		{ TEXT("Mix"), G.Scalar(Param::OutlineLightingMixFactor, 1.f, TEXT("Outline"), -900, 650), 0 },
		{ TEXT("L"), Light.Direction, 0 },
		{ TEXT("Illuminance"), Light.Illuminance, 0 },
		{ TEXT("Ambient"), Light.Ambient, 0 },
		{ TEXT("FallbackColor"), Light.FallbackColor, OutRGB },
		{ TEXT("FallbackDirection"), Light.FallbackDirection, OutRGB }, // unused; keeps the parameter set the same as the surface's
		{ TEXT("Version"), G.Scalar(Param::GraphVersion, float(GraphVersion), TEXT("Internal"), -900, 800), 0 },
	};
	UMaterialExpression* Color = G.Custom(TEXT("VRM outline colour"), OutlineColorCode, CMOT_Float3, ColorInputs, -500, 500);

	UMaterialEditorOnlyData* Ed = Material.GetEditorOnlyData();
	Ed->EmissiveColor.Connect(0, Color);
	Ed->OpacityMask.Connect(0, BackFace);
	Ed->WorldPositionOffset.Connect(0, Offset);
	return G.Finish(OutError);
}

int32 VRM::MToon::GetGraphVersion(const UMaterialInterface* Material)
{
	float Version = 0.f;
	if (Material && Material->GetScalarParameterDefaultValue(FHashedMaterialParameterInfo(FName(Param::GraphVersion)), Version))
	{
		return FMath::RoundToInt(Version);
	}
	return 0;
}

namespace VRMMToonMaterialPrivate
{
	template <class T>
	T* FindOrLoad(const FString& PackagePath, const TCHAR* Name)
	{
		const FString ObjectPath = PackagePath + TEXT(".") + Name;
		if (T* InMemory = FindObject<T>(nullptr, *ObjectPath))
		{
			return InMemory;
		}
		return FPackageName::DoesPackageExist(PackagePath) ? LoadObject<T>(nullptr, *ObjectPath) : nullptr;
	}

	UPackage* NewAssetPackage(const FString& PackagePath)
	{
		UPackage* Package = CreatePackage(*PackagePath);
		Package->FullyLoad();
		return Package;
	}
}

VRM::MToon::FMaterials VRM::MToon::FindOrCreateMToonMaterials(FString& OutError)
{
	using namespace VRMMToonMaterialPrivate;
	check(IsInGameThread());
	FMaterials Out;
	const FString Folder = GeneratedFolder;

	UTexture2D* WhiteMask = FindOrLoad<UTexture2D>(Folder / WhiteMaskName, WhiteMaskName);
	if (!WhiteMask)
	{
		UPackage* Package = NewAssetPackage(Folder / WhiteMaskName);
		WhiteMask = CreateWhiteMaskTexture(Package, WhiteMaskName, RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(WhiteMask);
		Package->MarkPackageDirty();
	}

	auto Get = [&](const TCHAR* Name, bool (*Build)(UMaterial&, UTexture*, FString&)) -> UMaterialInterface*
	{
		const FString PackagePath = Folder / Name;
		UMaterial* Material = FindOrLoad<UMaterial>(PackagePath, Name);
		const bool bNew = Material == nullptr;
		if (bNew)
		{
			Material = NewObject<UMaterial>(NewAssetPackage(PackagePath), Name, RF_Public | RF_Standalone | RF_Transactional);
		}
		else if (GetGraphVersion(Material) >= GraphVersion)
		{
			return Material;
		}
		FString Error;
		if (!Build(*Material, WhiteMask, Error))
		{
			OutError = FString::Printf(TEXT("Could not build %s: %s"), Name, *Error);
			return nullptr;
		}
		if (bNew)
		{
			FAssetRegistryModule::AssetCreated(Material);
		}
		Material->MarkPackageDirty();
		return Material;
	};

	Out.Surface = Get(SurfaceName, &BuildSurfaceMaterial);
	Out.Outline = Out.Surface ? Get(OutlineName, &BuildOutlineMaterial) : nullptr;
	return Out;
}
