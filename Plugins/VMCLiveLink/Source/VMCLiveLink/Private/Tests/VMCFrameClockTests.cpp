// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// O3a: the frame clock gives unevenly arriving frames steady times, and with them Live Link's read time
// moves forward smoothly.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Math/RandomStream.h"
#include "VMCFrameClock.h"

namespace VMCFrameClockTests
{
	/**
	 * A 60 fps sender on a loaded machine, like XR Animator measured on 2026-10-08: most frames a
	 * millisecond or two late; a stall of 50 to 300 ms starting at 2% of frames, after which the frames held up arrive together,
	 * 0.3 ms apart; a quarter of the stalled frames lost; one pause (0.7 s). Seconds.
	 */
	TArray<double> MakeLoadedArrivals(int32 Seed, double Duration, double StallChance = 0.02, double MaxJitter = 0.003, double Pause = 0.7)
	{
		FRandomStream Random(Seed);
		TArray<double> Arrivals;
		const double Interval = 1.0 / 60.0;
		double StallEnd = -1.0;
		double LastArrival = 0.0;
		for (int32 Frame = 0; Frame * Interval < Duration; ++Frame)
		{
			const double Capture = 1.0 + Frame * Interval + (Frame * Interval > Duration * 0.5 ? Pause : 0.0);
			if (Capture > StallEnd && Random.FRand() < StallChance)
			{
				StallEnd = Capture + Random.FRandRange(0.05, 0.3);
			}
			double Arrival;
			if (Capture < StallEnd)
			{
				if (Random.FRand() < 0.25)
				{
					continue; // lost
				}
				Arrival = StallEnd;
			}
			else
			{
				Arrival = Capture + Random.FRandRange(0.0005, MaxJitter);
			}
			Arrival = FMath::Max(Arrival, LastArrival + 0.0003);
			Arrivals.Add(Arrival);
			LastArrival = Arrival;
		}
		return Arrivals;
	}

	/** Copy of FClockOffsetEstimatorRamp (LiveLink, not exported), with its defaults. */
	struct FClockOffsetRamp
	{
		double Estimated = 0.0;
		int32 BigErrors = 5;
		void Update(double SourceTime, double ArrivalTime)
		{
			const double Current = ArrivalTime - SourceTime;
			const double Error = Estimated - Current;
			if (FMath::Abs(Error) > 0.25)
			{
				if (++BigErrors > 5) { Estimated = Current; return; }
			}
			else
			{
				BigErrors = 0;
			}
			if (FMath::Abs(Error) < 100e-6) { Estimated = Current; return; }
			Estimated += Estimated < Current ? 100e-6 : -100e-6;
		}
	};

	/** Copy of FLiveLinkTimedDataInput::UpdateSmoothEngineTimeOffset (UE 5.6 to 5.8), cvar 1.5. */
	struct FSmoothOffset
	{
		TArray<double> Times;
		double Offset = 0.0;
		int32 ChangeCount = 0;
		int32 NumToConsider = TNumericLimits<int32>::Max();
		void Update(double SourceTime)
		{
			constexpr double NumFrames = 1.5;
			if (Times.Num() >= 200) Times.RemoveAt(0);
			Times.Add(SourceTime);
			const int32 Num = Times.Num();
			if (Num < 2) return;
			const double Latest = Times[Num - 1] - Times[Num - 2];
			if (Latest > 0.5)
			{
				Times.RemoveAt(0, Num - 1);
				Offset = 0.0;
				NumToConsider = 1;
				return;
			}
			const double PreviousAverage = Offset / NumFrames;
			if (FMath::Abs(Latest - PreviousAverage) > 0.005)
			{
				if (++ChangeCount >= 5) { NumToConsider = 4; ChangeCount = 0; }
			}
			else
			{
				ChangeCount = 0;
			}
			NumToConsider = NumToConsider < Num ? NumToConsider + 1 : Num;
			const int32 Oldest = Num - NumToConsider;
			Offset = (Times[Num - 1] - Times[Oldest]) / (NumToConsider - 1) * NumFrames;
		}
	};

	struct FResult
	{
		int32 Ticks = 0;
		int32 Held = 0;
		int32 Backwards = 0;
		double MaxBackStep = 0.0;
		double MaxSmoothChange = 0.0;
		double MeanLatency = 0.0; // tick time minus read time
	};

