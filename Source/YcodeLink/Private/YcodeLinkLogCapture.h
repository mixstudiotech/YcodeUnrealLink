// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogVerbosity.h"
#include "Misc/OutputDevice.h"

struct FYcodeLinkLogEntry
{
	uint64 Seq = 0;
	double UnixTime = 0.0;
	ELogVerbosity::Type Verbosity = ELogVerbosity::Log;
	FName Category;
	FString Text;
};

// Output device hooked into GLog at PreDefault. Keeps a bounded ring of the
// most recent lines (the IDE backfills from it after connecting) plus the
// lines that arrived since the last drain (the server streams those). Lines
// can be produced from any thread; everything is guarded by one lock.
class FYcodeLinkLogCapture final : public FOutputDevice
{
public:
	~FYcodeLinkLogCapture() override;

	void Install();
	void Uninstall();

	// FOutputDevice
	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override;
	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category, double Time) override;
	virtual bool CanBeUsedOnAnyThread() const override { return true; }
	virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

	// Moves the lines captured since the previous call into `Out`.
	void DrainPending(TArray<FYcodeLinkLogEntry>& Out);
	// Discards pending lines (nobody is listening) without touching the ring.
	void DiscardPending();

	// Lines with Seq > AfterSeq, oldest first, at most Limit. Category and
	// verbosity are optional filters (empty / NoLogging = no filter).
	TArray<FYcodeLinkLogEntry> Tail(uint64 AfterSeq, int32 Limit, const FString& Category, ELogVerbosity::Type MinVerbosity) const;
	uint64 LastSeq() const;

private:
	void Push(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category, double SecondsSinceStart);

	static constexpr int32 RingCapacity = 5000;
	static constexpr int32 MaxLineLength = 8192;

	mutable FCriticalSection Lock;
	TArray<FYcodeLinkLogEntry> Ring;
	TArray<FYcodeLinkLogEntry> Pending;
	uint64 NextSeq = 1;
	double StartUnixTime = 0.0;
	bool bInstalled = false;
};
