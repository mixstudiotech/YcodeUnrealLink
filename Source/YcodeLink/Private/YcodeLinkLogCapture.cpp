// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#include "YcodeLinkLogCapture.h"

#include "CoreGlobals.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeLock.h"

FYcodeLinkLogCapture::~FYcodeLinkLogCapture()
{
	Uninstall();
}

void FYcodeLinkLogCapture::Install()
{
	if (bInstalled || GLog == nullptr)
	{
		return;
	}
	const FDateTime Now = FDateTime::UtcNow();
	const double NowUnix = static_cast<double>(Now.ToUnixTimestamp()) + Now.GetMillisecond() / 1000.0;
	StartUnixTime = NowUnix - (FPlatformTime::Seconds() - GStartTime);
	bInstalled = true;
	GLog->AddOutputDevice(this);
}

void FYcodeLinkLogCapture::Uninstall()
{
	if (!bInstalled)
	{
		return;
	}
	bInstalled = false;
	// GLog is already gone during a late shutdown; removing then would be a
	// use-after-free, and there is nothing left to detach from anyway.
	if (GLog != nullptr)
	{
		GLog->RemoveOutputDevice(this);
	}
}

void FYcodeLinkLogCapture::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category)
{
	Push(V, Verbosity, Category, FPlatformTime::Seconds() - GStartTime);
}

void FYcodeLinkLogCapture::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category, double Time)
{
	Push(V, Verbosity, Category, Time);
}

void FYcodeLinkLogCapture::Push(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category, double SecondsSinceStart)
{
	// Mask bits (SetColor / BreakOnLog) are not messages.
	if (V == nullptr || Verbosity > ELogVerbosity::All)
	{
		return;
	}

	FString Text(V);
	if (Text.Len() > MaxLineLength)
	{
		Text.LeftInline(MaxLineLength);
		Text += TEXT(" [... truncated ...]");
	}

	FScopeLock Guard(&Lock);
	const double UnixTime = StartUnixTime + SecondsSinceStart;
	// Multi-line messages become one entry per line so every line carries its
	// own sequence number and the IDE can filter and link them individually.
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, /*bCullEmpty=*/false);
	if (Lines.IsEmpty())
	{
		Lines.Add(FString());
	}
	for (FString& Line : Lines)
	{
		FYcodeLinkLogEntry Entry;
		Entry.Seq = NextSeq++;
		Entry.UnixTime = UnixTime;
		Entry.Verbosity = Verbosity;
		Entry.Category = Category;
		Entry.Text = MoveTemp(Line);
		Pending.Add(Entry);
		Ring.Add(MoveTemp(Entry));
	}
	// Trim in slabs so the common path is a plain append.
	if (Ring.Num() > RingCapacity + RingCapacity / 4)
	{
		Ring.RemoveAt(0, Ring.Num() - RingCapacity, EAllowShrinking::No);
	}
	if (Pending.Num() > RingCapacity)
	{
		Pending.RemoveAt(0, Pending.Num() - RingCapacity, EAllowShrinking::No);
	}
}

void FYcodeLinkLogCapture::DrainPending(TArray<FYcodeLinkLogEntry>& Out)
{
	FScopeLock Guard(&Lock);
	Out.Append(MoveTemp(Pending));
	Pending.Reset();
}

void FYcodeLinkLogCapture::DiscardPending()
{
	FScopeLock Guard(&Lock);
	Pending.Reset();
}

TArray<FYcodeLinkLogEntry> FYcodeLinkLogCapture::Tail(uint64 AfterSeq, int32 Limit, const FString& Category, ELogVerbosity::Type MinVerbosity) const
{
	TArray<FYcodeLinkLogEntry> Result;
	FScopeLock Guard(&Lock);
	const int32 Max = Limit > 0 ? Limit : RingCapacity;
	// Walk backwards so a limit means "the newest N that match".
	for (int32 Index = Ring.Num() - 1; Index >= 0 && Result.Num() < Max; --Index)
	{
		const FYcodeLinkLogEntry& Entry = Ring[Index];
		if (Entry.Seq <= AfterSeq)
		{
			break;
		}
		if (!Category.IsEmpty() && !Entry.Category.ToString().Equals(Category, ESearchCase::IgnoreCase))
		{
			continue;
		}
		// Lower verbosity values are more severe; keep everything at least as severe.
		if (MinVerbosity != ELogVerbosity::NoLogging && Entry.Verbosity > MinVerbosity)
		{
			continue;
		}
		Result.Add(Entry);
	}
	Algo::Reverse(Result);
	return Result;
}

uint64 FYcodeLinkLogCapture::LastSeq() const
{
	FScopeLock Guard(&Lock);
	return NextSeq - 1;
}
