// Copyright (c) 2025 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMInterchangeSettings.h"
#include "VRMImportPipelineRegistration.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "VRMInterchangeSettings"


UVRMInterchangeSettings::UVRMInterchangeSettings()
{
	// helps where it appears in the Settings tree (optional)
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("VRM Interchange");
	// Sensible defaults
	bGenerateSpringBoneData = true;
	bGeneratePostProcessAnimBP = false;
	bAssignPostProcessABP = false;
	bOverwriteExistingSpringAssets = false;
	bOverwriteExistingPostProcessABP = false;
	// New default: prefer reusing existing ABP on re-import
	bReusePostProcessABPOnReimport = true;
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
