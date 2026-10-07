// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "InterchangeManager.h"
#include "UObject/Package.h"
#include "VRMInterchangeSettings.h"
#include "VRMSpringBonesPostImportPipeline.h"
#include "VRMIKRigPostImportPipeline.h"
#include "VRMLiveLinkPostImportPipeline.h"
#include "VRMAvatarDescriptionPipeline.h"

namespace VRMPipelineSettingsTestsPrivate
{
	const TCHAR* const SpringAsset = TEXT("/VRMInterchange/DefaultPipelines/DefaultSpringBonesPipeline.DefaultSpringBonesPipeline");
	const TCHAR* const IKRigAsset = TEXT("/VRMInterchange/DefaultPipelines/DefaultVRMIKRigPipeline.DefaultVRMIKRigPipeline");
	const TCHAR* const LiveLinkAsset = TEXT("/VRMInterchange/DefaultPipelines/DefaultVRMLiveLinkPipeline.DefaultVRMLiveLinkPipeline");
	const TCHAR* const AvatarAsset = TEXT("/VRMInterchange/DefaultPipelines/DefaultVRMAvatarDescriptionPipeline.DefaultVRMAvatarDescriptionPipeline");

	/** The settings-backed spring flags of a pipeline, as one comparable string. */
	FString SpringFlags(const UVRMSpringBonesPostImportPipeline& P)
	{
		return FString::Printf(TEXT("Generate=%d UpdateExisting=%d PostProcess=%d Assign=%d UpdateABP=%d ReuseABP=%d"),
			P.bGenerateSpringBoneData, P.bOverwriteExisting, P.bGeneratePostProcessAnimBP, P.bAssignPostProcessABP,
			P.bOverwriteExistingPostProcessABP, P.bReusePostProcessABPOnReimport);
	}

	FString SettingsSpringFlags(const UVRMInterchangeSettings& S)
	{
		return FString::Printf(TEXT("Generate=%d UpdateExisting=%d PostProcess=%d Assign=%d UpdateABP=%d ReuseABP=%d"),
			S.bGenerateSpringBoneData, S.bOverwriteExistingSpringAssets, S.bGeneratePostProcessAnimBP, S.bAssignPostProcessABP,
			S.bOverwriteExistingPostProcessABP, S.bReusePostProcessABPOnReimport);
	}

	/** Sets every settings-backed spring flag of a pipeline to the opposite of the project setting. */
	void SetSpringFlagsAgainstSettings(UVRMSpringBonesPostImportPipeline& P, const UVRMInterchangeSettings& S)
	{
		P.bGenerateSpringBoneData = !S.bGenerateSpringBoneData;
		P.bOverwriteExisting = !S.bOverwriteExistingSpringAssets;
		P.bGeneratePostProcessAnimBP = !S.bGeneratePostProcessAnimBP;
		P.bAssignPostProcessABP = !S.bAssignPostProcessABP;
		P.bOverwriteExistingPostProcessABP = !S.bOverwriteExistingPostProcessABP;
		P.bReusePostProcessABPOnReimport = !S.bReusePostProcessABPOnReimport;
	}

	/** The project settings this file changes, restored when the scope ends. */
	struct FScopedSettings
	{
		UVRMInterchangeSettings* Settings = GetMutableDefault<UVRMInterchangeSettings>();
		const bool bSpring = Settings->bGenerateSpringBoneData;
		const bool bSpringUpdate = Settings->bOverwriteExistingSpringAssets;
		const bool bPostProcess = Settings->bGeneratePostProcessAnimBP;
		const bool bAssign = Settings->bAssignPostProcessABP;
		const bool bUpdateABP = Settings->bOverwriteExistingPostProcessABP;
		const bool bReuseABP = Settings->bReusePostProcessABPOnReimport;
		const bool bIKRig = Settings->bGenerateIKRigAssets;
		const bool bLiveLink = Settings->bGenerateLiveLinkEnabledActor;
		const bool bAvatar = Settings->bGenerateAvatarDescription;
		~FScopedSettings()
		{
			Settings->bGenerateSpringBoneData = bSpring;
			Settings->bOverwriteExistingSpringAssets = bSpringUpdate;
			Settings->bGeneratePostProcessAnimBP = bPostProcess;
			Settings->bAssignPostProcessABP = bAssign;
			Settings->bOverwriteExistingPostProcessABP = bUpdateABP;
			Settings->bReusePostProcessABPOnReimport = bReuseABP;
			Settings->bGenerateIKRigAssets = bIKRig;
			Settings->bGenerateLiveLinkEnabledActor = bLiveLink;
			Settings->bGenerateAvatarDescription = bAvatar;
			UVRMPipelineBase::ApplyProjectSettingsToLoadedAssets();
		}
	};
}

