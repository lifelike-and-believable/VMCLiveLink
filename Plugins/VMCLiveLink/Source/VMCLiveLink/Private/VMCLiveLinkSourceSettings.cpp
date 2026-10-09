// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VMCLiveLinkSourceSettings.h"

UVMCLiveLinkSourceSettings::UVMCLiveLinkSourceSettings()
{
	BufferSettings.EngineTimeOffset = 0.03f;
	BufferSettings.MaxNumberOfFrameToBuffered = 30;
}

FVMCConnectionSettings UVMCLiveLinkSourceSettings::ToConnectionSettings() const
{
	FVMCConnectionSettings Out;
	Out.Port = Port;
	Out.BindAddress = BindAddress;
	Out.SubjectName = SubjectName;
	Out.bUnityToUE = bUnityToUE;
	Out.bMetersToCm = bMetersToCm;
	Out.YawOffsetDeg = YawOffsetDeg;
	Out.bZeroMissingCurves = bZeroMissingCurves;
	Out.bPreferIncomingTranslations = bPreferIncomingTranslations;
	Out.bReceiveThread = bReceiveThread;
	for (const FString& Sender : AllowedSenders)
	{
		const FString Trimmed = Sender.TrimStartAndEnd();
		if (!Trimmed.IsEmpty())
		{
			Out.AllowedSenders.AddUnique(Trimmed); // Validate reports entries that aren't addresses
		}
	}
	Out.bLockToFirstSender = bLockToFirstSender;
	Out.bDeviceSubjects = bDeviceSubjects;
	Out.bCameraSubject = bCameraSubject;
	Out.bSteadyFrameTimes = bSteadyFrameTimes;
	return Out;
}

void UVMCLiveLinkSourceSettings::FromConnectionSettings(const FVMCConnectionSettings& In)
{
	Port = In.Port;
	BindAddress = In.BindAddress;
	SubjectName = In.SubjectName;
	bUnityToUE = In.bUnityToUE;
	bMetersToCm = In.bMetersToCm;
	YawOffsetDeg = In.YawOffsetDeg;
	bZeroMissingCurves = In.bZeroMissingCurves;
	bPreferIncomingTranslations = In.bPreferIncomingTranslations;
	bReceiveThread = In.bReceiveThread;
	AllowedSenders = In.AllowedSenders;
	bLockToFirstSender = In.bLockToFirstSender;
	bDeviceSubjects = In.bDeviceSubjects;
	bCameraSubject = In.bCameraSubject;
	bSteadyFrameTimes = In.bSteadyFrameTimes;
	// Presets recreate the source from the connection string, so keep it in step.
	ConnectionString = In.ToString();
}
