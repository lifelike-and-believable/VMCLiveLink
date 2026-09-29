// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// P6.3: the report shown after a VRM import, and the import warnings it lists.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Blueprint.h"
#include "UObject/Package.h"
#include "VRMAvatarDescription.h"
#include "VRMCoreLog.h"
#include "VRMImportMessages.h"
#include "VRMImportReport.h"
#include "VRMSpringBoneData.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMImportMessagesTest, "VRM.Import.Messages",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMImportMessagesTest::RunTest(const FString& Parameters)
{
	// Warnings logged under the VRM categories between Begin and Take are collected; other
	// categories and lower verbosities aren't.
	AddExpectedError(TEXT("import report test warning"), EAutomationExpectedErrorFlags::Contains, 1);
	VRM::ImportMessages::Begin();
	TestTrue(TEXT("Collecting"), VRM::ImportMessages::IsCollecting());
	UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] import report test warning"));
	UE_LOG(LogVRMInterchange, Log, TEXT("[VRMInterchange] import report test log line"));
	UE_LOG(LogTemp, Display, TEXT("import report test, another category"));
	const TArray<VRM::ImportMessages::FMessage> Messages = VRM::ImportMessages::Take();
	TestFalse(TEXT("Take stops collecting"), VRM::ImportMessages::IsCollecting());
	if (!TestEqual(TEXT("One message"), Messages.Num(), 1)) return false;
	TestEqual(TEXT("A warning"), int32(Messages[0].Verbosity), int32(ELogVerbosity::Warning));
	TestEqual(TEXT("Without the log prefix"), Messages[0].Text, FString(TEXT("import report test warning")));
	TestEqual(TEXT("Nothing more once taken"), VRM::ImportMessages::Take().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMImportReportTest, "VRM.Import.Report",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMImportReportTest::RunTest(const FString& Parameters)
{
	FVRMImportReport& Report = FVRMImportReport::Get();
	Report.Discard();

	UVRMSpringBoneData* Spring = NewObject<UVRMSpringBoneData>(GetTransientPackage(), TEXT("Alice_SpringData"));
	UVRMAvatarDescription* Avatar = NewObject<UVRMAvatarDescription>(GetTransientPackage(), TEXT("Alice_Avatar"));
	UBlueprint* Actor = NewObject<UBlueprint>(GetTransientPackage(), TEXT("BP_LL_VRM_Alice"));

	Report.AddAsset(Spring, false);
	TestEqual(TEXT("Nothing is reported before a file begins"), Report.GetPendingFiles().Num(), 0);

	Report.BeginFile(TEXT("C:/Avatars/Alice.vrm"), /*bReimport*/ false);
	Report.AddAsset(Spring, /*bUpdated*/ false);
	Report.AddAsset(Avatar, /*bUpdated*/ true);
	Report.AddAsset(Actor, /*bUpdated*/ true);
	Report.AddAsset(Spring, /*bUpdated*/ false); // a second pipeline reporting the same asset
	Report.SetLicense(TEXT("Licence: CC0"));

	if (!TestEqual(TEXT("One file"), Report.GetPendingFiles().Num(), 1)) return false;
	const FVRMImportReport::FFile& File = Report.GetPendingFiles()[0];
	TestEqual(TEXT("Each asset once"), File.Assets.Num(), 3);

	const FString Text = FVRMImportReport::Describe(File);
	AddInfo(Text);
	TestTrue(TEXT("Created"), Text.Contains(TEXT("Created: Alice_SpringData (Spring data).")));
	TestTrue(TEXT("Updated from the file"), Text.Contains(TEXT("edits to them replaced): Alice_Avatar (Avatar description).")));
	TestTrue(TEXT("Blueprints keep edits"), Text.Contains(TEXT("(your edits kept): BP_LL_VRM_Alice (Actor Blueprint).")));
	TestTrue(TEXT("Licence"), Text.Contains(TEXT("Licence: CC0")));
	TestFalse(TEXT("Not a reimport"), Text.Contains(TEXT("Reimport")));

	// The same file again, as a reimport, adds to the same entry.
	Report.BeginFile(TEXT("C:/Avatars/Alice.vrm"), /*bReimport*/ true);
	TestEqual(TEXT("Still one file"), Report.GetPendingFiles().Num(), 1);
	TestTrue(TEXT("Reimport noted"), FVRMImportReport::Describe(Report.GetPendingFiles()[0]).Contains(TEXT("Reimport:")));

	Report.Discard();
	TestEqual(TEXT("Discarded"), Report.GetPendingFiles().Num(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
