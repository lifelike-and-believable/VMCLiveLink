// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// Tests for the generated MToon materials (P4.5): parameters, settings, and that they compile.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "MaterialShared.h"
#include "RHI.h"
#include "UObject/Package.h"
#include "VRMMToonMaterial.h"

namespace VRMMToonMaterialTests
{
	TSet<FName> ParameterNames(const UMaterial& Material, EMaterialParameterType Type)
	{
		TArray<FMaterialParameterInfo> Infos;
		TArray<FGuid> Ids;
		Material.GetAllParameterInfoOfType(Type, Infos, Ids);
		TSet<FName> Names;
		for (const FMaterialParameterInfo& Info : Infos)
		{
			Names.Add(Info.Name);
		}
		return Names;
	}

	void TestHas(FAutomationTestBase& Test, const TCHAR* What, const TSet<FName>& Names, std::initializer_list<const TCHAR*> Expected)
	{
		for (const TCHAR* Name : Expected)
		{
			Test.TestTrue(FString::Printf(TEXT("%s parameter %s"), What, Name), Names.Contains(FName(Name)));
		}
	}

	// Compiles the material for the running feature level, when the editor compiles shaders here,
	// and reports the errors.
	void TestCompiles(FAutomationTestBase& Test, UMaterial& Material)
	{
		FMaterialResource* Resource = Material.GetMaterialResource(GMaxRHIFeatureLevel);
		if (!Resource)
		{
			return;
		}
		Resource->FinishCompilation();
		for (const FString& Error : Resource->GetCompileErrors())
		{
			Test.AddError(FString::Printf(TEXT("%s: %s"), *Material.GetName(), *Error));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMMToonMaterialGraph, "VRM.Materials.MToonGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMMToonMaterialGraph::RunTest(const FString& Parameters)
{
	using namespace VRMMToonMaterialTests;
	namespace P = VRM::MToon::Param;

	UTexture2D* WhiteMask = VRM::MToon::CreateWhiteMaskTexture(GetTransientPackage(), NAME_None, RF_Transient);
	if (!TestNotNull(TEXT("White mask texture"), WhiteMask))
	{
		return false;
	}
	TestFalse(TEXT("White mask is linear"), WhiteMask->SRGB);
	TestTrue(TEXT("White mask uses mask compression"), WhiteMask->CompressionSettings == TC_Masks);

	// Surface
	UMaterial* Surface = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	FString Error;
	if (!TestTrue(TEXT("Surface builds"), VRM::MToon::BuildSurfaceMaterial(*Surface, WhiteMask, Error)))
	{
		AddError(Error);
		return false;
	}
	TestTrue(TEXT("Surface is unlit"), Surface->GetShadingModels().HasOnlyShadingModel(MSM_Unlit));
	TestTrue(TEXT("Surface is opaque (instances override)"), Surface->BlendMode == BLEND_Opaque);
	TestTrue(TEXT("Surface is used with skeletal meshes"), Surface->bUsedWithSkeletalMesh != 0);
	TestTrue(TEXT("Surface is used with morph targets"), Surface->bUsedWithMorphTargets != 0);
	TestEqual(TEXT("Surface graph version"), VRM::MToon::GetGraphVersion(Surface), VRM::MToon::GraphVersion);

	TestHas(*this, TEXT("Surface texture"), ParameterNames(*Surface, EMaterialParameterType::Texture), {
		P::BaseColorTexture, P::ShadeMultiplyTexture, P::NormalTexture, P::ShadingShiftTexture,
		P::EmissiveTexture, P::MatcapTexture, P::RimMultiplyTexture });
	TestHas(*this, TEXT("Surface vector"), ParameterNames(*Surface, EMaterialParameterType::Vector), {
		P::BaseColorFactor, P::ShadeColorFactor, P::EmissiveFactor, P::MatcapFactor, P::ParametricRimColorFactor,
		P::FallbackLightDirection, P::FallbackLightColor });
	TestHas(*this, TEXT("Surface scalar"), ParameterNames(*Surface, EMaterialParameterType::Scalar), {
		P::NormalScale, P::ShadingShiftTextureScale, P::ShadingShiftFactor, P::ShadingToonyFactor,
		P::ParametricRimFresnelPowerFactor, P::ParametricRimLiftFactor, P::RimLightingMixFactor,
		P::UVOffsetU, P::UVOffsetV, P::UVScaleU, P::UVScaleV, P::UVRotation, P::GraphVersion,
		P::AlphaCutoff, P::AlphaMode, P::DoubleSided });
	TestHas(*this, TEXT("Surface static switch"), ParameterNames(*Surface, EMaterialParameterType::StaticSwitch), {
		P::UseShadingShiftTexture, P::UnlitShading });
	TestEqual(TEXT("Surface clips masked pixels at 0.5 (the cutoff is in the graph)"), Surface->OpacityMaskClipValue, 0.5f);
	TestCompiles(*this, *Surface);

	// Outline
	UMaterial* Outline = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!TestTrue(TEXT("Outline builds"), VRM::MToon::BuildOutlineMaterial(*Outline, WhiteMask, Error)))
	{
		AddError(Error);
		return false;
	}
	TestTrue(TEXT("Outline is unlit"), Outline->GetShadingModels().HasOnlyShadingModel(MSM_Unlit));
	TestTrue(TEXT("Outline is masked"), Outline->BlendMode == BLEND_Masked);
	TestTrue(TEXT("Outline is two-sided (it keeps the back faces)"), Outline->TwoSided != 0);
	TestEqual(TEXT("Outline graph version"), VRM::MToon::GetGraphVersion(Outline), VRM::MToon::GraphVersion);
	TestHas(*this, TEXT("Outline texture"), ParameterNames(*Outline, EMaterialParameterType::Texture), {
		P::OutlineWidthMultiplyTexture });
	TestHas(*this, TEXT("Outline vector"), ParameterNames(*Outline, EMaterialParameterType::Vector), {
		P::OutlineColorFactor, P::FallbackLightDirection, P::FallbackLightColor });
	TestHas(*this, TEXT("Outline scalar"), ParameterNames(*Outline, EMaterialParameterType::Scalar), {
		P::OutlineWidthFactor, P::OutlineScreenCoordinates, P::OutlineLightingMixFactor,
		P::UVOffsetU, P::UVOffsetV, P::UVScaleU, P::UVScaleV, P::UVRotation, P::GraphVersion });
	TestCompiles(*this, *Outline);

	// Rebuilding replaces the graph instead of adding to it
	const int32 NodeCount = Surface->GetExpressions().Num();
	TestTrue(TEXT("Surface rebuilds"), VRM::MToon::BuildSurfaceMaterial(*Surface, WhiteMask, Error));
	TestEqual(TEXT("Rebuild keeps the node count"), Surface->GetExpressions().Num(), NodeCount);

	// The translator parents instances to these paths; they must name what the editor generates.
	TestEqual(TEXT("Surface path"), FString(VRM::MToon::SurfacePath),
		FString(VRM::MToon::GeneratedFolder) / VRM::MToon::SurfaceName + TEXT(".") + VRM::MToon::SurfaceName);
	TestEqual(TEXT("Outline path"), FString(VRM::MToon::OutlinePath),
		FString(VRM::MToon::GeneratedFolder) / VRM::MToon::OutlineName + TEXT(".") + VRM::MToon::OutlineName);

	TestEqual(TEXT("A plain material has no graph version"), VRM::MToon::GetGraphVersion(NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient)), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
