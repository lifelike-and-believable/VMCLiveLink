// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VMCSourceDiagnostics.h"

namespace VMCDiagnostics
{
	FText FormatStatus(const FVMCSourceStatusInputs& In)
	{
		if (!In.bValid)
		{
			return NSLOCTEXT("VMCLiveLink", "Status_Stopped", "Stopped");
		}
		const FText PortText = FText::AsNumber(In.Port, &FNumberFormattingOptions::DefaultNoGrouping());
		const bool bAnyAddress = In.BindAddress.IsEmpty() || In.BindAddress == TEXT("0.0.0.0");
		if (!In.bListening)
		{
			// Every interface can always be bound, so only the port can be at fault.
			return bAnyAddress
				? FText::Format(NSLOCTEXT("VMCLiveLink", "Status_PortInUse", "Port {0} in use"), PortText)
				: FText::Format(NSLOCTEXT("VMCLiveLink", "Status_CantListen", "Can't listen on {0}:{1} (port in use, or not an address of this machine)"),
					FText::FromString(In.BindAddress), PortText);
		}

		// "; sender: calibrating; locked to this sender; 2 device/camera subjects; ignoring 1 other sender", or empty
		FText Notes;
		{
			TArray<FString> Parts;
			if (!In.SenderState.IsEmpty())
			{
				Parts.Add(FString::Printf(TEXT("sender: %s"), *In.SenderState));
			}
			if (In.bLockedToSender)
			{
				Parts.Add(TEXT("locked to this sender"));
			}
			if (In.DeviceSubjects > 0)
			{
				Parts.Add(FString::Printf(TEXT("%d device/camera subject%s"), In.DeviceSubjects, In.DeviceSubjects == 1 ? TEXT("") : TEXT("s")));
			}
			if (In.IgnoredSenders > 0)
			{
				Parts.Add(FString::Printf(TEXT("ignoring %d other sender%s"), In.IgnoredSenders, In.IgnoredSenders == 1 ? TEXT("") : TEXT("s")));
			}
			if (Parts.Num() > 0)
			{
				Notes = FText::FromString(TEXT("; ") + FString::Join(Parts, TEXT("; ")));
			}
		}

		if (!In.bReceivedFrame)
		{
			return FText::Format(NSLOCTEXT("VMCLiveLink", "Status_Listening", "Listening on {0}:{1}, waiting for data{2}"),
				FText::FromString(bAnyAddress ? FString() : In.BindAddress), PortText, Notes);
		}
		const FText Sender = In.Sender.IsEmpty() ? NSLOCTEXT("VMCLiveLink", "UnknownSender", "unknown sender") : FText::FromString(In.Sender);
		if (In.SecondsSinceLastFrame > NoDataSeconds)
		{
			return FText::Format(NSLOCTEXT("VMCLiveLink", "Status_NoData", "No data for {0} s (last from {1}){2}"),
				FText::AsNumber(FMath::FloorToInt(In.SecondsSinceLastFrame)), Sender, Notes);
		}
		FNumberFormattingOptions OneDecimal;
		OneDecimal.SetMinimumFractionalDigits(1).SetMaximumFractionalDigits(1);
		return FText::Format(NSLOCTEXT("VMCLiveLink", "Status_Receiving", "Receiving {0} fps from {1}, jitter {2} ms ({3}){4}"),
			FText::AsNumber(In.MeanFrameInterval > 0.0 ? 1.0 / In.MeanFrameInterval : 0.0, &OneDecimal),
			Sender,
			FText::AsNumber(In.Jitter * 1000.0, &OneDecimal),
			In.bReceiveThread ? NSLOCTEXT("VMCLiveLink", "Path_Thread", "receive thread") : NSLOCTEXT("VMCLiveLink", "Path_Game", "game thread"),
			Notes);
	}

	const TCHAR* DescribeAddress(VMCProtocol::EAddress Kind)
	{
		using VMCProtocol::EAddress;
		switch (Kind)
		{
		case EAddress::RootPos:        return TEXT("/VMC/Ext/Root/Pos");
		case EAddress::BonePos:        return TEXT("/VMC/Ext/Bone/Pos");
		case EAddress::BlendVal:       return TEXT("/VMC/Ext/Blend/Val");
		case EAddress::BlendApply:     return TEXT("/VMC/Ext/Blend/Apply");
		case EAddress::Time:           return TEXT("/VMC/Ext/T");
		case EAddress::Available:      return TEXT("/VMC/Ext/OK");
		case EAddress::DevicePos:      return TEXT("/VMC/Ext/Hmd|Con|Tra/Pos");
		case EAddress::DevicePosLocal: return TEXT("/VMC/Ext/Hmd|Con|Tra/Pos/Local");
		case EAddress::Camera:         return TEXT("/VMC/Ext/Cam");
		default:                       return TEXT("(not used by the plugin)");
		}
	}
}

