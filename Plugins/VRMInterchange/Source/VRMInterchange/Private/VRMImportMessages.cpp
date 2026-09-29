// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMImportMessages.h"

#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "VRMCoreLog.h"
#include "VRMInterchangeLog.h"
#include <atomic>

namespace VRM::ImportMessages
{
	/** Sees every log line; keeps the VRM import categories' warnings and errors while collecting. */
	class FCapture final : public FOutputDevice
	{
	public:
		virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (!bCollecting.load(std::memory_order_relaxed))
			{
				return;
			}
			const ELogVerbosity::Type Level = ELogVerbosity::Type(Verbosity & ELogVerbosity::VerbosityMask);
			if (Level > ELogVerbosity::Warning || Level == ELogVerbosity::NoLogging)
			{
				return;
			}
			if (Category != LogVRMInterchange.GetCategoryName() && Category != LogVRMSpring.GetCategoryName())
			{
				return;
			}
			FString Message(Text);
			Message.RemoveFromStart(TEXT("[VRMInterchange] "));
			FScopeLock Lock(&Mutex);
			if (Messages.Num() < MaxMessages)
			{
				Messages.Add({ Level == ELogVerbosity::Warning ? ELogVerbosity::Warning : ELogVerbosity::Error, MoveTemp(Message) });
			}
		}

		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

		std::atomic<bool> bCollecting { false };
		FCriticalSection Mutex;
		TArray<FMessage> Messages;
	};

	FCapture& GetCapture()
	{
		static FCapture Capture;
		return Capture;
	}

	void Begin()
	{
		FCapture& Capture = GetCapture();
		FScopeLock Lock(&Capture.Mutex);
		Capture.Messages.Reset();
		Capture.bCollecting = true;
	}

	TArray<FMessage> Take()
	{
		FCapture& Capture = GetCapture();
		FScopeLock Lock(&Capture.Mutex);
		Capture.bCollecting = false;
		return MoveTemp(Capture.Messages);
	}

	bool IsCollecting()
	{
		return GetCapture().bCollecting.load();
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
