// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VMCLiveLinkSourceSettings.h"

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
	// Presets recreate the source from the connection string, so keep it in step.
	ConnectionString = In.ToString();
}
