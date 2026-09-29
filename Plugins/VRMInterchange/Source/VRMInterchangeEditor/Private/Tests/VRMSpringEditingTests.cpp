// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// P6.4: reimporting spring data from its source file, from the asset's editing tools.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "VRMSpringBoneData.h"
#include "VRMSpringBonesPostImportPipeline.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMSpringReimportFromSource, "VRM.SpringBones.Editing.ReimportFromSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMSpringReimportFromSource::RunTest(const FString& Parameters)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VRMInterchange"));
	if (!TestTrue(TEXT("Plugin found"), Plugin.IsValid())) return false;
	const FString Fixture = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Tests"), TEXT("Fixtures"), TEXT("vrm1_minimal.vrm"));

	UVRMSpringBoneData* Data = NewObject<UVRMSpringBoneData>(GetTransientPackage());
	FString Error;
	TestFalse(TEXT("No source file recorded"), UVRMSpringBonesPostImportPipeline::ReimportFromSource(Data, Error));

	Data->SourceFilename = Fixture;
	if (!TestTrue(FString::Printf(TEXT("Reimport (%s)"), *Error), UVRMSpringBonesPostImportPipeline::ReimportFromSource(Data, Error))) return false;
	if (!TestTrue(TEXT("Springs read"), Data->SpringConfig.Springs.Num() > 0 && Data->SpringConfig.Joints.Num() > 0)) return false;
	TestTrue(TEXT("File values recorded for Reset"), Data->HasSourceValues());
	TestFalse(TEXT("Source hash recorded"), Data->SourceHash.IsEmpty());
	const float FileStiffness = Data->SpringConfig.Joints[0].Stiffness;
	const float FileDrag = Data->SpringConfig.Joints[0].Drag;

	// Edits, then a reimport: the file's values come back, and running nodes see a new revision.
	Data->ScaleParameters(0.5f, 0.5f, 0.5f);
	const int32 RevisionBefore = Data->EditRevision;
	TestTrue(TEXT("Reimport again"), UVRMSpringBonesPostImportPipeline::ReimportFromSource(Data, Error));
	TestEqual(TEXT("Stiffness from the file"), Data->SpringConfig.Joints[0].Stiffness, FileStiffness, 1.0e-5f);
	TestEqual(TEXT("Drag from the file"), Data->SpringConfig.Joints[0].Drag, FileDrag, 1.0e-5f);
	TestTrue(TEXT("New revision"), Data->EditRevision > RevisionBefore);

	// A file that's gone leaves the data alone.
	const int32 JointsBefore = Data->SpringConfig.Joints.Num();
	Data->SourceFilename = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Tests"), TEXT("Fixtures"), TEXT("no_such_file.vrm"));
	TestFalse(TEXT("Missing file"), UVRMSpringBonesPostImportPipeline::ReimportFromSource(Data, Error));
	TestTrue(TEXT("The error names the file"), Error.Contains(TEXT("no_such_file.vrm")));
	TestEqual(TEXT("Data unchanged"), Data->SpringConfig.Joints.Num(), JointsBefore);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
