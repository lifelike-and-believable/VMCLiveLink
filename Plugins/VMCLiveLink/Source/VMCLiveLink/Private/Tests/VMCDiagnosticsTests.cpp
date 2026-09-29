// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// P6.1: the source's status text, VMC.Stats and the creation panel's port check.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Common/UdpSocketBuilder.h"
#include "IPAddress.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "VMCLiveLinkSource.h"
#include "VMCSourceDiagnostics.h"
#include "VMCUdpReceiver.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCDiagnosticsStatusTest, "VMC.Diagnostics.Status",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCDiagnosticsStatusTest::RunTest(const FString& Parameters)
{
	auto Status = [](const FVMCSourceStatusInputs& In) { return VMCDiagnostics::FormatStatus(In).ToString(); };

	FVMCSourceStatusInputs In;
	In.Port = 39539;
	In.bListening = true;
	TestEqual(TEXT("Listening, any address"), Status(In), FString(TEXT("Listening on :39539, waiting for data")));
	In.BindAddress = TEXT("192.168.1.5");
	TestEqual(TEXT("Listening, one address"), Status(In), FString(TEXT("Listening on 192.168.1.5:39539, waiting for data")));

	In.bListening = false;
	TestTrue(TEXT("Can't listen on one address"), Status(In).StartsWith(TEXT("Can't listen on 192.168.1.5:39539")));
	In.BindAddress = TEXT("0.0.0.0");
	TestEqual(TEXT("Port in use"), Status(In), FString(TEXT("Port 39539 in use")));

	In.bListening = true;
	In.bReceivedFrame = true;
	In.Sender = TEXT("192.168.1.20");
	In.MeanFrameInterval = 1.0 / 60.0;
	In.Jitter = 0.0004;
	In.SecondsSinceLastFrame = 0.01;
	TestEqual(TEXT("Receiving"), Status(In), FString(TEXT("Receiving 60.0 fps from 192.168.1.20, jitter 0.4 ms (receive thread)")));

	In.bReceiveThread = false;
	In.SenderState = TEXT("calibrating");
	In.bLockedToSender = true;
	In.IgnoredSenders = 2;
	TestEqual(TEXT("Receiving, with notes"), Status(In),
		FString(TEXT("Receiving 60.0 fps from 192.168.1.20, jitter 0.4 ms (game thread); sender: calibrating; locked to this sender; ignoring 2 other senders")));

	In.SenderState.Reset();
	In.bLockedToSender = false;
	In.IgnoredSenders = 0;
	In.SecondsSinceLastFrame = 5.5;
	TestEqual(TEXT("No data"), Status(In), FString(TEXT("No data for 5 s (last from 192.168.1.20)")));

	In.bValid = false;
	TestEqual(TEXT("Stopped"), Status(In), FString(TEXT("Stopped")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCDiagnosticsStatsTest, "VMC.Diagnostics.Stats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCDiagnosticsStatsTest::RunTest(const FString& Parameters)
{
	using VMCProtocol::EAddress;
	FVMCMessageStats Stats;
	Stats.Reset(100.0);
	for (int32 i = 0; i < 20; ++i)
	{
		Stats.CountPacket();
		Stats.CountMessage(EAddress::BonePos);
		Stats.CountMessage(EAddress::BonePos);
		Stats.CountMessage(EAddress::BlendApply);
	}
	Stats.CountUnknown(TEXT("/VMC/Ext/Set/Period"));
	Stats.CountUnknown(TEXT("/VMC/Ext/Set/Period"));
	for (int32 i = 0; i < FVMCMessageStats::MaxUnknownAddresses + 3; ++i)
	{
		Stats.CountUnknown(FString::Printf(TEXT("/Other/%02d"), i));
	}

	// 2 seconds after the reset
	const FString First = Stats.Report(102.0);
	AddInfo(First);
	TestTrue(TEXT("Packets per second"), First.Contains(TEXT("packets: 10.0/s (total 20)")));
	TestTrue(TEXT("Bone/Pos per second"), First.Contains(TEXT("/VMC/Ext/Bone/Pos")) && First.Contains(TEXT("20.0/s (total 40)")));
	TestTrue(TEXT("Blend/Apply listed"), First.Contains(TEXT("/VMC/Ext/Blend/Apply")));
	TestFalse(TEXT("Addresses never received aren't listed"), First.Contains(TEXT("/VMC/Ext/Cam")));
	TestTrue(TEXT("Unknown address listed with its count"), First.Contains(TEXT("/VMC/Ext/Set/Period")) && First.Contains(TEXT("1.0/s (total 2)")));
	// One named address slot went to /VMC/Ext/Set/Period, so 4 of the /Other ones overflow.
	TestTrue(TEXT("Overflow counted"), First.Contains(TEXT("(4 more messages to addresses beyond the first 32)")));

	// The next report covers only what arrived since the first.
	Stats.CountPacket();
	Stats.CountMessage(EAddress::BonePos);
	const FString Second = Stats.Report(103.0);
	TestTrue(TEXT("Rates since the last report"), Second.Contains(TEXT("packets: 1.0/s (total 21)")));
	TestTrue(TEXT("Bone/Pos since the last report"), Second.Contains(TEXT("1.0/s (total 41)")));
	TestTrue(TEXT("Unknown address rate falls to 0"), Second.Contains(TEXT("0.0/s (total 2)")));

	Stats.Reset(200.0);
	const FString Cleared = Stats.Report(201.0);
	TestTrue(TEXT("Reset clears the counts"), Cleared.Contains(TEXT("packets: 0.0/s (total 0)")) && Cleared.Contains(TEXT("unknown addresses: none")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCDiagnosticsPortCheckTest, "VMC.Diagnostics.PortCheck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCDiagnosticsPortCheckTest::RunTest(const FString& Parameters)
{
	FString Error;
	TUniquePtr<FVMCUdpReceiver> Receiver = FVMCUdpReceiver::Start(TEXT("127.0.0.1"), 0,
		[](TConstArrayView<uint8>, double, const FInternetAddr&) {}, TEXT("VMC port check test"), Error);
	if (!TestTrue(FString::Printf(TEXT("Receiver starts (%s)"), *Error), Receiver.IsValid())) return false;
	const int32 Taken = Receiver->GetBoundPort();

	TestFalse(TEXT("A port in use can't be bound"), FVMCUdpReceiver::CanBind(TEXT("127.0.0.1"), Taken, Error));
	TestTrue(TEXT("The reason names the port"), Error.Contains(FString::FromInt(Taken)));
	TestFalse(TEXT("Not an address"), FVMCUdpReceiver::CanBind(TEXT("not an address"), 39539, Error));
	TestTrue(TEXT("Any free port can be bound"), FVMCUdpReceiver::CanBind(TEXT("127.0.0.1"), 0, Error));

	Receiver.Reset();
	TestTrue(TEXT("The port is free again once the receiver closes"), FVMCUdpReceiver::CanBind(TEXT("127.0.0.1"), Taken, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCDiagnosticsSourceTest, "VMC.Diagnostics.Source",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCDiagnosticsSourceTest::RunTest(const FString& Parameters)
{
	// A free port: bind any, note it, let it go.
	int32 Port = 0;
	{
		FString Error;
		TUniquePtr<FVMCUdpReceiver> Probe = FVMCUdpReceiver::Start(TEXT("127.0.0.1"), 0,
			[](TConstArrayView<uint8>, double, const FInternetAddr&) {}, TEXT("VMC port probe"), Error);
		if (!TestTrue(FString::Printf(TEXT("Probe starts (%s)"), *Error), Probe.IsValid())) return false;
		Port = Probe->GetBoundPort();
	}

	FVMCConnectionSettings Settings;
	Settings.BindAddress = TEXT("127.0.0.1");
	Settings.Port = Port;
	Settings.bReceiveThread = true;

	// No Live Link client: the source receives and counts, but can't push frames (and doesn't need
	// to for what's checked here).
	const TSharedRef<FVMCLiveLinkSource> Source = MakeShared<FVMCLiveLinkSource>(Settings, TEXT("VMC diagnostics test"));
	Source->ReceiveClient(nullptr, FGuid::NewGuid());
	TestEqual(TEXT("Listening"), Source->GetSourceStatus().ToString(), FString::Printf(TEXT("Listening on 127.0.0.1:%d, waiting for data"), Port));
	TestEqual(TEXT("No sender yet"), Source->GetSourceMachineName().ToString(), FString(TEXT("No sender yet")));

	// A second source on the same port says so.
	AddExpectedError(FString::Printf(TEXT("can't bind 127.0.0.1:%d"), Port), EAutomationExpectedErrorFlags::Contains, 1);
	const TSharedRef<FVMCLiveLinkSource> Second = MakeShared<FVMCLiveLinkSource>(Settings, TEXT("VMC diagnostics test 2"));
	Second->ReceiveClient(nullptr, FGuid::NewGuid());
	TestTrue(TEXT("Second source can't listen"), Second->GetSourceStatus().ToString().StartsWith(FString::Printf(TEXT("Can't listen on 127.0.0.1:%d"), Port)));
	Second->RequestSourceShutdown();

	// Send one bone and an address the plugin doesn't use.
	auto Pad = [](TArray<uint8>& Out) { while (Out.Num() % 4 != 0) Out.Add(0); };
	auto AddString = [&Pad](TArray<uint8>& Out, const char* Text) { Out.Append(reinterpret_cast<const uint8*>(Text), FCStringAnsi::Strlen(Text)); Out.Add(0); Pad(Out); };
	auto AddFloat = [](TArray<uint8>& Out, float Value) { uint32 Bits; FMemory::Memcpy(&Bits, &Value, 4); for (int32 Shift = 24; Shift >= 0; Shift -= 8) Out.Add(uint8(Bits >> Shift)); };
	TArray<uint8> BonePacket;
	AddString(BonePacket, "/VMC/Ext/Bone/Pos");
	AddString(BonePacket, ",sfffffff");
	AddString(BonePacket, "Hips");
	for (float Value : { 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f }) AddFloat(BonePacket, Value);
	TArray<uint8> UnknownPacket;
	AddString(UnknownPacket, "/VMC/Ext/Set/Period");
	AddString(UnknownPacket, ",");

	FSocket* Sender = FUdpSocketBuilder(TEXT("VMC diagnostics send")).AsNonBlocking().Build();
	if (!TestNotNull(TEXT("Sender socket"), Sender)) return false;
	const TSharedRef<FInternetAddr> To = FIPv4Endpoint(FIPv4Address(127, 0, 0, 1), uint16(Port)).ToInternetAddr();
	int32 Sent = 0;
	for (int32 i = 0; i < 5; ++i)
	{
		Sender->SendTo(BonePacket.GetData(), BonePacket.Num(), Sent, *To);
	}
	Sender->SendTo(UnknownPacket.GetData(), UnknownPacket.Num(), Sent, *To);

	FString Report;
	const double Deadline = FPlatformTime::Seconds() + 3.0;
	while (FPlatformTime::Seconds() < Deadline)
	{
		if (Source->GetSourceMachineName().ToString() == TEXT("127.0.0.1"))
		{
			Report = Source->GetStatsReport();
			if (Report.Contains(TEXT("/VMC/Ext/Set/Period")) && Report.Contains(TEXT("(total 5)")))
			{
				break;
			}
		}
		FPlatformProcess::Sleep(0.01f);
	}
	Sender->Close();
	ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Sender);
	AddInfo(Report);

	TestEqual(TEXT("Machine name is the sender"), Source->GetSourceMachineName().ToString(), FString(TEXT("127.0.0.1")));
	TestTrue(TEXT("VMC.Stats counts Bone/Pos"), Report.Contains(TEXT("/VMC/Ext/Bone/Pos")) && Report.Contains(TEXT("(total 5)")));
	TestTrue(TEXT("VMC.Stats lists the unknown address"), Report.Contains(TEXT("/VMC/Ext/Set/Period")));
	TestTrue(TEXT("VMC.Stats counts packets"), Report.Contains(TEXT("(total 6)")));
	Source->RequestSourceShutdown();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
