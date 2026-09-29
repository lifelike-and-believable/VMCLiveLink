// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/**
 * The warnings and errors a VRM import logs, collected so the editor can show them on a message log
 * page instead of only in the output log (P6.3). Collects what is logged under LogVRMInterchange and
 * LogVRMSpring, from any thread.
 *
 * Each import has its own bucket, from Begin(File) to Take(File), so imports that overlap (several
 * files imported together) keep their messages apart. A message goes to the bucket of the file the
 * logging thread is working on (FScope), and is dropped if that bucket is closed. One logged outside
 * any scope while an import is open goes to the next Take that asks for it. A bucket nothing takes
 * (an import that failed or made no report) is closed after BucketLifetimeSeconds.
 *
 * Attribution relies on the capture being called on the logging thread: an output device that can be
 * used on any thread is served directly by FOutputDeviceRedirector, not from its log thread.
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

	/** An open bucket older than this is dropped when another opens or is taken. */
	constexpr double BucketLifetimeSeconds = 600.0;

	/** Opens (or empties) the bucket for File. The translator calls it when an import of File starts. */
	VRMINTERCHANGE_API void Begin(const FString& File);

	/**
	 * Closes File's bucket and returns its messages, oldest first. With bIncludeUnscoped, also those
	 * logged outside any scope since the last Take that included them.
	 */
	VRMINTERCHANGE_API TArray<FMessage> Take(const FString& File, bool bIncludeUnscoped = true);

	/** While one exists on a thread, what that thread logs belongs to File. Scopes nest. */
	class VRMINTERCHANGE_API FScope
	{
	public:
		explicit FScope(const FString& File);
		~FScope();
		FScope(const FScope&) = delete;
		FScope& operator=(const FScope&) = delete;
	};

	/**
	 * What the capture does with one log line: keeps it if a bucket is open and it is a warning or
	 * error in a VRM import category. Tests call it directly, since a real warning logged in a test
	 * counts against the test (and one the test expects arrives demoted to Verbose).
	 */
	VRMINTERCHANGE_API void Receive(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category);

	/** Closes every bucket and drops what they hold (tests, for imports another test started). */
	VRMINTERCHANGE_API void Clear();

	/** Whether any import's bucket is open. */
	VRMINTERCHANGE_API bool IsCollecting();

	/** Adds and removes the log capture (the module's startup and shutdown). */
	void RegisterCapture();
	void UnregisterCapture();
}
