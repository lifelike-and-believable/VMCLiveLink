// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// Tests for the post-import pipelines' toggles (P1.15) and their post-import step (P3.4).
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/SkeletalMesh.h"
#include "InterchangeSourceData.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "UObject/Package.h"
#include "VRMInterchangeSettings.h"
#include "VRMIKRigPostImportPipeline.h"
#include "VRMLiveLinkPostImportPipeline.h"
#include "VRMPipelineTargets.h"
#include "VRMSpringBonesPostImportPipeline.h"
#include "VRMSpringBoneData.h"
#include "Rig/IKRigDefinition.h"
#include "Misc/SecureHash.h"
#include "UObject/UObjectIterator.h"

namespace VRMPipelineTargetTests
{
	const TCHAR* const ContentBase = TEXT("/Game/VRMPipelineTests");

	FString SourceFile(const TCHAR* Name)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("VRMPipelineTests") / (FString(Name) + TEXT(".vrm")));
	}

	/** A skeletal mesh at PackagePath, optionally recording SourceFilename as where it was imported from. */
	USkeletalMesh* MakeMesh(const FString& PackagePath, const FString& SourceFilename)
	{
		UPackage* Package = CreatePackage(*PackagePath);
		USkeletalMesh* Mesh = NewObject<USkeletalMesh>(Package, *FPaths::GetBaseFilename(PackagePath), RF_Transient);
		if (!SourceFilename.IsEmpty())
		{
			UAssetImportData* ImportData = NewObject<UAssetImportData>(Mesh, TEXT("AssetImportData"));
			ImportData->Update(SourceFilename);
			Mesh->SetAssetImportData(ImportData);
		}
		return Mesh;
	}

	FString FixturePath()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VRMInterchange"));
		return Plugin.IsValid() ? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Tests"), TEXT("Fixtures"), TEXT("vrm1_minimal.vrm")) : FString();
	}

	/** Runs a pipeline's ExecutePipeline on the vrm1_minimal fixture, as an import would. */
	template <typename PipelineType>
	void Execute(PipelineType* Pipeline)
	{
		UInterchangeSourceData* Source = NewObject<UInterchangeSourceData>();
		Source->SetFilename(FixturePath());
		TArray<UInterchangeSourceData*> Sources = { Source };
		Pipeline->ExecutePipeline(NewObject<UInterchangeBaseNodeContainer>(), Sources, ContentBase);
	}

	/** Changes project settings for the lifetime of the scope. */
	struct FScopedSettings
	{
		UVRMInterchangeSettings* Settings = GetMutableDefault<UVRMInterchangeSettings>();
		const bool bSpring = Settings->bGenerateSpringBoneData;
		const bool bABP = Settings->bGeneratePostProcessAnimBP;
		const bool bIKRig = Settings->bGenerateIKRigAssets;
		const bool bLiveLink = Settings->bGenerateLiveLinkEnabledActor;
		~FScopedSettings()
		{
			Settings->bGenerateSpringBoneData = bSpring;
			Settings->bGeneratePostProcessAnimBP = bABP;
			Settings->bGenerateIKRigAssets = bIKRig;
			Settings->bGenerateLiveLinkEnabledActor = bLiveLink;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMPipelinePathBoundary, "VRM.Pipeline.Targets.PathBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMPipelinePathBoundary::RunTest(const FString& Parameters)
{
	using VRMPipeline::IsUnderPath;
	TestTrue(TEXT("A package in the folder"), IsUnderPath(TEXT("/Game/Chars/Alice/SK_Alice"), TEXT("/Game/Chars/Alice")));
	TestTrue(TEXT("The folder itself"), IsUnderPath(TEXT("/Game/Chars/Alice"), TEXT("/Game/Chars/Alice")));
	TestTrue(TEXT("A trailing slash on the root"), IsUnderPath(TEXT("/Game/Chars/Alice/SK_Alice"), TEXT("/Game/Chars/Alice/")));
	TestFalse(TEXT("A sibling whose name starts the same"), IsUnderPath(TEXT("/Game/Chars/Alice2/SK_Alice2"), TEXT("/Game/Chars/Alice")));
	TestFalse(TEXT("The parent folder"), IsUnderPath(TEXT("/Game/Chars/SK_Bob"), TEXT("/Game/Chars/Alice")));
	TestFalse(TEXT("An empty root matches nothing"), IsUnderPath(TEXT("/Game/Chars/Alice"), TEXT("")));
	TestEqual(TEXT("Character folder"), VRMPipeline::MakeCharacterBasePath(TEXT("C:/Avatars/Alice.vrm"), TEXT("/Game/Chars")), FString(TEXT("/Game/Chars/Alice")));
	TestEqual(TEXT("Character folder without a content path"), VRMPipeline::MakeCharacterBasePath(TEXT("C:/Avatars/Alice.vrm"), TEXT("")), FString(TEXT("/Game/Alice")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMPipelinePostImportSpringData, "VRM.Pipeline.PostImport.SpringData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMPipelinePostImportSpringData::RunTest(const FString& Parameters)
{
	// The spring pipeline stages its data in ExecutePipeline and creates the asset when Interchange
	// reports this import's skeletal mesh (P3.4). Overwrite updates the asset in place.
	using namespace VRMPipelineTargetTests;
	if (!TestFalse(TEXT("Fixture found"), FixturePath().IsEmpty()))
	{
		return false;
	}
	FScopedSettings Scoped;
	const FString CharacterFolder = FString(ContentBase) / TEXT("vrm1_minimal");
	const FString SpringFolder = CharacterFolder / TEXT("SpringBones");
	USkeletalMesh* Mesh = MakeMesh(CharacterFolder / TEXT("SK_PostImportSpring"), FixturePath());

	auto CountSpringAssets = [&SpringFolder]()
	{
		int32 Count = 0;
		for (TObjectIterator<UVRMSpringBoneData> It; It; ++It)
		{
			if (VRMPipeline::IsUnderPath(It->GetOutermost()->GetName(), SpringFolder) && It->GetName().StartsWith(TEXT("SK_PostImportSpring_SpringData")))
			{
				++Count;
			}
		}
		return Count;
	};
	auto MakePipeline = [](bool bOverwrite)
	{
		UVRMSpringBonesPostImportPipeline* Pipeline = NewObject<UVRMSpringBonesPostImportPipeline>();
		Pipeline->bGenerateSpringBoneData = true;
		Pipeline->bGeneratePostProcessAnimBP = false;
		Pipeline->bOverwriteExisting = bOverwrite;
		return Pipeline;
	};

	const int32 Before = CountSpringAssets();
	UVRMSpringBonesPostImportPipeline* First = MakePipeline(true);
	Execute(First);
	TestTrue(TEXT("Staged, waiting for the mesh"), First->HasPendingPostImportWork());
	First->HandleImportedAsset(NewObject<UAssetImportData>(), false);
	TestTrue(TEXT("Other assets of the import don't finish the job"), First->HasPendingPostImportWork());
	First->HandleImportedAsset(Mesh, false);
	TestFalse(TEXT("The mesh finishes the job"), First->HasPendingPostImportWork());

	const FString AssetPath = SpringFolder / TEXT("SK_PostImportSpring_SpringData.SK_PostImportSpring_SpringData");
	UVRMSpringBoneData* Created = FindObject<UVRMSpringBoneData>(nullptr, *AssetPath);
	if (!TestNotNull(TEXT("Spring data created next to the character"), Created))
	{
		return false;
	}
	TestTrue(TEXT("It holds the parsed springs"), Created->SpringConfig.Joints.Num() > 0);
	TestEqual(TEXT("It records the file's hash"), Created->SourceHash, LexToString(FMD5Hash::HashFile(*FixturePath())));
	const int32 AfterFirst = CountSpringAssets();

	// Overwrite: the same asset is updated, nothing new is created.
	Created->SpringConfig.Joints.Reset();
	UVRMSpringBonesPostImportPipeline* Overwrite = MakePipeline(true);
	Execute(Overwrite);
	Overwrite->HandleImportedAsset(Mesh, true);
	TestTrue(TEXT("Overwrite keeps the same object"), FindObject<UVRMSpringBoneData>(nullptr, *AssetPath) == Created);
	TestTrue(TEXT("... and replaces its data"), Created->SpringConfig.Joints.Num() > 0);
	TestEqual(TEXT("... without another asset"), CountSpringAssets(), AfterFirst);

	// Without overwrite: a new asset under a unique name, the old one untouched.
	UVRMSpringBonesPostImportPipeline* Unique = MakePipeline(false);
	Execute(Unique);
	Unique->HandleImportedAsset(Mesh, false);
	TestEqual(TEXT("No overwrite: one more asset"), CountSpringAssets(), AfterFirst + 1);
	TestTrue(TEXT("... the first one is still there"), FindObject<UVRMSpringBoneData>(nullptr, *AssetPath) == Created);
	TestTrue(TEXT("At least one asset was made"), AfterFirst > Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMPipelinePostImportIKRig, "VRM.Pipeline.PostImport.IKRig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMPipelinePostImportIKRig::RunTest(const FString& Parameters)
{
	// The IK Rig is copied from the template with IAssetTools::DuplicateAsset once the mesh arrives.
	using namespace VRMPipelineTargetTests;
	if (!TestFalse(TEXT("Fixture found"), FixturePath().IsEmpty()))
	{
		return false;
	}
	FScopedSettings Scoped;
	const FString CharacterFolder = FString(ContentBase) / TEXT("vrm1_minimal");
	USkeletalMesh* Mesh = MakeMesh(CharacterFolder / TEXT("SK_PostImportIK"), FixturePath());

	UVRMIKRigPostImportPipeline* Pipeline = NewObject<UVRMIKRigPostImportPipeline>();
	Pipeline->bGenerateIKRig = true;
	Pipeline->bOverwriteExisting = true;
	Execute(Pipeline);
	Pipeline->HandleImportedAsset(Mesh, false);
	TestFalse(TEXT("Done"), Pipeline->HasPendingPostImportWork());

	const FString RigPath = CharacterFolder / TEXT("IKRigDefinition") / TEXT("IK_Rig_VRM_SK_PostImportIK.IK_Rig_VRM_SK_PostImportIK");
	UIKRigDefinition* Rig = FindObject<UIKRigDefinition>(nullptr, *RigPath);
	if (!TestNotNull(TEXT("IK Rig created"), Rig))
	{
		return false;
	}
	TestTrue(TEXT("Its preview mesh is the imported mesh"), Rig->GetPreviewMesh() == Mesh);

	// Reimport with overwrite reuses it.
	UVRMIKRigPostImportPipeline* Again = NewObject<UVRMIKRigPostImportPipeline>();
	Again->bGenerateIKRig = true;
	Again->bOverwriteExisting = true;
	Execute(Again);
	Again->HandleImportedAsset(Mesh, true);
	TestTrue(TEXT("Overwrite reuses the IK Rig"), FindObject<UIKRigDefinition>(nullptr, *RigPath) == Rig);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMPipelineToggles, "VRM.Pipeline.Toggles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMPipelineToggles::RunTest(const FString& Parameters)
{
	using namespace VRMPipelineTargetTests;
	if (!TestFalse(TEXT("Fixture found"), FixturePath().IsEmpty()))
	{
		return false;
	}
	FScopedSettings Scoped;
	UVRMInterchangeSettings* Settings = Scoped.Settings;

	// Spring bones: the project setting only seeds the default; an unticked box in the dialog wins.
	Settings->bGenerateSpringBoneData = true;
	Settings->bGeneratePostProcessAnimBP = false;
	{
		UVRMSpringBonesPostImportPipeline* Pipeline = NewObject<UVRMSpringBonesPostImportPipeline>();
		TestTrue(TEXT("Spring: new pipeline takes the project setting"), Pipeline->bGenerateSpringBoneData);
		Pipeline->bGenerateSpringBoneData = false;
		Execute(Pipeline);
		TestFalse(TEXT("Spring: unticked in the dialog, nothing is generated"), Pipeline->HasPendingPostImportWork());
	}
	{
		UVRMSpringBonesPostImportPipeline* Pipeline = NewObject<UVRMSpringBonesPostImportPipeline>();
		Execute(Pipeline);
		TestTrue(TEXT("Spring: ticked, spring data is staged"), Pipeline->HasPendingPostImportWork());
	}

	// IK Rig: likewise, and a ticked box works even when the project setting is off.
	Settings->bGenerateIKRigAssets = false;
	{
		UVRMIKRigPostImportPipeline* Pipeline = NewObject<UVRMIKRigPostImportPipeline>();
		TestFalse(TEXT("IK Rig: new pipeline takes the project setting"), Pipeline->bGenerateIKRig);
		Pipeline->bGenerateIKRig = true;
		Execute(Pipeline);
		TestTrue(TEXT("IK Rig: ticked in the dialog, it runs although the project setting is off"), Pipeline->HasPendingPostImportWork());
	}
	Settings->bGenerateIKRigAssets = true;
	{
		UVRMIKRigPostImportPipeline* Pipeline = NewObject<UVRMIKRigPostImportPipeline>();
		Pipeline->bGenerateIKRig = false;
		Execute(Pipeline);
		TestFalse(TEXT("IK Rig: unticked in the dialog, nothing is generated"), Pipeline->HasPendingPostImportWork());
	}

	// Live Link: the retarget actor alone is reason enough to run.
	Settings->bGenerateLiveLinkEnabledActor = false;
	{
		UVRMLiveLinkPostImportPipeline* Pipeline = NewObject<UVRMLiveLinkPostImportPipeline>();
		TestFalse(TEXT("Live Link: new pipeline takes the project setting"), Pipeline->bGenerateLiveLinkEnabledActor);
		Pipeline->bGenerateLiveLinkRetargetActor = false;
		Execute(Pipeline);
		TestFalse(TEXT("Live Link: both unticked, nothing is generated"), Pipeline->HasPendingPostImportWork());
	}
	{
		UVRMLiveLinkPostImportPipeline* Pipeline = NewObject<UVRMLiveLinkPostImportPipeline>();
		Pipeline->bGenerateLiveLinkEnabledActor = false;
		Pipeline->bGenerateLiveLinkRetargetActor = true;
		Execute(Pipeline);
		TestTrue(TEXT("Live Link: only the retarget actor ticked, it runs"), Pipeline->HasPendingPostImportWork());
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