// The plugin's pipeline assets follow the project settings, not values saved in them: the spring
// pipeline asset was saved with flags that differed from the settings, so every import ignored them.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMPipelineAssetsFollowSettingsTest, "VRM.Pipeline.Settings.AssetsFollowProjectSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMPipelineAssetsFollowSettingsTest::RunTest(const FString& Parameters)
{
	using namespace VRMPipelineSettingsTestsPrivate;
	FScopedSettings Scoped;
	UVRMInterchangeSettings* Settings = Scoped.Settings;
	UVRMSpringBonesPostImportPipeline* Spring = LoadObject<UVRMSpringBonesPostImportPipeline>(nullptr, SpringAsset);
	UVRMIKRigPostImportPipeline* IKRig = LoadObject<UVRMIKRigPostImportPipeline>(nullptr, IKRigAsset);
	UVRMLiveLinkPostImportPipeline* LiveLink = LoadObject<UVRMLiveLinkPostImportPipeline>(nullptr, LiveLinkAsset);
	UVRMAvatarDescriptionPipeline* Avatar = LoadObject<UVRMAvatarDescriptionPipeline>(nullptr, AvatarAsset);
	if (!TestNotNull(TEXT("Spring pipeline asset"), Spring) || !TestNotNull(TEXT("IK Rig pipeline asset"), IKRig)
		|| !TestNotNull(TEXT("Live Link pipeline asset"), LiveLink) || !TestNotNull(TEXT("Avatar pipeline asset"), Avatar))
	{
		return false;
	}
	TestTrue(TEXT("The plugin's pipeline assets follow the project settings"), Spring->FollowsProjectSettings());

	// As loaded, with the project's settings (whatever they are).
	TestEqual(TEXT("Spring asset as loaded"), SpringFlags(*Spring), SettingsSpringFlags(*Settings));
	TestEqual(TEXT("IK Rig asset as loaded"), IKRig->bGenerateIKRig, Settings->bGenerateIKRigAssets);
	TestEqual(TEXT("Live Link asset as loaded"), LiveLink->bGenerateLiveLinkEnabledActor, Settings->bGenerateLiveLinkEnabledActor);
	TestEqual(TEXT("Avatar asset as loaded"), Avatar->bGenerateAvatarDescription, Settings->bGenerateAvatarDescription);

	// Loading: values in the asset (as if saved there) give way to the settings. This runs the asset's
	// own load step again on the loaded object, as loading the package does.
	SetSpringFlagsAgainstSettings(*Spring, *Settings);
	Spring->SetFlags(RF_NeedPostLoad);
	Spring->ConditionalPostLoad();
	TestEqual(TEXT("Spring asset after loading with other saved values"), SpringFlags(*Spring), SettingsSpringFlags(*Settings));

	// An edited setting reaches the loaded assets, and the copy Interchange makes for an import.
	Settings->bGeneratePostProcessAnimBP = !Settings->bGeneratePostProcessAnimBP;
	Settings->bOverwriteExistingSpringAssets = !Settings->bOverwriteExistingSpringAssets;
	Settings->bGenerateIKRigAssets = !Settings->bGenerateIKRigAssets;
	Settings->bGenerateLiveLinkEnabledActor = !Settings->bGenerateLiveLinkEnabledActor;
	Settings->bGenerateAvatarDescription = !Settings->bGenerateAvatarDescription;
	UVRMPipelineBase::ApplyProjectSettingsToLoadedAssets();
	TestEqual(TEXT("Spring asset after a settings edit"), SpringFlags(*Spring), SettingsSpringFlags(*Settings));
	TestEqual(TEXT("IK Rig asset after a settings edit"), IKRig->bGenerateIKRig, Settings->bGenerateIKRigAssets);
	TestEqual(TEXT("Live Link asset after a settings edit"), LiveLink->bGenerateLiveLinkEnabledActor, Settings->bGenerateLiveLinkEnabledActor);
	TestEqual(TEXT("Avatar asset after a settings edit"), Avatar->bGenerateAvatarDescription, Settings->bGenerateAvatarDescription);
	if (const UVRMSpringBonesPostImportPipeline* Instance = Cast<UVRMSpringBonesPostImportPipeline>(UE::Interchange::GeneratePipelineInstance(FSoftObjectPath(SpringAsset))))
	{
		TestEqual(TEXT("Spring pipeline instance for an import"), SpringFlags(*Instance), SettingsSpringFlags(*Settings));
	}
	else
	{
		AddError(TEXT("Interchange made no spring pipeline instance from the asset"));
	}
	return true;
}

