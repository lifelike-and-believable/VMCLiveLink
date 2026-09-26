// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "VMCOscParser.h"
#include "VMCFrameAssembler.h"
#include "VMCConnectionSettings.h"
#include "VMCUdpReceiver.h"
#include "Common/UdpSocketBuilder.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Math/RandomStream.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

namespace VMCOscParserTests
{
	/** Writes OSC 1.0, as a sender would. */
	struct FWriter
	{
		TArray<uint8> Bytes;

		void Pad() { while (Bytes.Num() % 4) Bytes.Add(0); }
		void U32(uint32 V) { Bytes.Add(uint8(V >> 24)); Bytes.Add(uint8(V >> 16)); Bytes.Add(uint8(V >> 8)); Bytes.Add(uint8(V)); }
		void Str(const char* S) { while (*S) Bytes.Add(uint8(*S++)); Bytes.Add(0); Pad(); }
		void Raw(TConstArrayView<uint8> B) { Bytes.Append(B.GetData(), B.Num()); }
	};

	struct FArgSpec { char Tag; float F = 0.f; int32 I = 0; const char* S = nullptr; };

	TArray<uint8> Message(const char* Address, std::initializer_list<FArgSpec> Args)
	{
		FWriter W;
		W.Str(Address);
		TArray<char> Tags = { ',' };
		for (const FArgSpec& A : Args) Tags.Add(A.Tag);
		Tags.Add(0);
		W.Str(Tags.GetData());
		for (const FArgSpec& A : Args)
		{
			switch (A.Tag)
			{
			case 'f': { uint32 U; FMemory::Memcpy(&U, &A.F, 4); W.U32(U); break; }
			case 'i': W.U32(uint32(A.I)); break;
			case 's': W.Str(A.S); break;
			case 'b': W.U32(3); W.Bytes.Append({ 1, 2, 3 }); W.Pad(); break;
			case 'd': case 'h': case 't': W.U32(0); W.U32(0); break;
			default: break; // T F N I: no data
			}
		}
		return W.Bytes;
	}

	TArray<uint8> Bundle(std::initializer_list<TArray<uint8>> Elements)
	{
		FWriter W;
		W.Str("#bundle");
		W.U32(0); W.U32(1); // time tag "immediately"
		for (const TArray<uint8>& E : Elements)
		{
			W.U32(uint32(E.Num()));
			W.Raw(E);
		}
		return W.Bytes;
	}

	struct FSeen
	{
		FString Address;
		TArray<VMCProtocol::FArg::EType> Types;
		TArray<float> Numbers;
		TArray<FName> Names;
	};

	bool Parse(TConstArrayView<uint8> Packet, TArray<FSeen>& Out)
	{
		Out.Reset();
		return VMCOscParser::ParsePacket(Packet, [&Out](FAnsiStringView Address, TConstArrayView<VMCProtocol::FArg> Args)
		{
			FSeen& S = Out.AddDefaulted_GetRef();
			S.Address = FString(Address);
			for (const VMCProtocol::FArg& A : Args)
			{
				S.Types.Add(A.Type);
				S.Numbers.Add(A.Number);
				S.Names.Add(A.Type == VMCProtocol::FArg::EType::String ? A.ToName() : NAME_None);
			}
		});
	}

