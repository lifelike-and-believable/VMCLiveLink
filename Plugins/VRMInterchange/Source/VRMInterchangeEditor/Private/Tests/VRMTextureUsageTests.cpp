// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// Tests for texture colour space and normal maps (P1.10).
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "VRMTranslator.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMTextureUsages, "VRM.Textures.Usage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMTextureUsages::RunTest(const FString& Parameters)
{
	using VRM::ETextureUsage;
	FVRMParsedModel Model;
	Model.Images.SetNum(5);

	FVRMParsedModel::FMat A;
	A.BaseColorTexture = 0;
	A.EmissiveTexture = 0;
	A.NormalTexture = 1;
	A.MetallicRoughnessTexture = 2;
	A.OcclusionTexture = 2;
	Model.Materials.Add(A);

	// Image 3 is one material's base colour and another's normal map.
	FVRMParsedModel::FMat B;
	B.BaseColorTexture = 3;
	Model.Materials.Add(B);
	FVRMParsedModel::FMat C;
	C.NormalTexture = 3;
	C.MetallicRoughnessTexture = 42; // out of range: ignored
	Model.Materials.Add(C);

	const TArray<ETextureUsage> Usages = VRM::ComputeTextureUsages(Model);
	if (!TestEqual(TEXT("One entry per image"), Usages.Num(), 5))
	{
		return false;
	}
	TestTrue(TEXT("Base colour and emissive: colour"), Usages[0] == ETextureUsage::Color);
	TestTrue(TEXT("Normal map: normal"), Usages[1] == ETextureUsage::Normal);
	TestTrue(TEXT("Metallic-roughness and occlusion: data"), Usages[2] == ETextureUsage::Data);
	TestTrue(TEXT("Colour in one material and normal in another: both"), Usages[3] == (ETextureUsage::Color | ETextureUsage::Normal));
	TestTrue(TEXT("Unused image: none"), Usages[4] == ETextureUsage::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMTextureDecode, "VRM.Textures.Decode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMTextureDecode::RunTest(const FString& Parameters)
{
	using VRM::ETextureUsage;

	// A 2x1 PNG: (R10 G20 B30), (R200 G100 B50).
	const uint8 RGBA[] = { 10, 20, 30, 255, 200, 100, 50, 255 };
	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Writer = Module.CreateImageWrapper(EImageFormat::PNG);
	if (!TestTrue(TEXT("PNG writer"), Writer.IsValid() && Writer->SetRaw(RGBA, sizeof(RGBA), 2, 1, ERGBFormat::RGBA, 8)))
	{
		return false;
	}
	const TArray64<uint8> Png = Writer->GetCompressed();

	auto Green = [](UE::Interchange::FImportImage& Image, int32 Pixel) { return Image.GetArrayViewOfRawData()[Pixel * 4 + 1]; }; // BGRA

	TOptional<UE::Interchange::FImportImage> Color = VRM::DecodeTextureImage(Png, ETextureUsage::Color);
	if (TestTrue(TEXT("Colour decodes"), Color.IsSet()))
	{
		TestTrue(TEXT("Colour is sRGB"), Color->bSRGB);
		TestTrue(TEXT("Colour uses default compression"), Color->CompressionSettings == TC_Default);
		TestEqual(TEXT("Colour keeps green"), int32(Green(*Color, 0)), 20);
		TestEqual(TEXT("Colour is BGRA (blue first)"), int32(Color->GetArrayViewOfRawData()[0]), 30);
	}

	TOptional<UE::Interchange::FImportImage> Normal = VRM::DecodeTextureImage(Png, ETextureUsage::Normal);
	if (TestTrue(TEXT("Normal decodes"), Normal.IsSet()))
	{
		TestFalse(TEXT("Normal map is linear"), Normal->bSRGB);
		TestTrue(TEXT("Normal map uses normal-map compression"), Normal->CompressionSettings == TC_Normalmap);
		TestEqual(TEXT("Normal map green is flipped (+Y to -Y), pixel 0"), int32(Green(*Normal, 0)), 255 - 20);
		TestEqual(TEXT("Normal map green is flipped (+Y to -Y), pixel 1"), int32(Green(*Normal, 1)), 255 - 100);
	}

	TOptional<UE::Interchange::FImportImage> Data = VRM::DecodeTextureImage(Png, ETextureUsage::Data);
	if (TestTrue(TEXT("Data decodes"), Data.IsSet()))
	{
		TestFalse(TEXT("Data is linear"), Data->bSRGB);
		TestTrue(TEXT("Data uses mask compression"), Data->CompressionSettings == TC_Masks);
		TestEqual(TEXT("Data keeps green"), int32(Green(*Data, 1)), 100);
	}

	TestFalse(TEXT("Empty bytes don't decode"), VRM::DecodeTextureImage(TArray64<uint8>(), ETextureUsage::Color).IsSet());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
