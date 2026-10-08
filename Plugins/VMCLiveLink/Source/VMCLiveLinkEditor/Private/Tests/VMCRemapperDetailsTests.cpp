// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "VMCLiveLinkRemapper.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "LiveLinkSubjectSettings.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Remapper/LiveLinkSkeletonRemapper.h"
#include "UObject/Package.h"

namespace VMCRemapperDetailsTests
{
	/** The top-level categories a details panel shows for Object, with the registered layouts applied. */
	TSet<FName> Categories(UObject* Object)
	{
		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		const TSharedRef<IPropertyRowGenerator> Rows = PropertyEditor.CreatePropertyRowGenerator(FPropertyRowGeneratorArgs());
		Rows->SetObjects({ Object });

		TSet<FName> Names;
		for (const TSharedRef<IDetailTreeNode>& Node : Rows->GetRootTreeNodes())
		{
			if (Node->GetNodeType() == EDetailNodeType::Category)
			{
				Names.Add(Node->GetNodeName());
			}
		}
		return Names;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCRemapperSubjectDetailsTest, "VMC.Remapper.SubjectDetails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVMCRemapperSubjectDetailsTest::RunTest(const FString& Parameters)
{
	using namespace VMCRemapperDetailsTests;
	const FName Tools(TEXT("Mapping Tools"));
	const FName Live(TEXT("Live Mapping"));

	// The remapper's own details: its layout applies.
	UVMCLiveLinkRemapper* Standalone = NewObject<UVMCLiveLinkRemapper>(GetTransientPackage());
	const TSet<FName> Own = Categories(Standalone);
	TestTrue(TEXT("Remapper details: Mapping Tools"), Own.Contains(Tools));
	TestTrue(TEXT("Remapper details: Live Mapping"), Own.Contains(Live));

	// The Live Link panel shows the subject's settings, with the remapper inline under Remapper.
	ULiveLinkSubjectSettings* Settings = NewObject<ULiveLinkSubjectSettings>(GetTransientPackage());
	TestFalse(TEXT("No remapper: no Mapping Tools"), Categories(Settings).Contains(Tools));

	Settings->Remapper = NewObject<ULiveLinkSkeletonRemapper>(Settings);
	TestFalse(TEXT("Another remapper: no Mapping Tools"), Categories(Settings).Contains(Tools));

	Settings->Remapper = NewObject<UVMCLiveLinkRemapper>(Settings);
	const TSet<FName> Subject = Categories(Settings);
	TestTrue(TEXT("VMC remapper: Mapping Tools in the subject details"), Subject.Contains(Tools));
	TestTrue(TEXT("VMC remapper: Live Mapping in the subject details"), Subject.Contains(Live));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