void FVMCMessageStats::Reset(double NowSeconds)
{
	Packets.store(0, std::memory_order_relaxed);
	for (std::atomic<uint64>& Count : Counts)
	{
		Count.store(0, std::memory_order_relaxed);
	}
	{
		FScopeLock Lock(&UnknownLock);
		Unknown.Reset();
		UnknownOverflow = 0;
	}
	LastReportSeconds = NowSeconds;
	LastPackets = 0;
	FMemory::Memzero(LastCounts, sizeof(LastCounts));
	LastUnknown.Reset();
}

void FVMCMessageStats::CountUnknown(const FString& Address)
{
	CountMessage(VMCProtocol::EAddress::Other);
	FScopeLock Lock(&UnknownLock);
	if (uint64* Count = Unknown.Find(Address))
	{
		++*Count;
	}
	else if (Unknown.Num() < MaxUnknownAddresses)
	{
		Unknown.Add(Address, 1);
	}
	else
	{
		++UnknownOverflow;
	}
}

FString FVMCMessageStats::Report(double NowSeconds)
{
	const double Seconds = FMath::Max(NowSeconds - LastReportSeconds, 1e-3);
	auto Rate = [Seconds](uint64 Now, uint64 Before) { return double(Now - FMath::Min(Now, Before)) / Seconds; };

	TStringBuilder<1024> Out;
	const uint64 PacketsNow = Packets.load(std::memory_order_relaxed);
	Out.Appendf(TEXT("  packets: %.1f/s (total %llu), over the last %.1f s\n"), Rate(PacketsNow, LastPackets), PacketsNow, Seconds);
	for (int32 Kind = 0; Kind < NumKinds; ++Kind)
	{
		const uint64 Count = Counts[Kind].load(std::memory_order_relaxed);
		if (Count > 0)
		{
			Out.Appendf(TEXT("  %-34s %8.1f/s (total %llu)\n"), VMCDiagnostics::DescribeAddress(VMCProtocol::EAddress(Kind)), Rate(Count, LastCounts[Kind]), Count);
		}
		LastCounts[Kind] = Count;
	}

	TMap<FString, uint64> UnknownNow;
	uint64 Overflow = 0;
	{
		FScopeLock Lock(&UnknownLock);
		UnknownNow = Unknown;
		Overflow = UnknownOverflow;
	}
	if (UnknownNow.Num() == 0)
	{
		Out.Append(TEXT("  unknown addresses: none\n"));
	}
	else
	{
		UnknownNow.KeySort(TLess<FString>());
		Out.Append(TEXT("  unknown addresses:\n"));
		for (const TPair<FString, uint64>& Pair : UnknownNow)
		{
			const uint64* Before = LastUnknown.Find(Pair.Key);
			Out.Appendf(TEXT("    %-32s %8.1f/s (total %llu)\n"), *Pair.Key, Rate(Pair.Value, Before ? *Before : 0), Pair.Value);
		}
		if (Overflow > 0)
		{
			Out.Appendf(TEXT("    (%llu more messages to addresses beyond the first %d)\n"), Overflow, MaxUnknownAddresses);
		}
	}

	LastReportSeconds = NowSeconds;
	LastPackets = PacketsNow;
	LastUnknown = MoveTemp(UnknownNow);
	return FString(Out.ToView());
}

FVMCDistribution VMCDiagnostics::Summarize(TArray<double>& Values)
{
	FVMCDistribution Out;
	Out.Count = Values.Num();
	if (Out.Count == 0)
	{
		return Out;
	}
	Values.Sort();
	// Nearest rank: the smallest value with at least P of the values at or below it.
	auto Rank = [&Values](double P) { return Values[FMath::Clamp(FMath::CeilToInt(P * Values.Num()) - 1, 0, Values.Num() - 1)]; };
	Out.Min = Values[0];
	Out.P50 = Rank(0.5);
	Out.P95 = Rank(0.95);
	Out.Max = Values.Last();
	return Out;
}

