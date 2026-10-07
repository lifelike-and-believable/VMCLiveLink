// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMInterchangeSettings.h"
#include "VRMImportPipelineRegistration.h"
#include "VRMPipelineBase.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "VRMInterchangeSettings"


UVRMInterchangeSettings::UVRMInterchangeSettings()
{
	// helps where it appears in the Settings tree (optional)
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("VRM Interchange");
	// Sensible defaults
	// The springs run out of the box: the post-process AnimBlueprint is made and assigned, and a
	// reimport updates the spring data and reuses the AnimBlueprint in place.
	bGenerateSpringBoneData = true;
	bGeneratePostProcessAnimBP = true;
	bAssignPostProcessABP = true;
	bOverwriteExistingSpringAssets = true;
	bOverwriteExistingPostProcessABP = false;
	bReusePostProcessABPOnReimport = true;
}

void UVRMInterchangeSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// The pipeline assets already loaded took the old values; the next import should use the new ones.
	UVRMPipelineBase::ApplyProjectSettingsToLoadedAssets();
}

void UVRMInterchangeSettings::RegisterImportPipelines()
{
	const bool bChanged = VRMImportPipelineRegistration::Apply();

	FNotificationInfo Info(bChanged
		? LOCTEXT("PipelinesRegistered", "VRM import pipelines registered in Project Settings > Interchange.")
		: LOCTEXT("PipelinesAlreadyRegistered", "VRM import pipelines are already registered."));
	Info.ExpireDuration = 5.0f;
	FSlateNotificationManager::Get().AddNotification(Info);
}

#undef LOCTEXT_NAMESPACE
