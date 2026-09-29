// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "VMCProtocol.h"
#include <atomic>

/**
 * What a VMC source tells the user about itself (P6.1): its status text in the Live Link panel, and
 * the message counts the VMC.Stats console command prints. Kept apart from the source so both can
 * be unit tested.
 */

/** Everything the status text is made from. */
struct FVMCSourceStatusInputs
{
	bool bValid = true;             // false once Live Link has shut the source down
	bool bListening = false;        // the socket (or OSC server) is open
	bool bReceivedFrame = false;    // a frame has been pushed since receiving started
	FString BindAddress = TEXT("0.0.0.0");
	int32 Port = 0;
	bool bReceiveThread = true;
	double SecondsSinceLastFrame = 0.0;
	double MeanFrameInterval = 0.0; // seconds
	double Jitter = 0.0;            // seconds
	FString Sender;                 // the last sender's IP, or empty
	FString SenderState;            // VMCProtocol::DescribeSenderState, or empty
	bool bLockedToSender = false;   // Lock to First Sender has locked on to Sender
	int32 IgnoredSenders = 0;
	int32 DeviceSubjects = 0;
};

namespace VMCDiagnostics
{
	/** Seconds without a frame before the status says "No data". */
	constexpr double NoDataSeconds = 1.0;

	/**
	 * The status text: "Listening on :39539, waiting for data", "Receiving 60.0 fps from
	 * 192.168.1.20, jitter 0.4 ms (receive thread)", "No data for 5 s (last from 192.168.1.20)",
	 * "Port 39539 in use" or "Stopped", with notes about the sender appended.
	 */
	FText FormatStatus(const FVMCSourceStatusInputs& In);

	/** The OSC address, or addresses, a message kind stands for ("/VMC/Ext/Bone/Pos"). */
	const TCHAR* DescribeAddress(VMCProtocol::EAddress Kind);
}

/**
 * Message counts per VMC address, and the addresses the plugin doesn't use, for VMC.Stats. Counted
 * by the thread that builds frames, read by the game thread.
 */
class FVMCMessageStats
{
public:
	static constexpr int32 NumKinds = int32(VMCProtocol::EAddress::Other) + 1;
	/** Unknown addresses remembered by name; any beyond this are counted together. */
	static constexpr int32 MaxUnknownAddresses = 32;

	/** Clears the counts (receiving restarted). Not while messages are being counted. */
	void Reset(double NowSeconds);

	void CountPacket() { Packets.fetch_add(1, std::memory_order_relaxed); }
	void CountMessage(VMCProtocol::EAddress Kind) { Counts[int32(Kind)].fetch_add(1, std::memory_order_relaxed); }
	/** An address ClassifyAddress calls Other. Also counts it as a message. */
	void CountUnknown(const FString& Address);

	/**
	 * Report lines for VMC.Stats: packets and messages per second for each address since the
	 * previous report (or since receiving started), with totals, then the unknown addresses.
	 * Game thread only.
	 */
	FString Report(double NowSeconds);

private:
	std::atomic<uint64> Packets { 0 };
	std::atomic<uint64> Counts[NumKinds] = {};

	FCriticalSection UnknownLock;
	TMap<FString, uint64> Unknown;
	uint64 UnknownOverflow = 0; // unknown addresses beyond MaxUnknownAddresses

	// The previous report (game thread)
	double LastReportSeconds = 0.0;
	uint64 LastPackets = 0;
	uint64 LastCounts[NumKinds] = {};
	TMap<FString, uint64> LastUnknown;
};
