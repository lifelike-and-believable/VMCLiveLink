// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMImportMessages.h"

#include "HAL/PlatformTime.h"
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
		struct FBucket
		{
			double OpenedSeconds = 0.0;
			TArray<FMessage> Messages;
		};
		TMap<FString, FBucket> Buckets;          // by source file
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
		TArray<FMessage>* Into = &Capture.Unscoped;
		if (ThreadFiles.Num() > 0)
		{
			FCapture::FBucket* Bucket = Capture.Buckets.Find(ThreadFiles.Last());
			if (!Bucket)
			{
				return; // that import's report was already shown; don't put it on another's
			}
			Into = &Bucket->Messages;
		}
		if (Into->Num() < MaxMessages)
		{
			Into->Add(MoveTemp(Entry));
		}
	}

	/** Drops buckets nothing took in time. Under the lock. */
	void PruneStale(FCapture& Capture, double NowSeconds)
	{
		for (auto It = Capture.Buckets.CreateIterator(); It; ++It)
		{
			if (NowSeconds - It.Value().OpenedSeconds > BucketLifetimeSeconds)
			{
				It.RemoveCurrent();
			}
		}
	}

	void Begin(const FString& File)
	{
		FCapture& Capture = GetCapture();
		const double Now = FPlatformTime::Seconds();
		FScopeLock Lock(&Capture.Mutex);
		PruneStale(Capture, Now);
		if (Capture.Buckets.Num() == 0)
		{
			Capture.Unscoped.Reset(); // nothing was open: whatever is there predates this import
		}
		FCapture::FBucket& Bucket = Capture.Buckets.FindOrAdd(File);
		Bucket.OpenedSeconds = Now;
		Bucket.Messages.Reset();
		Capture.NumOpen = Capture.Buckets.Num();
	}

	/** Delivers anything still queued for buffered devices (this capture is served directly, but a
	 *  flush costs little and keeps that assumption from mattering). Not under the lock, since
	 *  delivering calls Serialize, which takes it. */
	static void FlushLog()
	{
		if (GLog && IsInGameThread())
		{
			GLog->Flush();
		}
	}

	TArray<FMessage> Take(const FString& File)
	{
		FlushLog();
		FCapture& Capture = GetCapture();
		FScopeLock Lock(&Capture.Mutex);
		TArray<FMessage> Out;
		FCapture::FBucket Bucket;
		if (Capture.Buckets.RemoveAndCopyValue(File, Bucket))
		{
			Out = MoveTemp(Bucket.Messages);
		}
		PruneStale(Capture, FPlatformTime::Seconds());
		Capture.NumOpen = Capture.Buckets.Num();
		return Out;
	}

	TArray<FMessage> TakeUnscoped()
	{
		FlushLog();
		FCapture& Capture = GetCapture();
		FScopeLock Lock(&Capture.Mutex);
		TArray<FMessage> Out = MoveTemp(Capture.Unscoped);
		Capture.Unscoped.Reset();
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
