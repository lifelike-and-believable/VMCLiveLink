// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMInterchangeEditorModule.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "VRMSpringBonesPostImportPipeline.h"
#include "VRMIKRigPostImportPipeline.h"
#include "VRMLiveLinkPostImportPipeline.h"
#include "VRMMaterialPostImportPipeline.h"
#include "VRMAvatarDescriptionPipeline.h"
#include "VRMImportPipelineRegistration.h"
#include "VRMInterchangeSettings.h"

#define LOCTEXT_NAMESPACE "VRMInterchangeEditor"

IMPLEMENT_MODULE(FVRMInterchangeEditorModule, VRMInterchangeEditor)

void FVRMInterchangeEditorModule::StartupModule()
{
	UVRMSpringBonesPostImportPipeline::StaticClass();
	UVRMIKRigPostImportPipeline::StaticClass();
	UVRMLiveLinkPostImportPipeline::StaticClass();
	UVRMMaterialPostImportPipeline::StaticClass();
	UVRMAvatarDescriptionPipeline::StaticClass();

	// The editor module never edits project settings by itself. Registering the VRM import
	// pipelines is done from Project Settings > Plugins > VRM Interchange, or from the prompt below.
	if (GIsRunning)
	{
		OnPostEngineInit();
	}
	else
	{
		PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FVRMInterchangeEditorModule::OnPostEngineInit);
	}
}

void FVRMInterchangeEditorModule::ShutdownModule()
{
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}
}

void FVRMInterchangeEditorModule::OnPostEngineInit()
{
	if (!GIsEditor || IsRunningCommandlet() || FApp::IsUnattended() || !FSlateApplication::IsInitialized())
	{
		return;
	}

	const UVRMInterchangeSettings* Settings = GetDefault<UVRMInterchangeSettings>();
	if (!Settings || !Settings->bPromptToRegisterImportPipelines || VRMImportPipelineRegistration::IsUpToDate())
	{
		return;
	}

	TSharedRef<TWeakPtr<SNotificationItem>> ItemHolder = MakeShared<TWeakPtr<SNotificationItem>>();
	auto Close = [ItemHolder](SNotificationItem::ECompletionState State)
	{
		if (TSharedPtr<SNotificationItem> Item = ItemHolder->Pin())
		{
			Item->SetCompletionState(State);
			Item->ExpireAndFadeout();
		}
	};

	FNotificationInfo Info(LOCTEXT("RegisterPipelinesPrompt", "The VRM import pipelines are not registered in this project."));
	Info.SubText = LOCTEXT("RegisterPipelinesPromptSub", "Registering adds spring bone, IK Rig, Live Link and material set-up to .vrm imports (Project Settings > Interchange).");
	Info.bFireAndForget = false;
	Info.bUseLargeFont = false;
	Info.ButtonDetails.Add(FNotificationButtonInfo(
		LOCTEXT("RegisterPipelines", "Register"),
		LOCTEXT("RegisterPipelinesTip", "Add the VRM import pipelines to the Interchange project settings and save them."),
		FSimpleDelegate::CreateLambda([Close]()
		{
			VRMImportPipelineRegistration::Apply();
			Close(SNotificationItem::CS_Success);
		}),
		SNotificationItem::CS_None));
	Info.ButtonDetails.Add(FNotificationButtonInfo(
		LOCTEXT("NotNow", "Not now"),
		LOCTEXT("NotNowTip", "Ask again next time the editor starts."),
		FSimpleDelegate::CreateLambda([Close]() { Close(SNotificationItem::CS_None); }),
		SNotificationItem::CS_None));
	Info.ButtonDetails.Add(FNotificationButtonInfo(
		LOCTEXT("DontAskAgain", "Don't ask again"),
		LOCTEXT("DontAskAgainTip", "Stop asking. You can still register from Project Settings > Plugins > VRM Interchange."),
		FSimpleDelegate::CreateLambda([Close]()
		{
			UVRMInterchangeSettings* MutableSettings = GetMutableDefault<UVRMInterchangeSettings>();
			MutableSettings->bPromptToRegisterImportPipelines = false;
			MutableSettings->TryUpdateDefaultConfigFile();
			Close(SNotificationItem::CS_None);
		}),
		SNotificationItem::CS_None));

	*ItemHolder = FSlateNotificationManager::Get().AddNotification(Info);
}


#undef LOCTEXT_NAMESPACE
