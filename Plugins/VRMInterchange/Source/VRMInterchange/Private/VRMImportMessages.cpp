// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMImportMessages.h"

#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include <atomic>

namespace VRM::ImportMessages
{
	/** Sees every log line; keeps the VRM import categories' warnings and errors while an import is open. */
	class FCapture final : public FOutputDevice
	{
	public:
		virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			Receive(Text, Verbosity, Category);
		}

		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

		std::atomic<int32> NumOpen { 0 }; // open buckets, read without the lock to skip unrelated lines cheaply
		FCriticalSection Mutex;
		TMap<FString, TArray<FMessage>> Buckets; // by source file
		TArray<FMessage> Unscoped;               // logged outside any FScope while a bucket was open
	};

	FCapture& GetCapture()
	{
		static FCapture Capture;
		return Capture;
	}

	/** The files this thread's FScopes name, innermost last. */
	TArray<FString>& GetThreadFiles()
	{
		thread_local TArray<FString> Files;
		return Files;
	}

	FScope::FScope(const FString& File)
	{
		GetThreadFiles().Add(File);
	}

	FScope::~FScope()
	{
		TArray<FString>& Files = GetThreadFiles();
		if (Files.Num() > 0)
		{
			Files.Pop(EAllowShrinking::No);
		}
	}

	void Receive(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category)
	{
		FCapture& Capture = GetCapture();
		if (Capture.NumOpen.load(std::memory_order_relaxed) == 0)
		{
			return;
		}
		const ELogVerbosity::Type Level = ELogVerbosity::Type(Verbosity & ELogVerbosity::VerbosityMask);
		if (Level > ELogVerbosity::Warning || Level == ELogVerbosity::NoLogging)
		{
			return;
		}
		// By name: builds without logging have no category objects to ask (FNoLoggingCategory).
		static const FName InterchangeCategory(TEXT("LogVRMInterchange"));
		static const FName SpringCategory(TEXT("LogVRMSpring"));
		if (Category != InterchangeCategory && Category != SpringCategory)
		{
			return;
		}
		FString Message(Text);
		Message.RemoveFromStart(TEXT("[VRMInterchange] "));
		const TArray<FString>& ThreadFiles = GetThreadFiles();
		FMessage Entry{ Level == ELogVerbosity::Warning ? ELogVerbosity::Warning : ELogVerbosity::Error, MoveTemp(Message) };

		FScopeLock Lock(&Capture.Mutex);
		TArray<FMessage>* Bucket = ThreadFiles.Num() > 0 ? Capture.Buckets.Find(ThreadFiles.Last()) : nullptr;
		TArray<FMessage>& Into = Bucket ? *Bucket : Capture.Unscoped;
		if (Into.Num() < MaxMessages)
		{
			Into.Add(MoveTemp(Entry));
		}
	}

	void Begin(const FString& File)
	{
		FCapture& Capture = GetCapture();
		FScopeLock Lock(&Capture.Mutex);
		Capture.Buckets.FindOrAdd(File).Reset();
		Capture.NumOpen = Capture.Buckets.Num();
	}

	TArray<FMessage> Take(const FString& File)
	{
		// The log may be written on a thread of its own: deliver what is queued before taking it.
		// Not under the lock, since delivering calls Serialize, which takes it.
		if (GLog && IsInGameThread())
		{
			GLog->Flush();
		}
		FCapture& Capture = GetCapture();
		FScopeLock Lock(&Capture.Mutex);
		TArray<FMessage> Out;
		Capture.Buckets.RemoveAndCopyValue(File, Out);
		Out.Append(Capture.Unscoped); // shown once, with the first import taken after them
		Capture.Unscoped.Reset();
		Capture.NumOpen = Capture.Buckets.Num();
		return Out;
	}

	void Clear()
	{
		FCapture& Capture = GetCapture();
		FScopeLock Lock(&Capture.Mutex);
		Capture.Buckets.Reset();
		Capture.Unscoped.Reset();
		Capture.NumOpen = 0;
	}

	bool IsCollecting()
	{
		return GetCapture().NumOpen.load() > 0;
	}

	void RegisterCapture()
	{
		if (GLog)
		{
			GLog->AddOutputDevice(&GetCapture());
		}
	}

	void UnregisterCapture()
	{
		if (GLog)
		{
			GLog->RemoveOutputDevice(&GetCapture());
		}
	}
}
