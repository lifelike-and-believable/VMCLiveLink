// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VMCFrameClock.h"

namespace VMCFrameClock
{
	/** Seconds, and frames, of arrivals before an interval estimate is trusted. */
	constexpr double MinSpanForInterval = 0.25;
	constexpr int32 MinFramesForInterval = 8;
	/** How fast the interval follows the window's average, per frame. */
	constexpr double IntervalSmoothing = 0.1;
}

void FVMCFrameClock::Reset()
{
	bStarted = false;
	LastArrival = LastStamp = Interval = Correction = 0.0;
	Restarts = 0;
	Arrivals.Reset();
	Lags.Reset();
}

void FVMCFrameClock::Restart(double ArrivalSeconds, double StampSeconds, bool bForgetRate)
{
	// Never earlier than the last time given out: Live Link keeps a subject's frames in time order.
	const double Stamp = bStarted ? FMath::Max(StampSeconds, LastStamp + MinInterval) : StampSeconds;
	Restarts += bStarted ? 1 : 0;
	bStarted = true;
	LastArrival = ArrivalSeconds;
	LastStamp = Stamp;
	Correction = 0.0;
	// The sender's interval is kept: it rarely changes, and re-learning it would take a window.
	if (bForgetRate || Arrivals.Num() == 0)
	{
		Arrivals.Reset();
		Arrivals.Add(ArrivalSeconds);
	}
	Lags.Reset();
	Lags.Add({ ArrivalSeconds, ArrivalSeconds - Stamp });
}

double FVMCFrameClock::Stamp(double ArrivalSeconds)
{
	if (bStarted && ArrivalSeconds == LastArrival)
	{
		return LastStamp; // the same packet
	}
	if (!bStarted || ArrivalSeconds < LastArrival || ArrivalSeconds - LastArrival >= ResetGap)
	{
		Restart(ArrivalSeconds, ArrivalSeconds, /*bForgetRate*/ true);
		return LastStamp;
	}

	// The sender's interval: frames over the time they took to arrive, in the window. Bursts and
	// gaps even out over it; a median wouldn't (under load, most intervals are bursts).
	Arrivals.Add(ArrivalSeconds);
	while (Arrivals.Num() > 2 && Arrivals[0] < ArrivalSeconds - Window)
	{
		Arrivals.RemoveAt(0, 1, EAllowShrinking::No);
	}
	const double Span = ArrivalSeconds - Arrivals[0];
	if (Span >= VMCFrameClock::MinSpanForInterval && Arrivals.Num() >= VMCFrameClock::MinFramesForInterval)
	{
		const double Measured = FMath::Clamp(Span / (Arrivals.Num() - 1), MinInterval, MaxInterval);
		Interval = Interval > 0.0 ? FMath::Lerp(Interval, Measured, VMCFrameClock::IntervalSmoothing) : Measured;
	}
	if (Interval <= 0.0)
	{
		// Too few frames to know the interval yet (the first quarter second): arrival times.
		LastArrival = ArrivalSeconds;
		LastStamp = FMath::Max(ArrivalSeconds, LastStamp + MinInterval);
		Lags.Add({ ArrivalSeconds, ArrivalSeconds - LastStamp + Correction });
		return LastStamp;
	}

	// One interval on, corrected toward the earliest arrivals in the window: the frames that weren't
	// held up. Later ones say nothing about the sender's clock.
	const double Uncorrected = LastStamp + Interval;
	double EarliestLag = ArrivalSeconds - Uncorrected;
	while (Lags.Num() > 0 && Lags[0].Arrival < ArrivalSeconds - Window)
	{
		Lags.RemoveAt(0, 1, EAllowShrinking::No);
	}
	for (const FLag& Past : Lags)
	{
		EarliestLag = FMath::Min(EarliestLag, Past.Lag - Correction);
	}
	const double Step = FMath::Clamp(EarliestLag, -MaxCorrection * Interval, MaxCorrection * Interval);
	double NewStamp = Uncorrected + Step;

	// Far behind its arrival: frames were lost, not just late (a burst of late ones catches up).
	if (ArrivalSeconds - NewStamp >= MaxLag)
	{
		Restart(ArrivalSeconds, ArrivalSeconds, /*bForgetRate*/ false);
		return LastStamp;
	}
	// Far ahead of it: a burst right after a restart. Held to MaxLead ahead, so the frames of the
	// burst come closer together; any longer lead and Live Link's clock offset would snap.
	if (NewStamp - ArrivalSeconds > MaxLead)
	{
		NewStamp = FMath::Max(LastStamp + MinInterval, ArrivalSeconds + MaxLead);
	}

	Correction += NewStamp - Uncorrected;
	LastArrival = ArrivalSeconds;
	LastStamp = NewStamp;
	Lags.Add({ ArrivalSeconds, ArrivalSeconds - NewStamp + Correction });
	return LastStamp;
}

double FVMCFrameClock::Map(double ArrivalSeconds) const
{
	if (!bStarted)
	{
		return ArrivalSeconds;
	}
	if (ArrivalSeconds == LastArrival)
	{
		return LastStamp;
	}
	const double HalfInterval = 0.5 * FMath::Max(Interval, MinInterval);
	return LastStamp + FMath::Clamp(ArrivalSeconds - LastArrival, 0.0, HalfInterval);
}
