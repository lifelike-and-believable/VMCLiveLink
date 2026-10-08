// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// A whole import through Interchange and the plugin's pipelines, then a reimport: the skeletal mesh
// carries the humanoid map as package metadata, which is what the package saves (VRM.Humanoid.*, the
// convention VMCLiveLink reads, D-4).
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/SkeletalMesh.h"
#include "InterchangeManager.h"
#include "InterchangeSourceData.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "VRMAvatarParser.h"
#include "VRMDocument.h"
#include "VRMParsedModel.h"

namespace VRMHumanoidMetadataImportTests
{
	const TCHAR* const ContentPath = TEXT("/Game/VRMHumanoidMetadataTests");

	FString FixturePath(const TCHAR* Name)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VRMInterchange"));
		return Plugin.IsValid() ? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Tests"), TEXT("Fixtures"), FString(Name) + TEXT(".vrm")) : FString();
	}

	/** The plugin's stack (VRMImportPipelineRegistration), named here so the test doesn't read the project's settings. */
	FImportAssetParameters MakeParameters()
	{
		FImportAssetParameters Parameters;
		Parameters.bIsAutomated = true;
		for (const TCHAR* Path : {
			TEXT("/VRMInterchange/DefaultPipelines/DefaultVRMAssetsPipeline.DefaultVRMAssetsPipeline"),
			TEXT("/VRMInterchange/DefaultPipelines/DefaultSpringBonesPipeline.DefaultSpringBonesPipeline"),
			TEXT("/VRMInterchange/DefaultPipelines/DefaultVRMIKRigPipeline.DefaultVRMIKRigPipeline"),
			TEXT("/VRMInterchange/DefaultPipelines/DefaultVRMLiveLinkPipeline.DefaultVRMLiveLinkPipeline"),
			TEXT("/VRMInterchange/DefaultPipelines/DefaultVRMMaterialPipeline.DefaultVRMMaterialPipeline"),
			TEXT("/VRMInterchange/DefaultPipelines/DefaultVRMAvatarDescriptionPipeline.DefaultVRMAvatarDescriptionPipeline") })
		{
			Parameters.OverridePipelines.Add(FSoftObjectPath(Path));
		}
		return Parameters;
	}

	/** The mesh's VRM humanoid tags, as the package will save them. */
	TMap<FName, FString> HumanoidTags(USkeletalMesh* Mesh)
	{
		TMap<FName, FString> Out;
		if (const TMap<FName, FString>* Tags = FMetaData::GetMapForObject(Mesh))
		{
			for (const TPair<FName, FString>& Tag : *Tags)
			{
				const FString Key = Tag.Key.ToString();
				if (Key.StartsWith(VRM::HumanoidMetadataPrefix) || Key == VRM::HumanoidMetadataVersionKey)
				{
					Out.Add(Tag.Key, Tag.Value);
				}
			}
		}
		return Out;
	}

	USkeletalMesh* FindMesh(const TArray<UObject*>& Objects)
	{
		for (UObject* Object : Objects)
		{
			if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Object))
			{
				return Mesh;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMHumanoidMetadataImportTest, "VRM.Avatar.HumanoidMetadataImport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMHumanoidMetadataImportTest::RunTest(const FString& Parameters)
{
	using namespace VRMHumanoidMetadataImportTests;

	// What the mesh should carry: the fixture's humanoid map, in the metadata convention.
	const FString Path = FixturePath(TEXT("vrm1_minimal"));
	FString Error;
	FVRMParsedModel Model;
	FVRMAvatarData Avatar;
	const TSharedPtr<const FVRMDocument> Document = FVRMDocument::LoadFile(Path, Error);
	if (!TestTrue(TEXT("Fixture parses"), Document.IsValid() && VRM::BuildParsedModel(*Document, Model) && VRM::BuildAvatarData(*Document, Model, Avatar)))
	{
		return false;
	}
	const TMap<FName, FString> Wanted = VRM::MakeHumanoidMetadata(Avatar);
	if (!TestTrue(TEXT("The fixture has a humanoid map"), Wanted.Contains(FName(FString(VRM::HumanoidMetadataPrefix) + TEXT("Hips")))))
	{
		return false;
	}

	UInterchangeManager& Manager = UInterchangeManager::GetInterchangeManager();
	UInterchangeSourceData* Source = UInterchangeManager::CreateSourceData(Path);
	TArray<UObject*> Imported;
	Manager.ImportAsset(ContentPath, Source, MakeParameters(), Imported);
	USkeletalMesh* Mesh = FindMesh(Imported);
	if (!TestNotNull(TEXT("The import made a skeletal mesh"), Mesh))
	{
		ObjectTools::DeleteObjectsUnchecked(Imported);
		return false;
	}
	TestTrue(TEXT("Import: the mesh has the humanoid map"), HumanoidTags(Mesh).OrderIndependentCompareEqual(Wanted));

	// A reimport puts missing tags back.
	if (TMap<FName, FString>* Tags = FMetaData::GetMapForObject(Mesh))
	{
		Tags->Reset();
	}
	TArray<UObject*> Reimported;
	Manager.ReimportAsset(Mesh, MakeParameters(), Reimported);
	USkeletalMesh* ReimportedMesh = FindMesh(Reimported);
	TestTrue(TEXT("The reimport updated the same mesh"), ReimportedMesh == Mesh);
	TestTrue(TEXT("Reimport: the mesh has the humanoid map"), HumanoidTags(Mesh).OrderIndependentCompareEqual(Wanted));

	Imported.Append(Reimported);
	ObjectTools::DeleteObjectsUnchecked(Imported);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
