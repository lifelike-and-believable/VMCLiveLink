// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include <atomic>

class FSocket;
class FRunnableThread;
class FInternetAddr;

/**
 * A UDP socket read on its own thread (P3.1 step 2). Each packet is handed to the callback on that
 * thread, with the time it was read (FPlatformTime::Seconds). The receive buffer is reused, so
 * reading allocates nothing.
 */
class FVMCUdpReceiver : public FRunnable
{
public:
	/** Called on the receive thread. The data is only valid during the call. */
	using FOnPacket = TFunction<void(TConstArrayView<uint8> Packet, double ArrivalSeconds)>;

	/** Binds and starts the thread. Returns null, with a reason in OutError, if the socket can't be opened. */
	static TUniquePtr<FVMCUdpReceiver> Start(const FString& BindAddress, int32 Port, FOnPacket OnPacket, const FString& ThreadName, FString& OutError);

	/** Stops the thread (waits for it) and closes the socket. */
	virtual ~FVMCUdpReceiver() override;

	// FRunnable
	virtual uint32 Run() override;
	virtual void Stop() override;

	/** The port actually bound (useful when 0 asked for any free port). */
	int32 GetBoundPort() const { return BoundPort; }

private:
	FVMCUdpReceiver() = default;

	FSocket* Socket = nullptr;
	FRunnableThread* Thread = nullptr;
	TSharedPtr<FInternetAddr> Sender;
	TArray<uint8> Buffer;
	FOnPacket OnPacket;
	int32 BoundPort = 0;
	std::atomic<bool> bStopping { false };
};
