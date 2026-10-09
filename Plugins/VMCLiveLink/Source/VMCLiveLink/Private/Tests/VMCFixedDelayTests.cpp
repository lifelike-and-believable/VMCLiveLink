// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// S-1 (O3b): Fixed Live Link Delay turns Live Link's smoothing off, and back on as it was.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/IConsoleManager.h"
#include "VMCLiveLinkSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCFixedDelayTest, "VMC.Settings.FixedLiveLinkDelay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCFixedDelayTest::RunTest(const FString& Parameters)
{
	IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("LiveLink.TimedDataInput.NumFramesForSmoothOffset"));
	if (!TestNotNull(TEXT("Live Link's smoothing variable"), Variable))
	{
		return false;
	}
	// A value from an ini file or the console outranks the setting (project-setting priority).
	if ((Variable->GetFlags() & ECVF_SetByMask) > ECVF_SetByProjectSetting)
	{
		AddInfo(TEXT("The variable is set at a higher priority in this project; the setting can't change it."));
		return true;
	}
	UVMCLiveLinkSettings* Settings = GetMutableDefault<UVMCLiveLinkSettings>();
	const bool bWasOn = Settings->bFixedLiveLinkDelay;
	ON_SCOPE_EXIT { Settings->bFixedLiveLinkDelay = bWasOn; Settings->ApplyLiveLinkSmoothing(); };

	// From off, whatever the project has.
	Settings->bFixedLiveLinkDelay = false;
	Settings->ApplyLiveLinkSmoothing();
	const float Before = Variable->GetFloat();

	Settings->bFixedLiveLinkDelay = true;
	Settings->ApplyLiveLinkSmoothing();
	TestEqual(TEXT("On: smoothing off"), Variable->GetFloat(), 0.f);
	Settings->ApplyLiveLinkSmoothing();
	TestEqual(TEXT("On twice: still off"), Variable->GetFloat(), 0.f);

	Settings->bFixedLiveLinkDelay = false;
	Settings->ApplyLiveLinkSmoothing();
	TestEqual(TEXT("Off: the value from before"), Variable->GetFloat(), Before);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
