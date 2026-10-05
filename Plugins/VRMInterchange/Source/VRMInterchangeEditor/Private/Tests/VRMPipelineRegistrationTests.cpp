// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "InterchangeManager.h"
#include "InterchangePipelineBase.h"
#include "VRMImportPipelineRegistration.h"
#include "VRMMaterialPostImportPipeline.h"
#include "VRMAvatarDescriptionPipeline.h"

namespace VRMPipelineRegistrationTestsPrivate
{
	/** True when one of Paths instantiates, the way an import does, as a pipeline of class T. */
	template <typename T>
	bool HasInstantiablePipeline(const TArray<FSoftObjectPath>& Paths)
	{
		for (const FSoftObjectPath& Path : Paths)
		{
			if (Cast<T>(UE::Interchange::GeneratePipelineInstance(Path)))
			{
				return true;
			}
		}
		return false;
	}
}

// Every path the registration saves must instantiate through GeneratePipelineInstance, which an
// import uses. Class paths load a UClass and are rejected, which left the MToon materials uncreated.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMPipelineRegistrationInstantiableTest, "VRM.Pipeline.Registration.Instantiable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMPipelineRegistrationInstantiableTest::RunTest(const FString& Parameters)
{
	using namespace VRMPipelineRegistrationTestsPrivate;
	// Seeded with an empty list, so only the plugin's own pipelines are checked, whatever else the
	// project's Interchange settings register.
	const TArray<FSoftObjectPath> Empty;
	const TArray<FSoftObjectPath> Paths = VRMImportPipelineRegistration::PreviewVRMTranslatorPipelines(&Empty);
	if (Paths.Num() == 0)
	{
		AddError(TEXT("No VRM translator pipeline list: the project's Interchange settings have no \"Assets\" pipeline stack."));
		return false;
	}

	for (const FSoftObjectPath& Path : Paths)
	{
		TestNotNull(*FString::Printf(TEXT("%s instantiates"), *Path.ToString()), UE::Interchange::GeneratePipelineInstance(Path));
	}
	TestTrue(TEXT("The material pipeline is registered"), HasInstantiablePipeline<UVRMMaterialPostImportPipeline>(Paths));
	TestTrue(TEXT("The avatar description pipeline is registered"), HasInstantiablePipeline<UVRMAvatarDescriptionPipeline>(Paths));
	return true;
}

// Projects registered by earlier versions saved class paths for the material and avatar pipelines;
// the registration replaces them with the assets.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMPipelineRegistrationMigrationTest, "VRM.Pipeline.Registration.ReplacesClassPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMPipelineRegistrationMigrationTest::RunTest(const FString& Parameters)
{
	using namespace VRMPipelineRegistrationTestsPrivate;
	const TArray<FSoftObjectPath> Old =
	{
		FSoftObjectPath(TEXT("/VRMInterchange/DefaultPipelines/DefaultVRMAssetsPipeline.DefaultVRMAssetsPipeline")),
		FSoftObjectPath(TEXT("/Script/VRMInterchangeEditor.VRMMaterialPostImportPipeline")),
		FSoftObjectPath(TEXT("/Script/VRMInterchangeEditor.VRMAvatarDescriptionPipeline")),
	};
	const TArray<FSoftObjectPath> Paths = VRMImportPipelineRegistration::PreviewVRMTranslatorPipelines(&Old);
	if (Paths.Num() == 0)
	{
		AddError(TEXT("No VRM translator pipeline list: the project's Interchange settings have no \"Assets\" pipeline stack."));
		return false;
	}

	for (const FSoftObjectPath& Path : Paths)
	{
		TestFalse(*FString::Printf(TEXT("%s is not a class path"), *Path.ToString()), Path.ToString().StartsWith(TEXT("/Script/")));
	}
	TestTrue(TEXT("The material pipeline is registered"), HasInstantiablePipeline<UVRMMaterialPostImportPipeline>(Paths));
	TestTrue(TEXT("The avatar description pipeline is registered"), HasInstantiablePipeline<UVRMAvatarDescriptionPipeline>(Paths));

	// The VRM assets pipeline runs first, and the avatar description after the spring pipeline,
	// whose spring data it points at.
	auto IndexOf = [&Paths](const TCHAR* AssetName)
	{
		return Paths.IndexOfByPredicate([AssetName](const FSoftObjectPath& Path) { return Path.GetAssetName() == AssetName; });
	};
	TestEqual(TEXT("The VRM assets pipeline is first"), IndexOf(TEXT("DefaultVRMAssetsPipeline")), 0);
	const int32 Springs = IndexOf(TEXT("DefaultSpringBonesPipeline"));
	const int32 Avatar = IndexOf(TEXT("DefaultVRMAvatarDescriptionPipeline"));
	TestTrue(TEXT("The avatar description runs after the spring pipeline"), Springs != INDEX_NONE && Avatar > Springs);
	return true;
}

#endif