// The import dialog restores the values used last time, then calls PreDialogCleanup: the project
// settings win for a new import.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMPipelineDialogTakesSettingsTest, "VRM.Pipeline.Settings.DialogTakesProjectSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMPipelineDialogTakesSettingsTest::RunTest(const FString& Parameters)
{
	using namespace VRMPipelineSettingsTestsPrivate;
	FScopedSettings Scoped;
	UVRMSpringBonesPostImportPipeline* Instance = Cast<UVRMSpringBonesPostImportPipeline>(UE::Interchange::GeneratePipelineInstance(FSoftObjectPath(SpringAsset)));
	if (!TestNotNull(TEXT("Spring pipeline instance"), Instance))
	{
		return false;
	}
	TestFalse(TEXT("An import's copy isn't a pipeline asset"), Instance->FollowsProjectSettings());
	SetSpringFlagsAgainstSettings(*Instance, *Scoped.Settings); // as the dialog's saved values might
	Instance->PreDialogCleanup(NAME_None);
	TestEqual(TEXT("Spring options when the dialog opens"), SpringFlags(*Instance), SettingsSpringFlags(*Scoped.Settings));
	return true;
}

// A copy of a pipeline keeps its values, as Interchange copies them for reimport and "Import All",
// even where a value equals the class default and the project setting differs.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMPipelineCopyKeepsChoicesTest, "VRM.Pipeline.Settings.CopyKeepsChoices",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMPipelineCopyKeepsChoicesTest::RunTest(const FString& Parameters)
{
	using namespace VRMPipelineSettingsTestsPrivate;
	FScopedSettings Scoped;
	const UVRMSpringBonesPostImportPipeline* Defaults = GetDefault<UVRMSpringBonesPostImportPipeline>();
	// Every setting opposite to the class default, every choice equal to it.
	Scoped.Settings->bGenerateSpringBoneData = !Defaults->bGenerateSpringBoneData;
	Scoped.Settings->bOverwriteExistingSpringAssets = !Defaults->bOverwriteExisting;
	Scoped.Settings->bGeneratePostProcessAnimBP = !Defaults->bGeneratePostProcessAnimBP;
	Scoped.Settings->bAssignPostProcessABP = !Defaults->bAssignPostProcessABP;
	Scoped.Settings->bOverwriteExistingPostProcessABP = !Defaults->bOverwriteExistingPostProcessABP;
	Scoped.Settings->bReusePostProcessABPOnReimport = !Defaults->bReusePostProcessABPOnReimport;

	UVRMSpringBonesPostImportPipeline* Chosen = NewObject<UVRMSpringBonesPostImportPipeline>(GetTransientPackage());
	const FString ChosenFlags = SpringFlags(*Chosen);
	TestEqual(TEXT("The choices equal the class defaults"), ChosenFlags, SpringFlags(*Defaults));
	const UVRMSpringBonesPostImportPipeline* Copy = DuplicateObject(Chosen, GetTransientPackage());
	TestEqual(TEXT("A copy keeps the choices"), SpringFlags(*Copy), ChosenFlags);
	return true;
}

// The IK Rig's Update Existing has no project setting: the plugin's pipeline asset holds it, on.
// (The pipeline classes keep their old defaults, off, so pipeline assets a studio saved with them
// load unchanged; the spring flags' new defaults are the project settings'.)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMPipelineSettingsDefaultsTest, "VRM.Pipeline.Settings.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMPipelineSettingsDefaultsTest::RunTest(const FString& Parameters)
{
	using namespace VRMPipelineSettingsTestsPrivate;
	const UVRMIKRigPostImportPipeline* IKRig = LoadObject<UVRMIKRigPostImportPipeline>(nullptr, IKRigAsset);
	if (TestNotNull(TEXT("IK Rig pipeline asset"), IKRig))
	{
		TestTrue(TEXT("IK Rig updated in place by default"), IKRig->bOverwriteExisting);
	}
	TestFalse(TEXT("Pipeline class default unchanged (studio assets saved with it load as saved)"), GetDefault<UVRMIKRigPostImportPipeline>()->bOverwriteExisting);
	const UVRMSpringBonesPostImportPipeline* Spring = GetDefault<UVRMSpringBonesPostImportPipeline>();
	TestFalse(TEXT("Spring class defaults unchanged"), Spring->bGeneratePostProcessAnimBP || Spring->bAssignPostProcessABP || Spring->bOverwriteExisting);
	return true;
}

#endif
