// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// Tests for the post-import pipelines' toggles and target resolution (P1.15).
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMPipelineSiblingCharacters, "VRM.Pipeline.Targets.SiblingCharacters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMPipelineSiblingCharacters::RunTest(const FString& Parameters)
{
	using namespace VRMPipelineTargetTests;
	const FString Alice = SourceFile(TEXT("Alice"));
	const FString Alice2 = SourceFile(TEXT("Alice2"));
	const FString AliceFolder = FString(ContentBase) / TEXT("Alice");

	// Alice and Alice2 imported into sibling folders; Alice's pipelines must take only Alice's mesh.
	USkeletalMesh* AliceMesh = MakeMesh(AliceFolder / TEXT("SK_Alice"), Alice);
	USkeletalMesh* Alice2Mesh = MakeMesh(FString(ContentBase) / TEXT("Alice2") / TEXT("SK_Alice2"), Alice2);
	TestTrue(TEXT("Alice's mesh belongs to Alice's import"),
		VRMPipeline::ResolveImportedMesh(AliceMesh, Alice, AliceFolder, ContentBase) == AliceMesh);
	TestTrue(TEXT("Alice2's mesh does not belong to Alice's import"),
		VRMPipeline::ResolveImportedMesh(Alice2Mesh, Alice, AliceFolder, ContentBase) == nullptr);

	// The source file decides wherever the mesh landed, e.g. directly in the content folder.
	USkeletalMesh* FlatAlice = MakeMesh(FString(ContentBase) / TEXT("SK_AliceFlat"), Alice);
	TestTrue(TEXT("Alice's mesh outside the character folder still belongs to Alice"),
		VRMPipeline::MeshBelongsToImport(FlatAlice, Alice, AliceFolder));
	USkeletalMesh* AliceFromOtherFile = MakeMesh(AliceFolder / TEXT("SK_Other"), SourceFile(TEXT("Bob")));
	TestFalse(TEXT("A mesh in Alice's folder imported from another file is not Alice's"),
		VRMPipeline::MeshBelongsToImport(AliceFromOtherFile, Alice, AliceFolder));

	// Without import data, only the character folder counts, with a folder boundary.
	TestTrue(TEXT("No import data, in Alice's folder"),
		VRMPipeline::MeshBelongsToImport(MakeMesh(AliceFolder / TEXT("SK_NoData"), FString()), Alice, AliceFolder));
	TestFalse(TEXT("No import data, in Alice2's folder"),
		VRMPipeline::MeshBelongsToImport(MakeMesh(FString(ContentBase) / TEXT("Alice2") / TEXT("SK_NoData"), FString()), Alice, AliceFolder));

	TestTrue(TEXT("Objects that are not meshes or skeletons are ignored"),
		VRMPipeline::ResolveImportedMesh(NewObject<UAssetImportData>(), Alice, AliceFolder, ContentBase) == nullptr);
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
