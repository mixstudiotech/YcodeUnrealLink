// Copyright Pix Philosophy (HK) Limited.

#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "YcodeLinkToolSet.h"

// Live Coding (Windows) with Hot Reload as the fallback, presented to the IDE
// as one "hot reload" capability plus the Live Coding specifics. State is
// polled on a ticker and pushed as notifications/ycode/hot_reload when it
// changes; a completed Live Coding patch is pushed as
// notifications/ycode/live_coding_patch_complete.
class FYcodeHotReload final : public FYcodeLinkToolSet
{
public:
	FYcodeHotReload();
	~FYcodeHotReload() override;

private:
	bool Tick(float DeltaTime);
	TSharedRef<FJsonObject> StateJson() const;
	FYcodeLinkToolResult Trigger() const;
	FYcodeLinkToolResult LiveCodingEnable(const TSharedRef<FJsonObject>& Args) const;
	FYcodeLinkToolResult LiveCodingCompile(bool bWait) const;

	FTSTicker::FDelegateHandle TickerHandle;
	FDelegateHandle PatchCompleteHandle;
	FString LastStateJson;
};