	/**
	 * Live Link's engine-time evaluation of one subject (FLiveLinkSubject::Update, FLiveLinkClient
	 * DoPendingWork): each tick, the frames that arrived are processed (clock offset and smooth offset
	 * updated, the arrival stamped at processing), then the subject is read at
	 * tick - user offset - clock offset - smooth offset.
	 */
	FResult Evaluate(const TArray<double>& Arrivals, const TArray<double>& Stamps, double UserOffset, double TickRate)
	{
		FResult Result;
		FClockOffsetRamp Clock;
		FSmoothOffset Smooth;
		int32 Next = 0;
		double Newest = -1.0;
		double LastSource = -1.0;
		double LastRead = -1.0;
		double LastSmooth = 0.0;
		double LatencySum = 0.0;
		const double Start = Arrivals[0] + 1.0; // after the first second
		for (double Tick = Arrivals[0]; Tick < Arrivals.Last(); Tick += 1.0 / TickRate)
		{
			for (; Next < Arrivals.Num() && Arrivals[Next] <= Tick; ++Next)
			{
				if (!FMath::IsNearlyEqual(Stamps[Next], LastSource))
				{
					LastSource = Stamps[Next];
					Clock.Update(Stamps[Next], Tick);
					Smooth.Update(Stamps[Next]);
				}
				Newest = FMath::Max(Newest, Stamps[Next]);
			}
			if (Newest < 0.0)
			{
				continue;
			}
			const double Read = Tick - UserOffset - Clock.Estimated - Smooth.Offset;
			if (Tick >= Start)
			{
				++Result.Ticks;
				Result.Held += Read > Newest ? 1 : 0;
				if (LastRead >= 0.0 && Read < LastRead)
				{
					++Result.Backwards;
					Result.MaxBackStep = FMath::Max(Result.MaxBackStep, LastRead - Read);
				}
				Result.MaxSmoothChange = FMath::Max(Result.MaxSmoothChange, FMath::Abs(Smooth.Offset - LastSmooth));
				LatencySum += Tick - Read;
			}
			LastRead = Read;
			LastSmooth = Smooth.Offset;
		}
		Result.MeanLatency = Result.Ticks > 0 ? LatencySum / Result.Ticks : 0.0;
		return Result;
	}

