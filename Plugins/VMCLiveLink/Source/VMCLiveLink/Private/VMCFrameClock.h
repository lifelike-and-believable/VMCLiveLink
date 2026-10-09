// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/**
 * Steady times for VMC frames that arrive unevenly (smoothness plan, O3a).
 *
 * Live Link reads a subject some time behind its newest frame: the source's Engine Time Offset, its
 * clock offset and its smooth offset, which is the average interval between frame times times
 * LiveLink.TimedDataInput.NumFramesForSmoothOffset. Frames stamped with their arrival times give it
 * the arrival jitter: an interval more than 5 ms from the average restarts that average, so the smooth
 * offset, and with it the read time, jumps from tick to tick and can step backwards.
 *
 * VMC frames carry no time of their own that senders reliably send, so this clock makes one: each
 * frame's time is the previous one plus the sender's average interval (frames over the last second),
 * nudged by at most MaxCorrection of an interval per frame toward the earliest arrivals, the ones that
 * weren't held up. Successive times are evenly spaced and follow the sender's rate without its jitter.
 * Frames held up and then delivered together keep their spacing and catch up with their arrivals by
 * the end of the burst. A frame MaxLag or more behind its arrival (frames were lost), or after a gap
 * of ResetGap, starts the clock again from its arrival; a burst after that would take the times ahead
 * of the arrivals, so they're held to MaxLead ahead: the rest of the burst gets times a tenth of a
 * millisecond apart (MinSqueezeStep). Both
 * bounds keep Live Link's clock offset estimate from snapping (0.25 s), which steps the read time.
 * (Tuned on a model of Live Link's timing: VMC.FrameClock.LiveLinkReadTime.)
 *
 * Used by the thread that builds frames.
 */
class FVMCFrameClock
{
public:
	/** Seconds of arrivals the interval and the earliest arrivals are taken from. */
	static constexpr double Window = 1.0;
	/** The most a frame's time is corrected per frame, as a share of the interval: inside Live Link's
	 *  5 ms threshold at any rate above 10 fps. */
	static constexpr double MaxCorrection = 0.05;
	/** A frame arriving this long after the time it would get restarts the clock. */
	static constexpr double MaxLag = 0.2;
	/** The furthest a frame's time may be ahead of its arrival. */
	static constexpr double MaxLead = 0.15;
	/** A gap this long restarts the clock: the sender paused (Live Link restarts its average too). */
	static constexpr double ResetGap = 0.5;
	/** The step between the times of a burst held to MaxLead ahead: above Live Link's "same time"
	 *  tolerance, and small enough that no burst takes the times past MaxLead. */
	static constexpr double MinSqueezeStep = 1e-4;
	/** Interval limits: 240 fps to 4 fps. */
	static constexpr double MinInterval = 1.0 / 240.0;
	static constexpr double MaxInterval = 0.25;

	/** Forgets the stream (receiving restarted). */
	void Reset();

	/** The time for a body frame that arrived at ArrivalSeconds. A frame from the same packet as the
	 *  previous one (the same arrival time) gets the same time. */
	double Stamp(double ArrivalSeconds);

	/** The time for something else the same sender sent (a device or camera) that arrived at
	 *  ArrivalSeconds, without moving the clock: the last body frame's time, which Live Link's
	 *  per-source smoothing doesn't count again. Its arrival time before any frame, or when the body
	 *  frames stopped (ResetGap). A caller keeping several subjects in time order still has to make
	 *  each one's times increase. */
	double Map(double ArrivalSeconds) const;

	/** The current estimate of the sender's interval, seconds; 0 until 8 frames over a quarter second. */
	double GetInterval() const { return Interval; }
	/** Times the clock was restarted or moved on since Reset (gaps, frames lost). */
	int32 GetRestarts() const { return Restarts; }

private:
	/** Starts the clock again at StampSeconds (never before the last time) for a frame that arrived at
	 *  ArrivalSeconds. bForgetRate: the sender paused, so the arrivals before say nothing about its
	 *  rate; otherwise they're kept. */
	void Restart(double ArrivalSeconds, double StampSeconds, bool bForgetRate);

	bool bStarted = false;
	double LastArrival = 0.0;
	double LastStamp = 0.0;
	double Interval = 0.0;     // smoothed estimate of the sender's interval
	int32 Restarts = 0;
	double Correction = 0.0;   // the corrections applied since the restart, summed

	TArray<double> Arrivals;   // arrival times in the window, oldest first
	/** A frame in the window: arrival minus its time, plus Correction when it was stamped. Minus the
	 *  current Correction, that's its lag behind the clock as corrected since. */
	struct FLag { double Arrival; double Lag; };
	TArray<FLag> Lags;
};
