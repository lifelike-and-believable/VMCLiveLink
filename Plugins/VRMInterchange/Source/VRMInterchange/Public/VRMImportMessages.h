// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/**
 * The warnings and errors a VRM import logs, collected so the editor can show them on a message log
 * page instead of only in the output log (P6.3). Collects what is logged under LogVRMInterchange and
 * LogVRMSpring, from any thread, between Begin and Take.
 */
namespace VRM::ImportMessages
{
	struct FMessage
	{
		ELogVerbosity::Type Verbosity = ELogVerbosity::Warning; // Error or Warning
		FString Text;
	};

	/** At most this many messages are kept per import. */
	constexpr int32 MaxMessages = 200;

	/** Starts collecting, dropping what an earlier import left. The translator calls it when an import starts. */
	VRMINTERCHANGE_API void Begin();

	/** Stops collecting and returns what was collected, oldest first. */
	VRMINTERCHANGE_API TArray<FMessage> Take();

	/**
	 * What the capture does with one log line: keeps it if collecting and it is a warning or error in
	 * a VRM import category. Tests call it directly, since a real warning logged in a test counts
	 * against the test (and one the test expects arrives demoted to Verbose).
	 */
	VRMINTERCHANGE_API void Receive(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category);

	/** Whether messages are being collected. */
	VRMINTERCHANGE_API bool IsCollecting();

	/** Adds and removes the log capture (the module's startup and shutdown). */
	void RegisterCapture();
	void UnregisterCapture();
}
