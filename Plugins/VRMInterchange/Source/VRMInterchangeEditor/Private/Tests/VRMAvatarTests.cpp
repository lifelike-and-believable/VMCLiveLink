// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// Tests for the avatar description parser (P4.1): humanoid map, expressions and meta from the fixtures.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Interfaces/IPluginManager.h"
#include "Engine/SkeletalMesh.h"
#include "InterchangeSourceData.h"
#include "InterchangeVRMNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "VRMAvatarDescription.h"
#include "VRMAvatarDescriptionPipeline.h"
#include "UObject/Package.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "VRMAvatarParser.h"
#include "VRMDocument.h"
#include "VRMParsedModel.h"

namespace VRMAvatarTests
{
	FString FixturePath(const TCHAR* Name, const TCHAR* Extension)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VRMInterchange"));
		return Plugin.IsValid()
			? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Tests"), TEXT("Fixtures"), FString(Name) + Extension)
			: FString();
	}

	/** Loads a fixture's document, model and avatar data, and its expected values. */
	bool Load(FAutomationTestBase& Test, const TCHAR* Name, FVRMParsedModel& OutModel, FVRMAvatarData& OutAvatar, TSharedPtr<FJsonObject>& OutExpected)
	{
		FString Error;
		const TSharedPtr<const FVRMDocument> Document = FVRMDocument::LoadFile(FixturePath(Name, TEXT(".vrm")), Error);
		if (!Test.TestTrue(FString::Printf(TEXT("%s loads (%s)"), Name, *Error), Document.IsValid())
			|| !Test.TestTrue(FString::Printf(TEXT("%s: model"), Name), VRM::BuildParsedModel(*Document, OutModel)))
		{
			return false;
		}
		TArray<FString> Warnings;
		const bool bIsVRM = VRM::BuildAvatarData(*Document, OutModel, OutAvatar, &Warnings);
		Test.TestTrue(FString::Printf(TEXT("%s is a VRM"), Name), bIsVRM);
		Test.TestEqual(FString::Printf(TEXT("%s: no warnings (%s)"), Name, *FString::Join(Warnings, TEXT("; "))), Warnings.Num(), 0);

		FString Text;
		return Test.TestTrue(FString::Printf(TEXT("%s.expected.json"), Name),
			FFileHelper::LoadFileToString(Text, *FixturePath(Name, TEXT(".expected.json")))
			&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), OutExpected) && OutExpected.IsValid());
	}

	/** Humanoid bones point at the bones the importer made for the expected nodes; expressions bind the expected morph targets. */
	void CheckAgainstExpected(FAutomationTestBase& Test, const TCHAR* Name, const FVRMParsedModel& Model, const FVRMAvatarData& Avatar, const FJsonObject& Expected)
	{
		const TSharedPtr<FJsonObject>* Humanoid = nullptr;
		if (Expected.TryGetObjectField(TEXT("humanoid"), Humanoid))
		{
			Test.TestEqual(FString::Printf(TEXT("%s: humanoid bone count"), Name), Avatar.HumanoidToBone.Num(), (*Humanoid)->Values.Num());
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Humanoid)->Values)
			{
				const EVRMHumanBone Bone = VRM::HumanBoneFromName(Pair.Key, EVRMAvatarVersion::VRM1);
				const FName* Actual = Avatar.HumanoidToBone.Find(Bone);
				const FName* ExpectedBone = Model.NodeToBoneMap.Find(int32(Pair.Value->AsNumber()));
				Test.TestTrue(FString::Printf(TEXT("%s: humanoid %s is the bone of node %d"), Name, *Pair.Key, int32(Pair.Value->AsNumber())),
					Actual && ExpectedBone && *Actual == *ExpectedBone);
			}
		}

		const TSharedPtr<FJsonObject>* Expressions = nullptr;
		if (Expected.TryGetObjectField(TEXT("expressions"), Expressions))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Expressions)->Values)
			{
				const FVRMExpression* Expression = Avatar.Expressions.FindByPredicate([&Pair](const FVRMExpression& E) { return E.Name == FName(*Pair.Key); });
				if (!Test.TestNotNull(FString::Printf(TEXT("%s: expression %s"), Name, *Pair.Key), Expression))
				{
					continue;
				}
				const TArray<TSharedPtr<FJsonValue>>& Binds = Pair.Value->AsArray();
				Test.TestEqual(FString::Printf(TEXT("%s: %s bind count"), Name, *Pair.Key), Expression->MorphBinds.Num(), Binds.Num());
				for (int32 i = 0; i < Binds.Num() && i < Expression->MorphBinds.Num(); ++i)
				{
					const TSharedPtr<FJsonObject> Bind = Binds[i]->AsObject();
					Test.TestEqual(FString::Printf(TEXT("%s: %s bind %d morph"), Name, *Pair.Key, i), Expression->MorphBinds[i].MorphTarget, FName(*Bind->GetStringField(TEXT("morph"))));
					Test.TestEqual(FString::Printf(TEXT("%s: %s bind %d weight"), Name, *Pair.Key, i), Expression->MorphBinds[i].Weight, float(Bind->GetNumberField(TEXT("weight"))), 1e-4f);
				}
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMAvatarVRM1Test, "VRM.Avatar.VRM1",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMAvatarVRM1Test::RunTest(const FString& Parameters)
{
	using namespace VRMAvatarTests;
	FVRMParsedModel Model;
	FVRMAvatarData Avatar;
	TSharedPtr<FJsonObject> Expected;
	if (!Load(*this, TEXT("vrm1_minimal"), Model, Avatar, Expected))
	{
		return false;
	}
	TestTrue(TEXT("VRM 1.0"), Avatar.Version == EVRMAvatarVersion::VRM1);
	CheckAgainstExpected(*this, TEXT("vrm1_minimal"), Model, Avatar, *Expected);

	const FVRMExpression* Happy = Avatar.Expressions.FindByPredicate([](const FVRMExpression& E) { return E.Name == TEXT("happy"); });
	TestTrue(TEXT("happy is the Happy preset"), Happy && Happy->Preset == EVRMExpressionPreset::Happy);

	TestEqual(TEXT("Meta name"), Avatar.Meta.Name, FString(TEXT("fixture")));
	TestTrue(TEXT("Meta authors"), Avatar.Meta.Authors == TArray<FString>{ TEXT("VMCLiveLink tests") });
	TestEqual(TEXT("Licence URL"), Avatar.Meta.LicenseUrl, FString(TEXT("https://vrm.dev/licenses/1.0/")));
	TestEqual(TEXT("Avatar permission"), Avatar.Meta.AvatarPermission, FString(TEXT("everyone")));
	TestEqual(TEXT("Commercial usage"), Avatar.Meta.CommercialUsage, FString(TEXT("personalNonProfit")));
	TestTrue(TEXT("Licence summary names the licence"), VRM::DescribeLicense(Avatar.Meta).Contains(TEXT("https://vrm.dev/licenses/1.0/")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMAvatarVRM0Test, "VRM.Avatar.VRM0",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMAvatarVRM0Test::RunTest(const FString& Parameters)
{
	using namespace VRMAvatarTests;
	FVRMParsedModel Model;
	FVRMAvatarData Avatar;
	TSharedPtr<FJsonObject> Expected;
	if (!Load(*this, TEXT("vrm0_minimal"), Model, Avatar, Expected))
	{
		return false;
	}
	TestTrue(TEXT("VRM 0.x"), Avatar.Version == EVRMAvatarVersion::VRM0);
	CheckAgainstExpected(*this, TEXT("vrm0_minimal"), Model, Avatar, *Expected);

	// 0.x preset "joy" is 1.0's happy; the bind's 0-100 weight is scaled to 0-1 (checked above).
	const FVRMExpression* Joy = Avatar.Expressions.FindByPredicate([](const FVRMExpression& E) { return E.Name == TEXT("Joy"); });
	TestTrue(TEXT("Joy is the Happy preset"), Joy && Joy->Preset == EVRMExpressionPreset::Happy);

	TestEqual(TEXT("Meta title"), Avatar.Meta.Name, FString(TEXT("fixture")));
	TestEqual(TEXT("Licence name"), Avatar.Meta.LicenseName, FString(TEXT("CC0")));
	TestEqual(TEXT("Everyone maps to everyone"), Avatar.Meta.AvatarPermission, FString(TEXT("everyone")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMAvatarNamesTest, "VRM.Avatar.Names",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMAvatarNamesTest::RunTest(const FString& Parameters)
{
	using namespace VRM;
	// Every bone and preset round-trips through its VRM 1.0 name.
	for (int32 i = 1; i < int32(EVRMHumanBone::Count); ++i)
	{
		const EVRMHumanBone Bone = EVRMHumanBone(i);
		TestTrue(FString::Printf(TEXT("Bone %s round-trips"), *HumanBoneName(Bone)), HumanBoneFromName(HumanBoneName(Bone), EVRMAvatarVersion::VRM1) == Bone);
	}
	for (int32 i = 1; i <= int32(EVRMExpressionPreset::Neutral); ++i)
	{
		const EVRMExpressionPreset Preset = EVRMExpressionPreset(i);
		TestTrue(FString::Printf(TEXT("Preset %s round-trips"), *ExpressionPresetName(Preset)), ExpressionPresetFromName(ExpressionPresetName(Preset), EVRMAvatarVersion::VRM1) == Preset);
		const FString VRM0Name = ExpressionPresetName(Preset, EVRMAvatarVersion::VRM0);
		TestTrue(FString::Printf(TEXT("Preset %s round-trips as 0.x %s"), *ExpressionPresetName(Preset), *VRM0Name), ExpressionPresetFromName(VRM0Name, EVRMAvatarVersion::VRM0) == Preset);
	}
	TestEqual(TEXT("0.x name of happy"), ExpressionPresetName(EVRMExpressionPreset::Happy, EVRMAvatarVersion::VRM0), FString(TEXT("joy")));
	TestEqual(TEXT("0.x name of blinkLeft"), ExpressionPresetName(EVRMExpressionPreset::BlinkLeft, EVRMAvatarVersion::VRM0), FString(TEXT("blink_l")));
	TestEqual(TEXT("0.x name of lookUp"), ExpressionPresetName(EVRMExpressionPreset::LookUp, EVRMAvatarVersion::VRM0), FString(TEXT("lookup")));

	// The licence summary leaves out what the meta doesn't give, with no empty quotes or dangling separators.
	{
		FVRMMeta Meta;
		TestEqual(TEXT("Empty meta: empty summary"), DescribeLicense(Meta), FString());
		Meta.Name = TEXT("Alice");
		TestEqual(TEXT("Name only"), DescribeLicense(Meta), FString(TEXT("'Alice'")));
		Meta.Name.Reset();
		Meta.Authors = { TEXT("Bob") };
		Meta.LicenseName = TEXT("CC0");
		TestEqual(TEXT("Authors and licence, no name"), DescribeLicense(Meta), FString(TEXT("By Bob. Licence: CC0")));
		Meta.Authors.Reset();
		TestEqual(TEXT("Licence only"), DescribeLicense(Meta), FString(TEXT("Licence: CC0")));
		Meta.Name = TEXT("Alice");
		Meta.Authors = { TEXT("Bob") };
		TestEqual(TEXT("Everything"), DescribeLicense(Meta), FString(TEXT("'Alice' by Bob. Licence: CC0")));
	}

	// VRM 0.x thumbs shift by one bone.
	TestTrue(TEXT("0.x thumb proximal is 1.0 metacarpal"), HumanBoneFromName(TEXT("leftThumbProximal"), EVRMAvatarVersion::VRM0) == EVRMHumanBone::LeftThumbMetacarpal);
	TestTrue(TEXT("0.x thumb intermediate is 1.0 proximal"), HumanBoneFromName(TEXT("rightThumbIntermediate"), EVRMAvatarVersion::VRM0) == EVRMHumanBone::RightThumbProximal);
	TestTrue(TEXT("0.x thumb distal stays distal"), HumanBoneFromName(TEXT("leftThumbDistal"), EVRMAvatarVersion::VRM0) == EVRMHumanBone::LeftThumbDistal);
	TestTrue(TEXT("1.0 thumb proximal is proximal"), HumanBoneFromName(TEXT("leftThumbProximal"), EVRMAvatarVersion::VRM1) == EVRMHumanBone::LeftThumbProximal);
	TestTrue(TEXT("Unknown bone"), HumanBoneFromName(TEXT("tail"), EVRMAvatarVersion::VRM1) == EVRMHumanBone::None);

	// VRM 0.x presets.
	TestTrue(TEXT("joy"), ExpressionPresetFromName(TEXT("joy"), EVRMAvatarVersion::VRM0) == EVRMExpressionPreset::Happy);
	TestTrue(TEXT("sorrow"), ExpressionPresetFromName(TEXT("sorrow"), EVRMAvatarVersion::VRM0) == EVRMExpressionPreset::Sad);
	TestTrue(TEXT("fun"), ExpressionPresetFromName(TEXT("fun"), EVRMAvatarVersion::VRM0) == EVRMExpressionPreset::Relaxed);
	TestTrue(TEXT("a"), ExpressionPresetFromName(TEXT("a"), EVRMAvatarVersion::VRM0) == EVRMExpressionPreset::Aa);
	TestTrue(TEXT("blink_l"), ExpressionPresetFromName(TEXT("blink_l"), EVRMAvatarVersion::VRM0) == EVRMExpressionPreset::BlinkLeft);
	TestTrue(TEXT("lookup"), ExpressionPresetFromName(TEXT("lookup"), EVRMAvatarVersion::VRM0) == EVRMExpressionPreset::LookUp);
	TestTrue(TEXT("unknown is custom"), ExpressionPresetFromName(TEXT("unknown"), EVRMAvatarVersion::VRM0) == EVRMExpressionPreset::Custom);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMAvatarNodeTest, "VRM.Avatar.NodeRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMAvatarNodeTest::RunTest(const FString& Parameters)
{
	// The translator passes the avatar to the pipelines on the VRM node, as JSON.
	using namespace VRMAvatarTests;
	FVRMParsedModel Model;
	FVRMAvatarData Avatar;
	TSharedPtr<FJsonObject> Expected;
	if (!Load(*this, TEXT("vrm1_minimal"), Model, Avatar, Expected))
	{
		return false;
	}
	UInterchangeVRMNode* Node = NewObject<UInterchangeVRMNode>();
	Node->SetAvatarData(Avatar);
	FVRMAvatarData Back;
	if (!TestTrue(TEXT("Read back"), Node->GetAvatarData(Back)))
	{
		return false;
	}
	TestTrue(TEXT("Version"), Back.Version == Avatar.Version);
	TestTrue(TEXT("Humanoid map (enum keys)"), Back.HumanoidToBone.OrderIndependentCompareEqual(Avatar.HumanoidToBone));
	TestEqual(TEXT("Expressions"), Back.Expressions.Num(), Avatar.Expressions.Num());
	if (Back.Expressions.Num() > 0 && Avatar.Expressions.Num() > 0)
	{
		TestTrue(TEXT("Preset"), Back.Expressions[0].Preset == Avatar.Expressions[0].Preset);
		TestEqual(TEXT("Morph target"), Back.Expressions[0].MorphBinds.Num() > 0 ? Back.Expressions[0].MorphBinds[0].MorphTarget : FName(), FName(TEXT("Fcl_ALL_Joy")));
	}
	TestEqual(TEXT("Meta"), Back.Meta.LicenseUrl, Avatar.Meta.LicenseUrl);
	TestFalse(TEXT("A node without it has none"), NewObject<UInterchangeVRMNode>()->GetAvatarData(Back));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMAvatarPipelineTest, "VRM.Avatar.Pipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMAvatarPipelineTest::RunTest(const FString& Parameters)
{
	// The pipeline makes <Mesh>_Avatar next to the character once the mesh arrives, and updates it on reimport.
	using namespace VRMAvatarTests;
	const FString ContentBase = TEXT("/Game/VRMAvatarTests");
	UInterchangeSourceData* Source = NewObject<UInterchangeSourceData>();
	Source->SetFilename(FixturePath(TEXT("vrm1_minimal"), TEXT(".vrm")));
	UPackage* MeshPackage = CreatePackage(*(ContentBase / TEXT("vrm1_minimal") / TEXT("SK_AvatarTest")));
	USkeletalMesh* Mesh = NewObject<USkeletalMesh>(MeshPackage, TEXT("SK_AvatarTest"), RF_Transient);

	auto Run = [&](bool bOverwrite, bool bReimport)
	{
		UVRMAvatarDescriptionPipeline* Pipeline = NewObject<UVRMAvatarDescriptionPipeline>();
		Pipeline->bGenerateAvatarDescription = true;
		Pipeline->bOverwriteExisting = bOverwrite;
		Pipeline->ExecutePipeline(NewObject<UInterchangeBaseNodeContainer>(), { Source }, ContentBase);
		TestTrue(TEXT("Waiting for the mesh"), Pipeline->HasPendingPostImportWork());
		Pipeline->HandleImportedAsset(Mesh, bReimport);
		return Pipeline->GetLastDescription();
	};

	UVRMAvatarDescription* Description = Run(true, false);
	if (!TestNotNull(TEXT("Description made"), Description))
	{
		return false;
	}
	TestEqual(TEXT("Named after the mesh, next to the character"), Description->GetPathName(),
		ContentBase / TEXT("vrm1_minimal") / TEXT("SK_AvatarTest_Avatar.SK_AvatarTest_Avatar"));
	TestTrue(TEXT("Points at the mesh"), Description->Mesh.Get() == Mesh);
	TestEqual(TEXT("Humanoid bones"), Description->Avatar.HumanoidToBone.Num(), 3);
	TestTrue(TEXT("Hips"), !Description->GetBone(EVRMHumanBone::Hips).IsNone());
	TestNotNull(TEXT("Happy expression"), Description->FindExpression(EVRMExpressionPreset::Happy));
	TestFalse(TEXT("Source hash"), Description->SourceHash.IsEmpty());

	// Reimport with overwrite updates the same asset.
	Description->Avatar.Expressions.Reset();
	TestTrue(TEXT("Overwrite keeps the asset"), Run(true, true) == Description);
	TestEqual(TEXT("... with fresh data"), Description->Avatar.Expressions.Num(), 1);

	// Turned off, nothing is staged.
	UVRMAvatarDescriptionPipeline* Off = NewObject<UVRMAvatarDescriptionPipeline>();
	Off->bGenerateAvatarDescription = false;
	Off->ExecutePipeline(NewObject<UInterchangeBaseNodeContainer>(), { Source }, ContentBase);
	TestFalse(TEXT("Off: nothing to do"), Off->HasPendingPostImportWork());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
