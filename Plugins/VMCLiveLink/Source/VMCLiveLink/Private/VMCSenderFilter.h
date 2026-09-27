// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/**
 * Which senders' packets a VMC source uses (P4.7): an allowlist of IPv4 addresses, and optionally
 * only the first sender heard from. Not thread safe: configured while nothing is received, then
 * used by the one thread that builds frames.
 */
class FVMCSenderFilter
{
public:
	enum class EResult : uint8
	{
		Accepted,
		Locked,       // accepted, and now the only sender used
		Rejected,
		RejectedNew,  // rejected, and the first packet from this sender (log it once)
	};

	/** Sets the rules and forgets the locked sender and the rejected ones. */
	void Configure(TConstArrayView<FString> InAllowed, bool bInLockToFirst)
	{
		Allowed.Reset();
		Allowed.Append(InAllowed);
		bLockToFirst = bInLockToFirst;
		Locked.Reset();
		Rejected.Reset();
	}

	/** False when every sender is accepted, so callers can skip formatting the sender's address. */
	bool IsActive() const { return bLockToFirst || Allowed.Num() > 0; }

	/** Whether to use a packet from Sender (an IPv4 address, without the port). */
	EResult Check(const FString& Sender)
	{
		bool bAccept = Allowed.Num() == 0 || Allowed.Contains(Sender);
		if (bAccept && bLockToFirst)
		{
			if (Locked.IsEmpty())
			{
				Locked = Sender;
				return EResult::Locked;
			}
			bAccept = Sender == Locked;
		}
		if (bAccept)
		{
			return EResult::Accepted;
		}
		// Remember a few, so a flood of spoofed addresses can't grow this without bound.
		if (Rejected.Num() < MaxRemembered && !Rejected.Contains(Sender))
		{
			Rejected.Add(Sender);
			return EResult::RejectedNew;
		}
		return EResult::Rejected;
	}

	/** The sender locked to, or empty. */
	const FString& GetLockedSender() const { return Locked; }

private:
	static constexpr int32 MaxRemembered = 32;

	TSet<FString> Allowed;
	bool bLockToFirst = false;
	FString Locked;
	TSet<FString> Rejected;
};
