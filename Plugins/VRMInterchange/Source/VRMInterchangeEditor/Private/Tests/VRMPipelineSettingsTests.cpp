// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "InterchangeManager.h"
#include "PackageTools.h"
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

	/** Every settings-backed flag, flipped, and back. */
	void FlipSettings(UVRMInterchangeSettings& S)
	{
		S.bGenerateSpringBoneData = !S.bGenerateSpringBoneData;
		S.bOverwriteExistingSpringAssets = !S.bOverwriteExistingSpringAssets;
		S.bGeneratePostProcessAnimBP = !S.bGeneratePostProcessAnimBP;
		S.bAssignPostProcessABP = !S.bAssignPostProcessABP;
		S.bOverwriteExistingPostProcessABP = !S.bOverwriteExistingPostProcessABP;
		S.bReusePostProcessABPOnReimport = !S.bReusePostProcessABPOnReimport;
		S.bGenerateIKRigAssets = !S.bGenerateIKRigAssets;
		S.bGenerateLiveLinkEnabledActor = !S.bGenerateLiveLinkEnabledActor;
		S.bGenerateAvatarDescription = !S.bGenerateAvatarDescription;
	}
}

// The plugin's pipeline assets follow the project settings, not values saved in them: the spring
// pipeline asset was saved with flags that differed from the settings, so every import ignored them.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMPipelineAssetsFollowSettingsTest, "VRM.Pipeline.Settings.AssetsFollowProjectSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMPipelineAssetsFollowSettingsTest::RunTest(const FString& Parameters)
{
	using namespace VRMPipelineSettingsTestsPrivate;
	UVRMInterchangeSettings* Settings = GetMutableDefault<UVRMInterchangeSettings>();
	UVRMSpringBonesPostImportPipeline* Spring = LoadObject<UVRMSpringBonesPostImportPipeline>(nullptr, SpringAsset);
	UVRMIKRigPostImportPipeline* IKRig = LoadObject<UVRMIKRigPostImportPipeline>(nullptr, IKRigAsset);
	UVRMLiveLinkPostImportPipeline* LiveLink = LoadObject<UVRMLiveLinkPostImportPipeline>(nullptr, LiveLinkAsset);
	UVRMAvatarDescriptionPipeline* Avatar = LoadObject<UVRMAvatarDescriptionPipeline>(nullptr, AvatarAsset);
	if (!TestNotNull(TEXT("Settings"), Settings) || !TestNotNull(TEXT("Spring pipeline asset"), Spring) || !TestNotNull(TEXT("IK Rig pipeline asset"), IKRig)
		|| !TestNotNull(TEXT("Live Link pipeline asset"), LiveLink) || !TestNotNull(TEXT("Avatar pipeline asset"), Avatar))
	{
		return false;
	}

	// As loaded, with the project's settings (whatever they are).
	TestEqual(TEXT("Spring asset as loaded"), SpringFlags(*Spring), SettingsSpringFlags(*Settings));
	TestEqual(TEXT("IK Rig asset as loaded"), IKRig->bGenerateIKRig, Settings->bGenerateIKRigAssets);
	TestEqual(TEXT("Live Link asset as loaded"), LiveLink->bGenerateLiveLinkEnabledActor, Settings->bGenerateLiveLinkEnabledActor);
	TestEqual(TEXT("Avatar asset as loaded"), Avatar->bGenerateAvatarDescription, Settings->bGenerateAvatarDescription);

	// An edited setting reaches the loaded assets, and the copy Interchange makes for an import.
	FlipSettings(*Settings);
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

	// Loading the assets with settings that differ from the values saved in them: the settings win.
	// (The spring asset is saved with its post-process and Update Existing flags on.)
	TArray<UPackage*> Packages = { Spring->GetPackage(), IKRig->GetPackage(), LiveLink->GetPackage(), Avatar->GetPackage() };
	FText ReloadError;
	UPackageTools::ReloadPackages(Packages, ReloadError, EReloadPackagesInteractionMode::AssumePositive);
	Spring = LoadObject<UVRMSpringBonesPostImportPipeline>(nullptr, SpringAsset);
	IKRig = LoadObject<UVRMIKRigPostImportPipeline>(nullptr, IKRigAsset);
	LiveLink = LoadObject<UVRMLiveLinkPostImportPipeline>(nullptr, LiveLinkAsset);
	Avatar = LoadObject<UVRMAvatarDescriptionPipeline>(nullptr, AvatarAsset);
	if (TestNotNull(TEXT("Spring asset reloaded"), Spring) && TestNotNull(TEXT("IK Rig asset reloaded"), IKRig)
		&& TestNotNull(TEXT("Live Link asset reloaded"), LiveLink) && TestNotNull(TEXT("Avatar asset reloaded"), Avatar))
	{
		TestEqual(TEXT("Spring asset loaded with other settings"), SpringFlags(*Spring), SettingsSpringFlags(*Settings));
		TestEqual(TEXT("IK Rig asset loaded with other settings"), IKRig->bGenerateIKRig, Settings->bGenerateIKRigAssets);
		TestEqual(TEXT("Live Link asset loaded with other settings"), LiveLink->bGenerateLiveLinkEnabledActor, Settings->bGenerateLiveLinkEnabledActor);
		TestEqual(TEXT("Avatar asset loaded with other settings"), Avatar->bGenerateAvatarDescription, Settings->bGenerateAvatarDescription);
	}

	FlipSettings(*Settings);
	UVRMPipelineBase::ApplyProjectSettingsToLoadedAssets();
	return true;
}

// The defaults make the springs run out of the box, as the shipped pipeline asset did before.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMPipelineSettingsDefaultsTest, "VRM.Pipeline.Settings.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMPipelineSettingsDefaultsTest::RunTest(const FString& Parameters)
{
	// The pipelines' class defaults (their default objects don't take the project settings), which
	// the settings' own defaults match; a project's config can't change these.
	const UVRMSpringBonesPostImportPipeline* Spring = GetDefault<UVRMSpringBonesPostImportPipeline>();
	TestTrue(TEXT("Post-process AnimBlueprint made by default"), Spring->bGeneratePostProcessAnimBP);
	TestTrue(TEXT("and assigned by default"), Spring->bAssignPostProcessABP);
	TestTrue(TEXT("Spring data updated in place by default"), Spring->bOverwriteExisting);
	TestTrue(TEXT("IK Rig updated in place by default"), GetDefault<UVRMIKRigPostImportPipeline>()->bOverwriteExisting);
	return true;
}

#endif
