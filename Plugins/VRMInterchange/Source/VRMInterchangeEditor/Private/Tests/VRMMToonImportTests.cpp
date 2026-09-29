// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// Tests for importing MToon materials (P4.5): the translator's material instances, and the
// material pipeline's generated masters, overrides, parents and outline overlay.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/SkeletalMesh.h"
#include "InterchangeManager.h"
#include "InterchangeMaterialInstanceNode.h"
#include "InterchangeSourceData.h"
#include "InterchangeTranslatorBase.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "VRMImportMessages.h"
#include "VRMMaterialPostImportPipeline.h"
#include "VRMMToonMaterial.h"

namespace VRMMToonImportTests
{
	FString FixturePath(const TCHAR* Name)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VRMInterchange"));
		return Plugin.IsValid() ? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Tests"), TEXT("Fixtures"), FString(Name) + TEXT(".vrm")) : FString();
	}

	const UInterchangeMaterialInstanceNode* FindInstance(const UInterchangeBaseNodeContainer& Container, const FString& DisplayLabel)
	{
		const UInterchangeMaterialInstanceNode* Found = nullptr;
		Container.IterateNodesOfType<UInterchangeMaterialInstanceNode>([&](const FString&, UInterchangeMaterialInstanceNode* Node)
		{
			if (Node && Node->GetDisplayLabel() == DisplayLabel)
			{
				Found = Node;
			}
		});
		return Found;
	}

	FString Parent(const UInterchangeMaterialInstanceNode* Node)
	{
		FString Path;
		if (Node)
		{
			Node->GetCustomParent(Path);
		}
		return Path;
	}

	float Scalar(const UInterchangeMaterialInstanceNode* Node, const TCHAR* Name, float Default = -1000.f)
	{
		float Value = Default;
		if (Node)
		{
			Node->GetScalarParameterValue(Name, Value);
		}
		return Value;
	}

	bool Switch(const UInterchangeMaterialInstanceNode* Node, const TCHAR* Name)
	{
		bool bValue = false;
		return Node && Node->GetStaticSwitchParameterValue(Name, bValue) && bValue;
	}

	FString Texture(const UInterchangeMaterialInstanceNode* Node, const TCHAR* Name)
	{
		FString Uid;
		if (Node)
		{
			Node->GetTextureParameterValue(Name, Uid);
		}
		return Uid;
	}

	UMaterialInstanceConstant* NewInstance(const FString& Folder, const FString& Name, UMaterialInterface* ParentMaterial)
	{
		UMaterialInstanceConstant* Instance = NewObject<UMaterialInstanceConstant>(CreatePackage(*(Folder / Name)), *Name, RF_Transient);
		Instance->SetParentEditorOnly(ParentMaterial);
		return Instance;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMMToonTranslateTest, "VRM.Materials.Translate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMMToonTranslateTest::RunTest(const FString& Parameters)
{
	using namespace VRMMToonImportTests;
	namespace P = VRM::MToon::Param;

	// The Interchange manager picks the translator registered for .vrm and gives it the source.
	UInterchangeSourceData* Source = UInterchangeManager::CreateSourceData(FixturePath(TEXT("mtoon_materials")));
	UInterchangeTranslatorBase* Translator = UInterchangeManager::GetInterchangeManager().GetTranslatorForSourceData(Source);
	if (!TestNotNull(TEXT("A translator for .vrm"), Translator)
		|| !TestEqual(TEXT("It is the VRM translator"), Translator->GetClass()->GetName(), FString(TEXT("VRMTranslator"))))
	{
		return false;
	}
	UInterchangeBaseNodeContainer* Container = NewObject<UInterchangeBaseNodeContainer>();
	const bool bTranslated = Translator->Translate(*Container);
	VRM::ImportMessages::Take(Source->GetFilename()); // the import's message bucket, which no report closes here
	if (!TestTrue(TEXT("Translates"), bTranslated))
	{
		return false;
	}

	// Character instances: one per master used, and the outline
	const UInterchangeMaterialInstanceNode* Character = FindInstance(*Container, TEXT("MI_VRM_mtoon_materials"));
	const UInterchangeMaterialInstanceNode* CharacterMToon = FindInstance(*Container, TEXT("MI_VRM_mtoon_materials__MToon"));
	const UInterchangeMaterialInstanceNode* Outline = FindInstance(*Container, TEXT("MI_VRM_mtoon_materials__Outline"));
	TestEqual(TEXT("Character instance on M_VRM_Master (Accessory is PBR)"), Parent(Character), FString(TEXT("/VRMInterchange/Materials/M_VRM_Master")));
	TestEqual(TEXT("Character instance on M_VRM_MToon"), Parent(CharacterMToon), FString(VRM::MToon::SurfacePath));
	TestEqual(TEXT("Outline instance on M_VRM_MToonOutline"), Parent(Outline), FString(VRM::MToon::OutlinePath));
	TestEqual(TEXT("Outline takes the widest outline (Body's, world)"), Scalar(Outline, P::OutlineWidthFactor), 0.004f, 1e-6f);
	TestEqual(TEXT("Outline in world units"), Scalar(Outline, P::OutlineScreenCoordinates), 0.f, 1e-6f);
	TestEqual(TEXT("Outline lighting mix"), Scalar(Outline, P::OutlineLightingMixFactor), 0.5f, 1e-6f);
	TestTrue(TEXT("Outline width texture is image 1 as data"), Texture(Outline, P::OutlineWidthMultiplyTexture).EndsWith(TEXT("Tex_1_Data")));

	// Body: MToon with a shading shift texture and a texture transform
	const UInterchangeMaterialInstanceNode* Body = FindInstance(*Container, TEXT("MI_VRM_mtoon_materials_Body"));
	TestEqual(TEXT("Body on M_VRM_MToon"), Parent(Body), FString(VRM::MToon::SurfacePath));
	TestEqual(TEXT("Body toony"), Scalar(Body, P::ShadingToonyFactor), 0.8f, 1e-6f);
	TestEqual(TEXT("Body shift"), Scalar(Body, P::ShadingShiftFactor), -0.1f, 1e-6f);
	TestTrue(TEXT("Body uses its shading shift texture"), Switch(Body, P::UseShadingShiftTexture));
	TestTrue(TEXT("Shading shift texture is image 1 as data"), Texture(Body, P::ShadingShiftTexture).EndsWith(TEXT("Tex_1_Data")));
	TestEqual(TEXT("Shading shift texture scale"), Scalar(Body, P::ShadingShiftTextureScale), 0.5f, 1e-6f);
	TestTrue(TEXT("Shade texture is image 0 as colour"), Texture(Body, P::ShadeMultiplyTexture).EndsWith(TEXT("Tex_0")));
	TestEqual(TEXT("UV scale U"), Scalar(Body, P::UVScaleU), 2.f, 1e-6f);
	TestEqual(TEXT("UV offset U"), Scalar(Body, P::UVOffsetU), 0.5f, 1e-6f);
	TestEqual(TEXT("Body opaque"), Scalar(Body, P::AlphaMode), 0.f, 1e-6f);
	TestEqual(TEXT("Rim fresnel power"), Scalar(Body, P::ParametricRimFresnelPowerFactor), 3.f, 1e-6f);
	FLinearColor Matcap;
	TestTrue(TEXT("Matcap factor set"), Body && Body->GetVectorParameterValue(P::MatcapFactor, Matcap));
	TestTrue(TEXT("No matcap texture: black matcap"), Matcap.Equals(FLinearColor::Black));
	TestFalse(TEXT("MToon is not unlit"), Switch(Body, P::UnlitShading));

	// Face: MToon, blended
	const UInterchangeMaterialInstanceNode* Face = FindInstance(*Container, TEXT("MI_VRM_mtoon_materials_Face"));
	TestEqual(TEXT("Face blended"), Scalar(Face, P::AlphaMode), 2.f, 1e-6f);
	TestFalse(TEXT("Face has no shading shift texture"), Switch(Face, P::UseShadingShiftTexture));

	// Hair: KHR_materials_unlit on the MToon master
	const UInterchangeMaterialInstanceNode* Hair = FindInstance(*Container, TEXT("MI_VRM_mtoon_materials_Hair"));
	TestEqual(TEXT("Hair on M_VRM_MToon"), Parent(Hair), FString(VRM::MToon::SurfacePath));
	TestTrue(TEXT("Hair unlit"), Switch(Hair, P::UnlitShading));
	TestEqual(TEXT("Hair masked"), Scalar(Hair, P::AlphaMode), 1.f, 1e-6f);
	TestEqual(TEXT("Hair cutoff"), Scalar(Hair, P::AlphaCutoff), 0.4f, 1e-6f);
	TestEqual(TEXT("Hair double-sided"), Scalar(Hair, P::DoubleSided), 1.f, 1e-6f);

	// Accessory: glTF PBR on M_VRM_Master, as before
	const UInterchangeMaterialInstanceNode* Accessory = FindInstance(*Container, TEXT("MI_VRM_mtoon_materials_Accessory"));
	TestEqual(TEXT("Accessory on M_VRM_Master"), Parent(Accessory), FString(TEXT("/VRMInterchange/Materials/M_VRM_Master")));
	TestTrue(TEXT("Accessory base colour"), Texture(Accessory, TEXT("BaseColorTexture")).EndsWith(TEXT("Tex_0")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMMToonPipelineTest, "VRM.Materials.Pipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMMToonPipelineTest::RunTest(const FString& Parameters)
{
	using namespace VRMMToonImportTests;
	namespace P = VRM::MToon::Param;

	// An import whose instances use the MToon master: the pipeline generates the materials first.
	UInterchangeBaseNodeContainer* Container = NewObject<UInterchangeBaseNodeContainer>();
	UInterchangeMaterialInstanceNode* Node = NewObject<UInterchangeMaterialInstanceNode>(Container);
	Container->SetupNode(Node, TEXT("TestInstance"), TEXT("TestInstance"), EInterchangeNodeContainerType::TranslatedAsset);
	Node->SetCustomParent(VRM::MToon::SurfacePath);
	UInterchangeSourceData* Source = NewObject<UInterchangeSourceData>();
	Source->SetFilename(FixturePath(TEXT("mtoon_materials")));

	UVRMMaterialPostImportPipeline* Pipeline = NewObject<UVRMMaterialPostImportPipeline>();
	Pipeline->ExecutePipeline(Container, { Source }, TEXT("/Game/VRMMaterialTests"));
	UMaterial* Surface = FindObject<UMaterial>(nullptr, VRM::MToon::SurfacePath);
	UMaterial* OutlineMaterial = FindObject<UMaterial>(nullptr, VRM::MToon::OutlinePath);
	if (!TestNotNull(TEXT("M_VRM_MToon generated"), Surface) || !TestNotNull(TEXT("M_VRM_MToonOutline generated"), OutlineMaterial))
	{
		return false;
	}
	TestEqual(TEXT("Current graph"), VRM::MToon::GetGraphVersion(Surface), VRM::MToon::GraphVersion);
	FString Error;
	TestTrue(TEXT("Found again, not rebuilt"), VRM::MToon::FindOrCreateMToonMaterials(Error).Surface == Surface);

	// The created assets, in the order Interchange might report them
	const FString Folder = TEXT("/Game/VRMMaterialTests/mtoon_materials/Materials");
	UMaterialInstanceConstant* Face = NewInstance(Folder, TEXT("MI_VRM_mtoon_materials_Face"), Surface);
	Face->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(P::AlphaMode), 2.f);
	Face->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(P::DoubleSided), 1.f);
	UMaterialInstanceConstant* Hair = NewInstance(Folder, TEXT("MI_VRM_mtoon_materials_Hair"), Surface);
	Hair->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(P::AlphaMode), 1.f);
	UMaterialInstanceConstant* CharacterMToon = NewInstance(Folder, TEXT("MI_VRM_mtoon_materials__MToon"), Surface);
	UMaterialInstanceConstant* Outline = NewInstance(Folder, TEXT("MI_VRM_mtoon_materials__Outline"), OutlineMaterial);
	USkeletalMesh* Mesh = NewObject<USkeletalMesh>(CreatePackage(*(FString(TEXT("/Game/VRMMaterialTests/mtoon_materials")) / TEXT("SK_MToonTest"))), TEXT("SK_MToonTest"), RF_Transient);

	Pipeline->HandleImportedAsset(Face);
	Pipeline->HandleImportedAsset(Hair);
	TestTrue(TEXT("Blend becomes translucent"), Face->BasePropertyOverrides.bOverride_BlendMode && Face->BasePropertyOverrides.BlendMode == BLEND_Translucent);
	TestTrue(TEXT("Double-sided becomes two-sided"), Face->BasePropertyOverrides.bOverride_TwoSided && Face->BasePropertyOverrides.TwoSided);
	TestTrue(TEXT("Mask becomes masked"), Hair->BasePropertyOverrides.BlendMode == BLEND_Masked);
	TestFalse(TEXT("Single-sided stays one-sided"), bool(Hair->BasePropertyOverrides.TwoSided));
	TestTrue(TEXT("Not reparented before the character instance arrives"), Face->Parent == Surface);

	Pipeline->HandleImportedAsset(CharacterMToon);
	TestTrue(TEXT("Face parented to the MToon character instance"), Face->Parent == CharacterMToon);
	TestTrue(TEXT("Hair too"), Hair->Parent == CharacterMToon);
	TestTrue(TEXT("Overrides survive the reparent"), Face->BasePropertyOverrides.BlendMode == BLEND_Translucent);

	FObjectPropertyBase* OverlayProperty = FindFProperty<FObjectPropertyBase>(USkeletalMesh::StaticClass(), TEXT("OverlayMaterial"));
	if (!TestNotNull(TEXT("Skeletal meshes have an overlay material"), OverlayProperty))
	{
		return false;
	}
	Pipeline->HandleImportedAsset(Outline);
	TestTrue(TEXT("No overlay before the mesh arrives"), OverlayProperty->GetObjectPropertyValue_InContainer(Mesh) == nullptr);
	Pipeline->HandleImportedAsset(Mesh);
	TestTrue(TEXT("The outline is the mesh's overlay material"), OverlayProperty->GetObjectPropertyValue_InContainer(Mesh) == Outline);
	TestTrue(TEXT("The outline is not reparented"), Outline->Parent == OutlineMaterial);

	// Turned off, no overlay
	USkeletalMesh* Mesh2 = NewObject<USkeletalMesh>(CreatePackage(*(FString(TEXT("/Game/VRMMaterialTests/mtoon_materials")) / TEXT("SK_MToonTest2"))), TEXT("SK_MToonTest2"), RF_Transient);
	UVRMMaterialPostImportPipeline* Off = NewObject<UVRMMaterialPostImportPipeline>();
	Off->bApplyMToonOutline = false;
	Off->ExecutePipeline(Container, { Source }, TEXT("/Game/VRMMaterialTests"));
	Off->HandleImportedAsset(Outline);
	Off->HandleImportedAsset(Mesh2);
	TestTrue(TEXT("Off: no overlay"), OverlayProperty->GetObjectPropertyValue_InContainer(Mesh2) == nullptr);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