void FVMCTimingStats::Reset()
{
	FScopeLock ScopeLock(&Lock);
	Intervals.Reset();
	FrameTimeLags.Reset();
	Distances.Reset();
	SmoothOffsets.Reset();
	SmoothChanges.Reset();
	ClockOffsets.Reset();
	Evaluations = Held = Backwards = 0;
	bHasLast = false;
}

void FVMCTimingStats::Add(TArray<double>& Samples, double Value)
{
	if (Samples.Num() >= MaxSamples)
	{
		Samples.RemoveAt(0, MaxSamples / 2, EAllowShrinking::No); // rare: VMC.Stats wasn't run for a long time
	}
	Samples.Add(Value);
}

void FVMCTimingStats::AddInterval(double Seconds)
{
	FScopeLock ScopeLock(&Lock);
	Add(Intervals, Seconds);
}

void FVMCTimingStats::AddFrameTimeLag(double Seconds)
{
	FScopeLock ScopeLock(&Lock);
	Add(FrameTimeLags, Seconds);
}

void FVMCTimingStats::AddEvaluation(const FVMCEvaluationSample& Sample)
{
	FScopeLock ScopeLock(&Lock);
	++Evaluations;
	Held += Sample.DistanceToNewest < 0.0 ? 1 : 0;
	Add(Distances, Sample.DistanceToNewest);
	Add(SmoothOffsets, Sample.SmoothOffset);
	Add(ClockOffsets, Sample.ClockOffset);
	UserOffset = Sample.UserOffset;
	if (bHasLast)
	{
		Backwards += Sample.ReadTime < LastReadTime ? 1 : 0;
		Add(SmoothChanges, FMath::Abs(Sample.SmoothOffset - LastSmoothOffset));
	}
	bHasLast = true;
	LastReadTime = Sample.ReadTime;
	LastSmoothOffset = Sample.SmoothOffset;
}

void FVMCTimingStats::SetNotEngineTime(bool bInNotEngineTime)
{
	FScopeLock ScopeLock(&Lock);
	bNotEngineTime = bInNotEngineTime;
}

FString FVMCTimingStats::Report()
{
	FScopeLock ScopeLock(&Lock);
	TStringBuilder<1024> Out;
	auto Line = [&Out](const TCHAR* Label, const FVMCDistribution& D)
	{
		Out.Appendf(TEXT("  %-34s p50 %.1f  p95 %.1f  min %.1f  max %.1f ms (%d)\n"), Label, D.P50 * 1000.0, D.P95 * 1000.0, D.Min * 1000.0, D.Max * 1000.0, D.Count);
	};

	if (Intervals.Num() > 0)
	{
		Line(TEXT("frame interval:"), VMCDiagnostics::Summarize(Intervals));
	}
	else
	{
		Out.Append(TEXT("  frame interval: no frames\n"));
	}
	if (FrameTimeLags.Num() > 0)
	{
		Line(TEXT("arrival minus frame time:"), VMCDiagnostics::Summarize(FrameTimeLags));
	}

	if (bNotEngineTime)
	{
		Out.Append(TEXT("  Live Link evaluation: the source's mode isn't Engine Time, so no read-time numbers\n"));
	}
	else if (Evaluations == 0)
	{
		Out.Append(TEXT("  Live Link evaluation: none (no frames buffered, or Live Link didn't tick)\n"));
	}
	else
	{
		Out.Appendf(TEXT("  Live Link evaluations: %d, newest frame held in %d (%.1f%%), read time went back in %d\n"),
			Evaluations, Held, 100.0 * Held / Evaluations, Backwards);
		Line(TEXT("newest frame minus read time:"), VMCDiagnostics::Summarize(Distances));
		Line(TEXT("smooth offset:"), VMCDiagnostics::Summarize(SmoothOffsets));
		if (SmoothChanges.Num() > 0)
		{
			Line(TEXT("smooth offset change per tick:"), VMCDiagnostics::Summarize(SmoothChanges));
		}
		Line(TEXT("clock offset:"), VMCDiagnostics::Summarize(ClockOffsets));
		Out.Appendf(TEXT("  user offset (Engine Time Offset): %.1f ms\n"), UserOffset * 1000.0);
	}

	Intervals.Reset();
	FrameTimeLags.Reset();
	Distances.Reset();
	SmoothOffsets.Reset();
	SmoothChanges.Reset();
	ClockOffsets.Reset();
	Evaluations = Held = Backwards = 0;
	return FString(Out.ToView());
}