	FString Describe(const TCHAR* Label, const FResult& R)
	{
		return FString::Printf(TEXT("%s: held %.1f%% of %d ticks, read time back %d times (max %.1f ms), smooth offset change max %.1f ms, latency %.1f ms"),
			Label, 100.0 * R.Held / FMath::Max(R.Ticks, 1), R.Ticks, R.Backwards, R.MaxBackStep * 1000.0, R.MaxSmoothChange * 1000.0, R.MeanLatency * 1000.0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCFrameClockStampsTest, "VMC.FrameClock.Stamps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCFrameClockStampsTest::RunTest(const FString& Parameters)
{
	using namespace VMCFrameClockTests;

	// An even 60 fps stream: times follow arrivals exactly, one interval apart.
	{
		FVMCFrameClock Clock;
		double Last = 0.0;
		double MaxError = 0.0;
		for (int32 i = 0; i < 300; ++i)
		{
			const double Arrival = 10.0 + i / 60.0;
			const double Stamp = Clock.Stamp(Arrival);
			if (i > 30) MaxError = FMath::Max(MaxError, FMath::Abs(Arrival - Stamp));
			Last = Stamp;
		}
		TestTrue(TEXT("Even stream: times on the arrivals (under 1 ms)"), MaxError < 0.001);
		TestTrue(TEXT("Even stream: the interval"), FMath::IsNearlyEqual(Clock.GetInterval(), 1.0 / 60.0, 1e-4));
		TestEqual(TEXT("Even stream: no restarts"), Clock.GetRestarts(), 0);
		TestEqual(TEXT("The same packet: the same time"), Clock.Stamp(10.0 + 299 / 60.0), Last);
	}

	// The loaded stream: increasing, evenly spaced, near their arrivals.
	const TArray<double> Arrivals = MakeLoadedArrivals(7, 30.0);
	FVMCFrameClock Clock;
	double Previous = -1.0;
	int32 NotIncreasing = 0;
	int32 Uneven = 0;
	double MaxLag = 0.0;
	double MaxLead = 0.0;
	int32 RestartsSeen = 0;
	for (double Arrival : Arrivals)
	{
		const double IntervalBefore = Clock.GetInterval();
		const int32 RestartsBefore = Clock.GetRestarts();
		const double Stamp = Clock.Stamp(Arrival);
		const bool bRestarted = Clock.GetRestarts() != RestartsBefore;
		RestartsSeen += bRestarted ? 1 : 0;
		if (Previous >= 0.0)
		{
			NotIncreasing += Stamp <= Previous ? 1 : 0;
			// Within Live Link's 5 ms of the interval, except where the clock restarted or held a burst
			// to MaxLead ahead.
			const bool bHeldToLead = Stamp - Arrival >= FVMCFrameClock::MaxLead - 1e-9;
			if (!bRestarted && !bHeldToLead && IntervalBefore > 0.0 && FMath::Abs((Stamp - Previous) - IntervalBefore) > 0.005)
			{
				++Uneven;
			}
		}
		MaxLag = FMath::Max(MaxLag, Arrival - Stamp);
		MaxLead = FMath::Max(MaxLead, Stamp - Arrival);
		Previous = Stamp;
	}
	AddInfo(FString::Printf(TEXT("%d frames, %d restarts, interval %.2f ms, arrival - time from %.1f to %.1f ms"),
		Arrivals.Num(), RestartsSeen, Clock.GetInterval() * 1000.0, -MaxLead * 1000.0, MaxLag * 1000.0));
	TestEqual(TEXT("Times always increase"), NotIncreasing, 0);
	TestEqual(TEXT("Times evenly spaced (within 5 ms) except at restarts"), Uneven, 0);
	TestTrue(TEXT("Times less than MaxLag behind their arrivals"), MaxLag < FVMCFrameClock::MaxLag);
	TestTrue(TEXT("Times well short of Live Link's 0.25 s snap ahead of them"), MaxLead < FVMCFrameClock::MaxLead + 0.05);
	TestTrue(TEXT("The interval found despite the bursts (60 fps less the lost frames)"), Clock.GetInterval() > 1.0 / 60.0 && Clock.GetInterval() < 1.0 / 40.0);
	TestTrue(TEXT("The 0.7 s pause restarted it"), RestartsSeen >= 1);

	// Map: devices from the same packet get the frame's time; later ones stay within half an interval.
	TestEqual(TEXT("Map, same packet"), Clock.Map(Arrivals.Last()), Previous);
	TestTrue(TEXT("Map, later packet"), Clock.Map(Arrivals.Last() + 1.0) <= Previous + 0.5 * Clock.GetInterval() + 1e-9);
	FVMCFrameClock Fresh;
	TestEqual(TEXT("Map before any frame: the arrival"), Fresh.Map(5.0), 5.0);

	Clock.Reset();
	TestEqual(TEXT("Reset: starts from the next arrival"), Clock.Stamp(100.0), 100.0);
	TestEqual(TEXT("Reset: no restarts"), Clock.GetRestarts(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCFrameClockLiveLinkTest, "VMC.FrameClock.LiveLinkReadTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCFrameClockLiveLinkTest::RunTest(const FString& Parameters)
{
	using namespace VMCFrameClockTests;
	for (const int32 Seed : { 7, 11, 23 })
	{
		const TArray<double> Arrivals = MakeLoadedArrivals(Seed, 30.0);
		TArray<double> Steady;
		FVMCFrameClock Clock;
		for (double Arrival : Arrivals)
		{
			Steady.Add(Clock.Stamp(Arrival));
		}
		for (const double TickRate : { 30.0, 60.0 })
		{
			const FResult Raw = Evaluate(Arrivals, Arrivals, 0.0, TickRate);
			const FResult Smoothed = Evaluate(Arrivals, Steady, 0.03, TickRate);
			AddInfo(FString::Printf(TEXT("seed %d, %.0f Hz ticks, %d restarts"), Seed, TickRate, Clock.GetRestarts()));
			AddInfo(Describe(TEXT("  arrival times"), Raw));
			AddInfo(Describe(TEXT("  frame clock, 30 ms offset"), Smoothed));
			const FString At = FString::Printf(TEXT(" (seed %d, %.0f Hz)"), Seed, TickRate);
			TestTrue(TEXT("Read time never steps back more than 20 ms") + At, Smoothed.MaxBackStep < 0.02);
			TestTrue(TEXT("Read time goes back less often") + At, Smoothed.Backwards <= FMath::Max(Raw.Backwards, 1));
			TestTrue(TEXT("Smooth offset steadier") + At, Smoothed.MaxSmoothChange < Raw.MaxSmoothChange);
			TestTrue(TEXT("Held less often") + At, Smoothed.Held < Raw.Held);
		}
	}

	// A healthy stream, a few milliseconds of jitter and no stalls (XR Animator with the editor
	// closed): Live Link's smooth offset no longer moves at all.
	{
		const TArray<double> Arrivals = MakeLoadedArrivals(5, 30.0, /*StallChance*/ 0.0, /*MaxJitter*/ 0.008, /*Pause*/ 0.0);
		TArray<double> Steady;
		FVMCFrameClock Clock;
		for (double Arrival : Arrivals)
		{
			Steady.Add(Clock.Stamp(Arrival));
		}
		const FResult Raw = Evaluate(Arrivals, Arrivals, 0.0, 60.0);
		const FResult Smoothed = Evaluate(Arrivals, Steady, 0.03, 60.0);
		AddInfo(TEXT("healthy stream, 60 Hz ticks"));
		AddInfo(Describe(TEXT("  arrival times"), Raw));
		AddInfo(Describe(TEXT("  frame clock, 30 ms offset"), Smoothed));
		TestTrue(TEXT("Healthy stream: smooth offset moves under 1 ms a tick"), Smoothed.MaxSmoothChange < 0.001);
		TestEqual(TEXT("Healthy stream: read time never goes back"), Smoothed.Backwards, 0);
		TestTrue(TEXT("Healthy stream: held no more often"), Smoothed.Held <= Raw.Held);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
