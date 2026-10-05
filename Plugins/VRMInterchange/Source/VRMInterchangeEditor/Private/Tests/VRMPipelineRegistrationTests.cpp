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
	const TArray<FSoftObjectPath> Paths = VRMImportPipelineRegistration::PreviewVRMTranslatorPipelines();
	if (!TestTrue(TEXT("The registration produces a VRM translator pipeline list"), Paths.Num() > 0))
	{
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

	for (const FSoftObjectPath& Path : Paths)
	{
		TestFalse(*FString::Printf(TEXT("%s is not a class path"), *Path.ToString()), Path.ToString().StartsWith(TEXT("/Script/")));
	}
	TestTrue(TEXT("The material pipeline is registered"), HasInstantiablePipeline<UVRMMaterialPostImportPipeline>(Paths));
	TestTrue(TEXT("The avatar description pipeline is registered"), HasInstantiablePipeline<UVRMAvatarDescriptionPipeline>(Paths));
	return true;
}

#endif
