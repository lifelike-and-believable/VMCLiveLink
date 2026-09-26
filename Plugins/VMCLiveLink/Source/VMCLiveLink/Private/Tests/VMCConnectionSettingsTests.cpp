// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "VMCConnectionSettings.h"
#include "VMCLiveLinkSourceSettings.h"
#include "Math/RandomStream.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCConnectionSettingsParseTest, "VMC.ConnectionSettings.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCConnectionSettingsParseTest::RunTest(const FString& Parameters)
{
	FVMCConnectionSettings S;
	TArray<FString> Errors;

	TestTrue(TEXT("Empty string parses"), FVMCConnectionSettings::FromString(TEXT(""), S, &Errors));
	TestTrue(TEXT("... to the defaults"), S == FVMCConnectionSettings());

	// A string saved by an earlier version (presets).
	TestTrue(TEXT("Legacy string parses"), FVMCConnectionSettings::FromString(TEXT("port=39540;unity2ue=0;meters2cm=1;subject=Alice"), S, &Errors));
	TestEqual(TEXT("port"), S.Port, 39540);
	TestFalse(TEXT("unity2ue"), S.bUnityToUE);
	TestTrue(TEXT("meters2cm"), S.bMetersToCm);
	TestEqual(TEXT("subject"), S.SubjectName, FName(TEXT("Alice")));
	TestEqual(TEXT("Missing keys keep defaults"), S.BindAddress, FString(TEXT("0.0.0.0")));

	// Keys are case-insensitive, whitespace around values is ignored, unknown keys are skipped.
	TestTrue(TEXT("Loose string parses"), FVMCConnectionSettings::FromString(TEXT(" PORT = 40000 ;Unity2UE=false;;foo=bar;zeromissing=on;yaw=-90.5;bind=127.0.0.1"), S, &Errors));
	TestEqual(TEXT("port"), S.Port, 40000);
	TestFalse(TEXT("false"), S.bUnityToUE);
	TestTrue(TEXT("on"), S.bZeroMissingCurves);
	TestEqual(TEXT("yaw"), S.YawOffsetDeg, -90.5f);
	TestEqual(TEXT("bind"), S.BindAddress, FString(TEXT("127.0.0.1")));
	TestEqual(TEXT("No errors so far"), Errors.Num(), 0);

	// Invalid values are reported and keep their defaults; the rest still apply.
	for (const TCHAR* BadPort : { TEXT("0"), TEXT("70000"), TEXT("abc"), TEXT("1.5"), TEXT("-1"), TEXT("") })
	{
		Errors.Reset();
		TestFalse(FString::Printf(TEXT("port '%s' rejected"), BadPort), FVMCConnectionSettings::FromString(FString::Printf(TEXT("port=%s;subject=Bob"), BadPort), S, &Errors));
		TestEqual(FString::Printf(TEXT("port '%s' keeps the default"), BadPort), S.Port, FVMCConnectionSettings::DefaultPort);
		TestEqual(TEXT("the subject still applies"), S.SubjectName, FName(TEXT("Bob")));
		TestEqual(TEXT("one error"), Errors.Num(), 1);
	}
	TestFalse(TEXT("bad bind address"), FVMCConnectionSettings::FromString(TEXT("bind=localhost"), S));
	TestFalse(TEXT("bad boolean"), FVMCConnectionSettings::FromString(TEXT("unity2ue=maybe"), S));
	TestTrue(TEXT("... keeps the default"), S.bUnityToUE);
	TestFalse(TEXT("empty subject"), FVMCConnectionSettings::FromString(TEXT("subject=  "), S));
	TestFalse(TEXT("bad yaw"), FVMCConnectionSettings::FromString(TEXT("yaw=left"), S));

	// Validate.
	FVMCConnectionSettings V;
	TestTrue(TEXT("Defaults are valid"), V.Validate());
	V.Port = 0;
	TestFalse(TEXT("Port 0 is invalid"), V.Validate());
	V = FVMCConnectionSettings();
	V.SubjectName = TEXT("a;b");
	TestFalse(TEXT("A ';' in the subject is invalid"), V.Validate());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCConnectionSettingsRoundTripTest, "VMC.ConnectionSettings.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCConnectionSettingsRoundTripTest::RunTest(const FString& Parameters)
{
	FRandomStream Random(42);
	const TCHAR* Subjects[] = { TEXT("VMC_Subject"), TEXT("Alice"), TEXT("Stage Left 2"), TEXT("Under_Score") };
	const TCHAR* Binds[] = { TEXT("0.0.0.0"), TEXT("127.0.0.1"), TEXT("192.168.1.20") };
	const float Yaws[] = { 0.f, 90.f, -90.5f, 12.25f, 179.75f };
	for (int32 i = 0; i < 50; ++i)
	{
		FVMCConnectionSettings In;
		In.Port = Random.RandRange(1, 65535);
		In.BindAddress = Binds[Random.RandRange(0, UE_ARRAY_COUNT(Binds) - 1)];
		In.SubjectName = Subjects[Random.RandRange(0, UE_ARRAY_COUNT(Subjects) - 1)];
		In.YawOffsetDeg = Yaws[Random.RandRange(0, UE_ARRAY_COUNT(Yaws) - 1)];
		In.bUnityToUE = Random.FRand() < 0.5f;
		In.bMetersToCm = Random.FRand() < 0.5f;
		In.bZeroMissingCurves = Random.FRand() < 0.5f;
		In.bPreferIncomingTranslations = Random.FRand() < 0.5f;
		In.bReceiveThread = Random.FRand() < 0.5f;

		const FString String = In.ToString();
		FVMCConnectionSettings Out;
		TArray<FString> Errors;
		TestTrue(FString::Printf(TEXT("'%s' parses"), *String), FVMCConnectionSettings::FromString(String, Out, &Errors));
		if (!TestTrue(FString::Printf(TEXT("'%s' round-trips"), *String), Out == In))
		{
			return false;
		}

		// And through the Live Link settings object.
		UVMCLiveLinkSourceSettings* Obj = NewObject<UVMCLiveLinkSourceSettings>();
		Obj->FromConnectionSettings(In);
		TestTrue(TEXT("Settings object round-trips"), Obj->ToConnectionSettings() == In);
		TestEqual(TEXT("Settings object keeps the connection string in step"), Obj->ConnectionString, String);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
