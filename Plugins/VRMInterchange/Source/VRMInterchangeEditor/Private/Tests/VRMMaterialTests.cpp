// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// Tests for the material parser (P4.5): glTF core, KHR extensions, VRM 1.0 MToon and VRM 0.x MToon.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "VRMMaterialTypes.h"

namespace VRMMaterialTests
{
	TArray<FVRMParsedMaterial> Parse(FAutomationTestBase& Test, const TCHAR* Json)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!Test.TestTrue(TEXT("JSON parses"), FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid()))
		{
			return {};
		}
		return VRM::ParseMaterials(*Root);
	}

	void TestColor(FAutomationTestBase& Test, const TCHAR* What, const FLinearColor& Actual, const FLinearColor& Expected)
	{
		Test.TestTrue(FString::Printf(TEXT("%s: expected %s, got %s"), What, *Expected.ToString(), *Actual.ToString()), Actual.Equals(Expected, 1e-3f));
	}

	// The textures list images in reverse, so a texture index is never its image index.
	const TCHAR* const Textures = TEXT(R"("textures":[{"source":5},{"source":4},{"source":3},{"source":2},{"source":1},{"source":0}],)");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMMaterialsGltf, "VRM.Materials.Gltf",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMMaterialsGltf::RunTest(const FString& Parameters)
{
	using namespace VRMMaterialTests;
	const FString Json = FString(TEXT("{")) + Textures + TEXT(R"json(
		"materials":[
			{"name":"Skin",
			 "pbrMetallicRoughness":{"baseColorFactor":[0.5,0.25,1,0.75],"metallicFactor":0.2,"roughnessFactor":0.7,
				"baseColorTexture":{"index":0,"extensions":{"KHR_texture_transform":{"offset":[0.1,0.2],"scale":[2,3],"rotation":0.5}}},
				"metallicRoughnessTexture":{"index":1}},
			 "normalTexture":{"index":2},"occlusionTexture":{"index":3},"emissiveTexture":{"index":4},
			 "emissiveFactor":[1,0.5,0],"doubleSided":true,"alphaMode":"MASK","alphaCutoff":0.3,
			 "extensions":{"KHR_materials_emissive_strength":{"emissiveStrength":4}}},
			{"alphaMode":"BLEND","extensions":{"KHR_materials_unlit":{}},
			 "pbrMetallicRoughness":{"baseColorTexture":{"index":99}}},
			{}
		]})json");

	const TArray<FVRMParsedMaterial> Materials = Parse(*this, *Json);
	if (!TestEqual(TEXT("Three materials"), Materials.Num(), 3))
	{
		return false;
	}

	const FVRMParsedMaterial& A = Materials[0];
	TestEqual(TEXT("Name"), A.Name, FString(TEXT("Skin")));
	TestEqual(TEXT("Base colour texture is the texture's image"), A.BaseColorTexture, 5);
	TestEqual(TEXT("Metallic-roughness texture"), A.MetallicRoughnessTexture, 4);
	TestEqual(TEXT("Normal texture"), A.NormalTexture, 3);
	TestEqual(TEXT("Occlusion texture"), A.OcclusionTexture, 2);
	TestEqual(TEXT("Emissive texture"), A.EmissiveTexture, 1);
	TestColor(*this, TEXT("Base colour factor"), A.BaseColorFactor, FLinearColor(0.5f, 0.25f, 1.f, 0.75f));
	TestColor(*this, TEXT("Emissive factor times strength"), A.EmissiveFactor, FLinearColor(4.f, 2.f, 0.f, 1.f));
	TestEqual(TEXT("Metallic"), A.MetallicFactor, 0.2f, 1e-5f);
	TestEqual(TEXT("Roughness"), A.RoughnessFactor, 0.7f, 1e-5f);
	TestTrue(TEXT("Double sided"), A.bDoubleSided);
	TestTrue(TEXT("Mask"), A.AlphaMode == EVRMAlphaMode::Mask);
	TestEqual(TEXT("Cutoff"), A.AlphaCutoff, 0.3f, 1e-5f);
	TestTrue(TEXT("Texture transform offset"), A.UVTransform.Offset.Equals(FVector2f(0.1f, 0.2f), 1e-5f));
	TestTrue(TEXT("Texture transform scale"), A.UVTransform.Scale.Equals(FVector2f(2.f, 3.f), 1e-5f));
	TestEqual(TEXT("Texture transform rotation"), A.UVTransform.Rotation, 0.5f, 1e-5f);
	TestFalse(TEXT("Not unlit"), A.bUnlit);
	TestFalse(TEXT("Not MToon"), A.bMToon);

	const FVRMParsedMaterial& B = Materials[1];
	TestEqual(TEXT("Unnamed material gets an index name"), B.Name, FString(TEXT("VRM_Mat_1")));
	TestTrue(TEXT("Unlit"), B.bUnlit);
	TestTrue(TEXT("Blend"), B.AlphaMode == EVRMAlphaMode::Blend);
	TestEqual(TEXT("Out-of-range texture is ignored"), B.BaseColorTexture, int32(INDEX_NONE));

	const FVRMParsedMaterial& C = Materials[2];
	TestTrue(TEXT("Defaults: opaque"), C.AlphaMode == EVRMAlphaMode::Opaque);
	TestFalse(TEXT("Defaults: single sided"), C.bDoubleSided);
	TestColor(*this, TEXT("Defaults: white base colour"), C.BaseColorFactor, FLinearColor::White);
	TestColor(*this, TEXT("Defaults: no emission"), C.EmissiveFactor, FLinearColor::Black);
	TestEqual(TEXT("Defaults: metallic 1"), C.MetallicFactor, 1.f, 1e-5f);
	TestTrue(TEXT("Defaults: identity texture transform"), C.UVTransform.IsIdentity());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMMaterialsMToon1, "VRM.Materials.MToon1",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMMaterialsMToon1::RunTest(const FString& Parameters)
{
	using namespace VRMMaterialTests;
	const FString Json = FString(TEXT("{")) + Textures + TEXT(R"json(
		"materials":[
			{"name":"Body","extensions":{"VRMC_materials_mtoon":{
				"specVersion":"1.0","transparentWithZWrite":true,"renderQueueOffsetNumber":2,
				"shadeColorFactor":[0.3,0.2,0.1],"shadeMultiplyTexture":{"index":0},
				"shadingShiftFactor":-0.2,"shadingShiftTexture":{"index":1,"scale":0.5},"shadingToonyFactor":0.8,
				"giEqualizationFactor":0.4,"matcapFactor":[0.5,0.5,0.5],"matcapTexture":{"index":2},
				"parametricRimColorFactor":[1,0,0],"rimMultiplyTexture":{"index":3},"rimLightingMixFactor":0.25,
				"parametricRimFresnelPowerFactor":3,"parametricRimLiftFactor":0.1,
				"outlineWidthMode":"worldCoordinates","outlineWidthFactor":0.002,"outlineWidthMultiplyTexture":{"index":4},
				"outlineColorFactor":[0,0,1],"outlineLightingMixFactor":0.5}}},
			{"extensions":{"VRMC_materials_mtoon":{"specVersion":"1.0","outlineWidthMode":"screenCoordinates"}}}
		]})json");

	const TArray<FVRMParsedMaterial> Materials = Parse(*this, *Json);
	if (!TestEqual(TEXT("Two materials"), Materials.Num(), 2))
	{
		return false;
	}

	const FVRMParsedMaterial& A = Materials[0];
	const FVRMMToon& T = A.MToon;
	TestTrue(TEXT("MToon"), A.bMToon);
	TestTrue(TEXT("Transparent with z-write"), T.bTransparentWithZWrite);
	TestEqual(TEXT("Render queue offset"), T.RenderQueueOffset, 2);
	TestColor(*this, TEXT("Shade colour"), T.ShadeColor, FLinearColor(0.3f, 0.2f, 0.1f));
	TestEqual(TEXT("Shade texture"), T.ShadeMultiplyTexture, 5);
	TestEqual(TEXT("Shading shift"), T.ShadingShift, -0.2f, 1e-5f);
	TestEqual(TEXT("Shading shift texture"), T.ShadingShiftTexture, 4);
	TestEqual(TEXT("Shading shift texture scale"), T.ShadingShiftTextureScale, 0.5f, 1e-5f);
	TestEqual(TEXT("Toony"), T.ShadingToony, 0.8f, 1e-5f);
	TestEqual(TEXT("GI equalization"), T.GIEqualization, 0.4f, 1e-5f);
	TestColor(*this, TEXT("Matcap colour"), T.MatcapColor, FLinearColor(0.5f, 0.5f, 0.5f));
	TestEqual(TEXT("Matcap texture"), T.MatcapTexture, 3);
	TestColor(*this, TEXT("Rim colour"), T.RimColor, FLinearColor(1.f, 0.f, 0.f));
	TestEqual(TEXT("Rim texture"), T.RimMultiplyTexture, 2);
	TestEqual(TEXT("Rim lighting mix"), T.RimLightingMix, 0.25f, 1e-5f);
	TestEqual(TEXT("Rim fresnel power"), T.RimFresnelPower, 3.f, 1e-5f);
	TestEqual(TEXT("Rim lift"), T.RimLift, 0.1f, 1e-5f);
	TestTrue(TEXT("World outline"), T.OutlineWidthMode == EVRMOutlineWidthMode::WorldCoordinates);
	TestEqual(TEXT("Outline width"), T.OutlineWidth, 0.002f, 1e-6f);
	TestEqual(TEXT("Outline width texture"), T.OutlineWidthMultiplyTexture, 1);
	TestColor(*this, TEXT("Outline colour"), T.OutlineColor, FLinearColor(0.f, 0.f, 1.f));
	TestEqual(TEXT("Outline lighting mix"), T.OutlineLightingMix, 0.5f, 1e-5f);
	TestTrue(TEXT("Has outline"), T.HasOutline());

	// Spec defaults
	const FVRMMToon& D = Materials[1].MToon;
	TestTrue(TEXT("Defaults: MToon"), Materials[1].bMToon);
	TestColor(*this, TEXT("Defaults: white shade"), D.ShadeColor, FLinearColor::White);
	TestEqual(TEXT("Defaults: toony 0.9"), D.ShadingToony, 0.9f, 1e-5f);
	TestEqual(TEXT("Defaults: GI equalization 0.9"), D.GIEqualization, 0.9f, 1e-5f);
	TestEqual(TEXT("Defaults: fresnel power 5"), D.RimFresnelPower, 5.f, 1e-5f);
	TestColor(*this, TEXT("Defaults: black rim"), D.RimColor, FLinearColor::Black);
	TestTrue(TEXT("Screen outline mode"), D.OutlineWidthMode == EVRMOutlineWidthMode::ScreenCoordinates);
	TestFalse(TEXT("Zero width: no outline"), D.HasOutline());
	TestEqual(TEXT("Defaults: no matcap texture"), D.MatcapTexture, int32(INDEX_NONE));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMMaterialsMToon0, "VRM.Materials.MToon0",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMMaterialsMToon0::RunTest(const FString& Parameters)
{
	using namespace VRMMaterialTests;
	// Material 0 is MToon, 1 is unlit cutout, 2 uses the glTF shader, 3 has no entry.
	const FString Json = FString(TEXT("{")) + Textures + TEXT(R"json(
		"materials":[
			{"name":"Face","pbrMetallicRoughness":{"baseColorTexture":{"index":5}}},
			{"name":"Hair","pbrMetallicRoughness":{"baseColorTexture":{"index":5}}},
			{"name":"Eyes","alphaMode":"BLEND","doubleSided":true},
			{"name":"Extra"}
		],
		"extensions":{"VRM":{"materialProperties":[
			{"name":"Face","shader":"VRM/MToon","renderQueue":3002,
			 "floatProperties":{"_BlendMode":1,"_Cutoff":0.4,"_CullMode":0,"_ShadeShift":-0.5,"_ShadeToony":0.5,
				"_IndirectLightIntensity":0.25,"_RimLightingMix":0.5,"_RimFresnelPower":2,"_RimLift":0.1,
				"_OutlineWidthMode":1,"_OutlineWidth":0.2,"_OutlineColorMode":1,"_OutlineLightingMix":0.75},
			 "vectorProperties":{"_Color":[0.5,1,0,0.5],"_ShadeColor":[0.5,0.5,0.5,1],"_RimColor":[1,1,1,1],
				"_OutlineColor":[0,0,0,1],"_EmissionColor":[0.5,0,0,1],"_MainTex":[2,4,0.25,0.5]},
			 "textureProperties":{"_MainTex":0,"_ShadeTexture":1,"_BumpMap":2,"_SphereAdd":3,"_EmissionMap":4,
				"_RimTexture":1,"_OutlineWidthTexture":2}},
			{"name":"Hair","shader":"VRM/UnlitCutout","floatProperties":{"_Cutoff":0.6}},
			{"name":"Eyes","shader":"VRM_USE_GLTFSHADER"}
		]}}})json");

	const TArray<FVRMParsedMaterial> Materials = Parse(*this, *Json);
	if (!TestEqual(TEXT("Four materials"), Materials.Num(), 4))
	{
		return false;
	}

	const FVRMParsedMaterial& A = Materials[0];
	const FVRMMToon& T = A.MToon;
	const float Half = 0.21404f; // sRGB 0.5 in linear
	TestTrue(TEXT("MToon"), A.bMToon);
	TestColor(*this, TEXT("_Color is converted to linear, alpha kept"), A.BaseColorFactor, FLinearColor(Half, 1.f, 0.f, 0.5f));
	TestEqual(TEXT("_MainTex replaces the glTF base colour texture"), A.BaseColorTexture, 5);
	TestEqual(TEXT("_BumpMap"), A.NormalTexture, 3);
	TestEqual(TEXT("_EmissionMap"), A.EmissiveTexture, 1);
	TestColor(*this, TEXT("_EmissionColor"), A.EmissiveFactor, FLinearColor(Half, 0.f, 0.f, 1.f));
	TestTrue(TEXT("_MainTex scale"), A.UVTransform.Scale.Equals(FVector2f(2.f, 4.f), 1e-5f));
	TestTrue(TEXT("_MainTex offset, V from the top"), A.UVTransform.Offset.Equals(FVector2f(0.25f, 1.f - 4.f - 0.5f), 1e-5f));
	TestTrue(TEXT("_BlendMode 1 is cutout"), A.AlphaMode == EVRMAlphaMode::Mask);
	TestEqual(TEXT("_Cutoff"), A.AlphaCutoff, 0.4f, 1e-5f);
	TestTrue(TEXT("_CullMode 0 is double sided"), A.bDoubleSided);
	TestFalse(TEXT("No z-write"), T.bTransparentWithZWrite);
	TestEqual(TEXT("Render queue only counts for transparent"), T.RenderQueueOffset, 0);

	TestColor(*this, TEXT("_ShadeColor"), T.ShadeColor, FLinearColor(Half, Half, Half));
	TestEqual(TEXT("_ShadeTexture"), T.ShadeMultiplyTexture, 4);
	// 0.x ramp from -0.5 to lerp(1, -0.5, 0.5) = 0.25: toony (2 - 0.75) / 2, shift -(0.25 - 0.5) / 2.
	TestEqual(TEXT("Toony migrated"), T.ShadingToony, 0.625f, 1e-5f);
	TestEqual(TEXT("Shift migrated"), T.ShadingShift, 0.125f, 1e-5f);
	TestEqual(TEXT("GI equalization is 1 - _IndirectLightIntensity"), T.GIEqualization, 0.75f, 1e-5f);
	TestEqual(TEXT("_SphereAdd is the matcap"), T.MatcapTexture, 2);
	TestColor(*this, TEXT("Matcap colour white"), T.MatcapColor, FLinearColor::White);
	TestColor(*this, TEXT("_RimColor"), T.RimColor, FLinearColor::White);
	TestEqual(TEXT("_RimTexture"), T.RimMultiplyTexture, 4);
	TestEqual(TEXT("_RimLightingMix"), T.RimLightingMix, 0.5f, 1e-5f);
	TestEqual(TEXT("_RimFresnelPower"), T.RimFresnelPower, 2.f, 1e-5f);
	TestEqual(TEXT("_RimLift"), T.RimLift, 0.1f, 1e-5f);
	TestTrue(TEXT("_OutlineWidthMode 1 is world"), T.OutlineWidthMode == EVRMOutlineWidthMode::WorldCoordinates);
	TestEqual(TEXT("_OutlineWidth centimetres to metres"), T.OutlineWidth, 0.002f, 1e-6f);
	TestEqual(TEXT("_OutlineWidthTexture"), T.OutlineWidthMultiplyTexture, 3);
	TestEqual(TEXT("Mixed outline colour uses _OutlineLightingMix"), T.OutlineLightingMix, 0.75f, 1e-5f);

	const FVRMParsedMaterial& B = Materials[1];
	TestTrue(TEXT("Unlit shader"), B.bUnlit);
	TestFalse(TEXT("Unlit is not MToon"), B.bMToon);
	TestTrue(TEXT("Unlit cutout is masked"), B.AlphaMode == EVRMAlphaMode::Mask);
	TestEqual(TEXT("Unlit cutoff"), B.AlphaCutoff, 0.6f, 1e-5f);
	TestEqual(TEXT("Unlit keeps the glTF texture"), B.BaseColorTexture, 0);

	const FVRMParsedMaterial& C = Materials[2];
	TestFalse(TEXT("glTF shader: not MToon"), C.bMToon);
	TestTrue(TEXT("glTF shader: keeps the glTF alpha mode"), C.AlphaMode == EVRMAlphaMode::Blend);
	TestTrue(TEXT("glTF shader: keeps double-sided"), C.bDoubleSided);

	TestFalse(TEXT("No entry: not MToon"), Materials[3].bMToon);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMMaterialsMToon0Shading, "VRM.Materials.MToon0Shading",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMMaterialsMToon0Shading::RunTest(const FString& Parameters)
{
	// The migrated 1.0 ramp starts and ends where the 0.x ramp did.
	const float Cases[][2] = { { 0.f, 0.9f }, { -0.5f, 0.5f }, { 0.3f, 0.f }, { -1.f, 1.f } };
	for (const auto& Case : Cases)
	{
		const float Shift0 = Case[0];
		const float Toony0 = Case[1];
		const FVector2f V1 = VRM::MigrateMToon0Shading(Shift0, Toony0);
		const float Start1 = -1.f + V1.Y - V1.X;
		const float End1 = 1.f - V1.Y - V1.X;
		const FString What = FString::Printf(TEXT("shift %.2f, toony %.2f"), Shift0, Toony0);
		TestEqual(*(What + TEXT(": ramp start")), Start1, Shift0, 1e-4f);
		TestEqual(*(What + TEXT(": ramp end")), End1, FMath::Lerp(1.f, Shift0, Toony0), 1e-4f);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