	/** Reads a .vmcrec capture (scripts/vmc_sender.py): <float64 seconds><uint32 length><packet>, little-endian. */
	bool ReadCapture(const FString& FileName, TArray<TArray<uint8>>& OutPackets)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VMCLiveLink"));
		if (!Plugin)
		{
			return false;
		}
		TArray<uint8> File;
		if (!FFileHelper::LoadFileToArray(File, *FPaths::Combine(Plugin->GetBaseDir(), TEXT("Tests/Captures"), FileName)))
		{
			return false;
		}
		int32 Pos = 0;
		while (Pos + 12 <= File.Num())
		{
			uint32 Len = 0;
			FMemory::Memcpy(&Len, File.GetData() + Pos + 8, 4); // little-endian, as the platforms we build for
			Pos += 12;
			if (Pos + int32(Len) > File.Num())
			{
				return false;
			}
			OutPackets.Emplace(File.GetData() + Pos, int32(Len));
			Pos += int32(Len);
		}
		return OutPackets.Num() > 0 && Pos == File.Num();
	}

	/** Runs a capture through the receive-thread path: parser, then the assembler. One entry per Blend/Apply. */
	void Replay(const TArray<TArray<uint8>>& Packets, FVMCFrameAssembler& A, TArray<FLiveLinkFrameDataStruct>& OutFrames, int32& OutMalformed)
	{
		const FVMCConnectionSettings Settings; // defaults: Unity to UE, metres to cm
		OutMalformed = 0;
		for (const TArray<uint8>& Packet : Packets)
		{
			VMCOscParser::ParsePacket(Packet, [&](FAnsiStringView Address, TConstArrayView<VMCProtocol::FArg> Args)
			{
				const FVMCFrameAssembler::FMessageResult R = A.ApplyMessage(VMCProtocol::ClassifyAddress(Address), Args, Settings);
				OutMalformed += R.bMalformed ? 1 : 0;
				if (R.bApply)
				{
					OutFrames.Add(A.MakeFrameData(FVMCFrameAssembler::FFrameOptions()));
					A.EndFrame(Settings.bZeroMissingCurves);
				}
			});
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCOscParserMessagesTest, "VMC.OscParser.Messages",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCOscParserMessagesTest::RunTest(const FString& Parameters)
{
	using namespace VMCOscParserTests;
	using EType = VMCProtocol::FArg::EType;
	TArray<FSeen> Seen;

	// Every argument type, in one message.
	const TArray<uint8> All = Message("/test", { {'i', 0, -7}, {'f', 2.5f}, {'s', 0, 0, "Hips"}, {'b'}, {'T'}, {'d'}, {'h'}, {'t'}, {'N'}, {'f', -1.f} });
	TestTrue(TEXT("All types parse"), Parse(All, Seen));
	if (TestEqual(TEXT("One message"), Seen.Num(), 1) && TestEqual(TEXT("Ten arguments"), Seen[0].Types.Num(), 10))
	{
		TestEqual(TEXT("address"), Seen[0].Address, FString(TEXT("/test")));
		TestEqual(TEXT("int"), Seen[0].Numbers[0], -7.f);
		TestEqual(TEXT("float"), Seen[0].Numbers[1], 2.5f);
		TestEqual(TEXT("string"), Seen[0].Names[2], FName(TEXT("Hips")));
		TestTrue(TEXT("blob read past"), Seen[0].Types[3] == EType::Other);
		TestEqual(TEXT("last float after the 8-byte types"), Seen[0].Numbers[9], -1.f);
	}

	// A message with no type tag string (old OSC) has no arguments.
	FWriter NoTags;
	NoTags.Str("/VMC/Ext/Blend/Apply");
	TestTrue(TEXT("No type tags parses"), Parse(NoTags.Bytes, Seen));
	TestTrue(TEXT("... with no arguments"), Seen.Num() == 1 && Seen[0].Types.Num() == 0);

	// Bundles, nested, keep message order.
	const TArray<uint8> Nested = Bundle({ Message("/a", {}), Bundle({ Message("/b", {}), Message("/c", {{'i', 0, 1}}) }), Message("/d", {}) });
	TestTrue(TEXT("Nested bundle parses"), Parse(Nested, Seen));
	TestTrue(TEXT("Four messages in order"), Seen.Num() == 4 && Seen[0].Address == TEXT("/a") && Seen[1].Address == TEXT("/b") && Seen[2].Address == TEXT("/c") && Seen[3].Address == TEXT("/d"));

	// An unknown type tag spoils its message only.
	const TArray<uint8> Bad = Bundle({ Message("/a", {}), Message("/bad", {{'?'}}), Message("/c", {}) });
	TestFalse(TEXT("A bad element is reported"), Parse(Bad, Seen));
	TestTrue(TEXT("... and the others are delivered"), Seen.Num() == 2 && Seen[0].Address == TEXT("/a") && Seen[1].Address == TEXT("/c"));

	// UTF-8 strings (Japanese blend shape names) become the right names.
	const char Utf8A[] = { char(0xE3), char(0x81), char(0x82), 0 }; // U+3042 HIRAGANA LETTER A
	TestTrue(TEXT("UTF-8 string parses"), Parse(Message("/VMC/Ext/Blend/Val", { {'s', 0, 0, Utf8A}, {'f', 1.f} }), Seen));
	TestTrue(TEXT("... decoded as UTF-8"), Seen.Num() == 1 && Seen[0].Names[0] == FName(*FString::Chr(TCHAR(0x3042))));

	// Things that aren't OSC.
	for (const char* Text : { "", "not osc", "/no-terminator" })
	{
		TArray<uint8> Junk;
		for (const char* C = Text; *C; ++C) Junk.Add(uint8(*C));
		TestFalse(FString::Printf(TEXT("'%s' rejected"), UTF8_TO_TCHAR(Text)), Parse(Junk, Seen));
		TestEqual(TEXT("... with nothing delivered"), Seen.Num(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCOscParserMalformedTest, "VMC.OscParser.Malformed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCOscParserMalformedTest::RunTest(const FString& Parameters)
{
	using namespace VMCOscParserTests;
	TArray<FSeen> Seen;
	const TArray<uint8> Good = Bundle({
		Message("/VMC/Ext/Root/Pos", { {'s', 0, 0, "root"}, {'f', 1}, {'f', 2}, {'f', 3}, {'f', 0}, {'f', 0}, {'f', 0}, {'f', 1} }),
		Bundle({ Message("/VMC/Ext/Blend/Val", { {'s', 0, 0, "A"}, {'f', 0.5f} }) }),
		Message("/VMC/Ext/Blend/Apply", {}) });
	TestTrue(TEXT("The reference packet parses"), Parse(Good, Seen));
	const int32 Full = Seen.Num();

	// Every truncation: never more messages than the whole packet, never a crash.
	for (int32 Len = 0; Len < Good.Num(); ++Len)
	{
		Parse(TConstArrayView<uint8>(Good.GetData(), Len), Seen);
		if (Seen.Num() > Full)
		{
			AddError(FString::Printf(TEXT("Truncated to %d bytes: %d messages"), Len, Seen.Num()));
		}
	}

	// Element sizes that lie (too big, negative), and random corruption.
	TArray<uint8> Lying = Good;
	Lying[16] = 0x7F; // first element size: enormous
	TestFalse(TEXT("An oversized element is rejected"), Parse(Lying, Seen));
	Lying[16] = 0xFF; // negative
	TestFalse(TEXT("A negative element size is rejected"), Parse(Lying, Seen));

	FRandomStream Random(7);
	for (int32 i = 0; i < 2000; ++i)
	{
		TArray<uint8> Corrupt = Good;
		for (int32 n = Random.RandRange(1, 8); n > 0; --n)
		{
			Corrupt[Random.RandRange(0, Corrupt.Num() - 1)] = uint8(Random.RandRange(0, 255));
		}
		Parse(Corrupt, Seen); // must not crash or read out of bounds
	}
	for (int32 i = 0; i < 500; ++i)
	{
		TArray<uint8> Noise;
		Noise.SetNumUninitialized(Random.RandRange(0, 256));
		for (uint8& B : Noise) B = uint8(Random.RandRange(0, 255));
		if (i % 2 == 0 && Noise.Num() >= 8) FMemory::Memcpy(Noise.GetData(), "#bundle", 8);
		Parse(Noise, Seen);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCReplayCaptureTest, "VMC.Replay.SyntheticCapture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCReplayCaptureTest::RunTest(const FString& Parameters)
{
	using namespace VMCOscParserTests;
	// Expected values computed from scripts/vmc_sender.py's generator (float32, then Unity to UE).
	{
		TArray<TArray<uint8>> Packets;
		if (!TestTrue(TEXT("synthetic.vmcrec loads"), ReadCapture(TEXT("synthetic.vmcrec"), Packets))) return false;
		FVMCFrameAssembler A;
		TArray<FLiveLinkFrameDataStruct> Frames;
		int32 Malformed = 0;
		Replay(Packets, A, Frames, Malformed);
		TestEqual(TEXT("No malformed messages"), Malformed, 0);
		if (!TestEqual(TEXT("Ten frames"), Frames.Num(), 10)) return false;

		const FLiveLinkAnimationFrameData& Last = *Frames.Last().Cast<FLiveLinkAnimationFrameData>();
		const TConstArrayView<FName> Bones = A.GetBoneNames();
		auto Bone = [&](const TCHAR* Name) { return Last.Transforms[Bones.IndexOfByKey(FName(Name))]; };
		TestTrue(TEXT("Root position"), Last.Transforms[0].GetTranslation().Equals(FVector(-99.7189, 7.493, 0.0), 1.0e-3));
		TestTrue(TEXT("Root rotation"), Last.Transforms[0].GetRotation().Equals(FQuat(0.0, 0.0, -0.037491, 0.999297), 1.0e-5));
		TestTrue(TEXT("Hips translation"), Bone(TEXT("Hips")).GetTranslation().Equals(FVector(0, 0, 95), 1.0e-3));
		TestTrue(TEXT("Arm wave"), Bone(TEXT("LeftUpperArm")).GetRotation().Equals(FQuat(0.0, 0.117933, 0.0, 0.993022), 1.0e-5));
		TestTrue(TEXT("Head nod"), Bone(TEXT("Head")).GetRotation().Equals(FQuat(-0.029061, 0.0, 0.0, 0.999578), 1.0e-5));
		const int32 Joy = A.GetCurveNames().IndexOfByKey(FName(TEXT("Joy")));
		TestTrue(TEXT("Joy curve"), Joy != INDEX_NONE && FMath::IsNearlyEqual(Last.PropertyValues[Joy], 0.584482f, 1.0e-5f));
		TestEqual(TEXT("Thirteen curves"), A.GetCurveNames().Num(), 13);
		TestTrue(TEXT("Sender time from /VMC/Ext/T"), A.GetSenderTime().IsSet() && FMath::IsNearlyEqual(*A.GetSenderTime(), 0.15f, 1.0e-6f));
	}
	// One message per packet, the legacy 7-float root, and blend shapes sent every other frame.
	{
		TArray<TArray<uint8>> Packets;
		if (!TestTrue(TEXT("synthetic_unbundled.vmcrec loads"), ReadCapture(TEXT("synthetic_unbundled.vmcrec"), Packets))) return false;
		FVMCFrameAssembler A;
		TArray<FLiveLinkFrameDataStruct> Frames;
		int32 Malformed = 0;
		Replay(Packets, A, Frames, Malformed);
		TestEqual(TEXT("No malformed messages"), Malformed, 0);
		if (!TestEqual(TEXT("Four frames"), Frames.Num(), 4)) return false;
		const FLiveLinkAnimationFrameData& Last = *Frames.Last().Cast<FLiveLinkAnimationFrameData>();
		TestTrue(TEXT("Legacy root position"), Last.Transforms[0].GetTranslation().Equals(FVector(-99.9687, 2.4997, 0.0), 1.0e-3));
		const int32 CurveA = A.GetCurveNames().IndexOfByKey(FName(TEXT("A")));
		const int32 Neutral = A.GetCurveNames().IndexOfByKey(FName(TEXT("Neutral")));
		TestTrue(TEXT("'A' sent this frame"), CurveA != INDEX_NONE && FMath::IsNearlyEqual(Last.PropertyValues[CurveA], 0.942152f, 1.0e-5f));
		TestTrue(TEXT("'Neutral' held from the frame before"), Neutral != INDEX_NONE && FMath::IsNearlyEqual(Last.PropertyValues[Neutral], 0.528318f, 1.0e-5f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCUdpReceiverLoopbackTest, "VMC.Receiver.Loopback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCUdpReceiverLoopbackTest::RunTest(const FString& Parameters)
{
	using namespace VMCOscParserTests;
	// Packets sent to 127.0.0.1 arrive on the receive thread, whole, in order, with rising arrival times.
	FCriticalSection Lock;
	TArray<TArray<uint8>> Received;
	TArray<double> Times;
	FString Error;
	TUniquePtr<FVMCUdpReceiver> Receiver = FVMCUdpReceiver::Start(TEXT("127.0.0.1"), 0,
		[&](TConstArrayView<uint8> Packet, double Arrival)
		{
			FScopeLock ScopeLock(&Lock);
			Received.Emplace(Packet.GetData(), Packet.Num());
			Times.Add(Arrival);
		},
		TEXT("VMC receive test"), Error);
	if (!TestTrue(FString::Printf(TEXT("Receiver starts (%s)"), *Error), Receiver.IsValid())) return false;
	TestNotEqual(TEXT("Bound to a real port"), Receiver->GetBoundPort(), 0);

	FSocket* Sender = FUdpSocketBuilder(TEXT("VMC send test")).AsNonBlocking().Build();
	if (!TestNotNull(TEXT("Sender socket"), Sender)) return false;
	const TSharedRef<FInternetAddr> To = FIPv4Endpoint(FIPv4Address(127, 0, 0, 1), uint16(Receiver->GetBoundPort())).ToInternetAddr();

	constexpr int32 Count = 20;
	TArray<TArray<uint8>> Sent;
	for (int32 i = 0; i < Count; ++i)
	{
		Sent.Add(Bundle({ Message("/VMC/Ext/Blend/Val", { {'s', 0, 0, "A"}, {'f', float(i)} }), Message("/VMC/Ext/Blend/Apply", {}) }));
		int32 BytesSent = 0;
		Sender->SendTo(Sent.Last().GetData(), Sent.Last().Num(), BytesSent, *To);
		FPlatformProcess::Sleep(0.002f);
	}
	const double Deadline = FPlatformTime::Seconds() + 3.0;
	while (FPlatformTime::Seconds() < Deadline)
	{
		{
			FScopeLock ScopeLock(&Lock);
			if (Received.Num() >= Count) break;
		}
		FPlatformProcess::Sleep(0.01f);
	}
	Receiver.Reset(); // stops the thread before the checks read what it wrote
	Sender->Close();
	ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Sender);

	if (!TestEqual(TEXT("Every packet arrived"), Received.Num(), Count)) return false;
	for (int32 i = 0; i < Count; ++i)
	{
		TestTrue(FString::Printf(TEXT("Packet %d intact and in order"), i), Received[i] == Sent[i]);
		if (i > 0) TestTrue(TEXT("Arrival times rise"), Times[i] >= Times[i - 1]);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
