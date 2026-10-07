// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VMCUdpReceiver.h"
#include "Common/UdpSocketBuilder.h"
#include "HAL/PlatformTime.h"
#include "HAL/RunnableThread.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

namespace VMCUdpReceiver
{
	// Checks the address and port and binds a UDP socket there (not reusable: a port another
	// program already has is an error to report, not to share). Null, with OutError, on failure.
	FSocket* BindSocket(const FString& BindAddress, int32 Port, const FString& Description, FString& OutError)
	{
		FIPv4Address Address;
		if (!FIPv4Address::Parse(BindAddress, Address))
		{
			OutError = FString::Printf(TEXT("'%s' is not an IPv4 address"), *BindAddress);
			return nullptr;
		}
		if (Port < 0 || Port > 65535)
		{
			OutError = FString::Printf(TEXT("port %d is out of range"), Port);
			return nullptr;
		}
		if (!ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
		{
			OutError = TEXT("no socket subsystem");
			return nullptr;
		}
		FSocket* Socket = FUdpSocketBuilder(Description)
			.AsNonBlocking()
			.BoundToAddress(Address)
			.BoundToPort(uint16(Port))
			.WithReceiveBufferSize(4 * 1024 * 1024)
			.Build();
		if (!Socket)
		{
			OutError = FString::Printf(TEXT("can't bind %s:%d (address not on this machine, or port in use?)"), *BindAddress, Port);
		}
		return Socket;
	}
}

bool FVMCUdpReceiver::CanBind(const FString& BindAddress, int32 Port, FString& OutError)
{
	FSocket* Socket = VMCUdpReceiver::BindSocket(BindAddress, Port, TEXT("VMC port check"), OutError);
	if (!Socket)
	{
		return false;
	}
	Socket->Close();
	ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
	return true;
}

TUniquePtr<FVMCUdpReceiver> FVMCUdpReceiver::Start(const FString& BindAddress, int32 Port, FOnPacket InOnPacket, const FString& ThreadName, FString& OutError)
{
	FSocket* NewSocket = VMCUdpReceiver::BindSocket(BindAddress, Port, ThreadName, OutError);
	if (!NewSocket)
	{
		return nullptr;
	}

	TUniquePtr<FVMCUdpReceiver> Receiver(new FVMCUdpReceiver());
	Receiver->Socket = NewSocket;
	Receiver->BoundPort = NewSocket->GetPortNo();
	Receiver->Sender = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	Receiver->Buffer.SetNumUninitialized(65536); // the largest UDP payload
	Receiver->OnPacket = MoveTemp(InOnPacket);
	Receiver->Thread = FRunnableThread::Create(Receiver.Get(), *ThreadName, 0, TPri_AboveNormal);
	if (!Receiver->Thread)
	{
		OutError = TEXT("can't start the receive thread");
		return nullptr; // the destructor closes the socket
	}
	return Receiver;
}

FVMCUdpReceiver::~FVMCUdpReceiver()
{
	if (Thread)
	{
		Thread->Kill(/*bShouldWait*/ true); // calls Stop, then waits for Run to return
		delete Thread;
		Thread = nullptr;
	}
	if (Socket)
	{
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
}

void FVMCUdpReceiver::Stop()
{
	bStopping = true;
}

uint32 FVMCUdpReceiver::Run()
{
	// Wake at least every 100 ms to notice Stop.
	const FTimespan WaitTime = FTimespan::FromMilliseconds(100);
	while (!bStopping)
	{
		if (!Socket->Wait(ESocketWaitConditions::WaitForRead, WaitTime))
		{
			continue;
		}
		int32 BytesRead = 0;
		while (!bStopping && Socket->RecvFrom(Buffer.GetData(), Buffer.Num(), BytesRead, *Sender))
		{
			if (BytesRead <= 0)
			{
				break;
			}
			OnPacket(TConstArrayView<uint8>(Buffer.GetData(), BytesRead), FPlatformTime::Seconds(), *Sender);
		}
	}
	return 0;
}
