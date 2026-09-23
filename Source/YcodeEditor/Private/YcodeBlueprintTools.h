// Copyright Pix Philosophy (HK) Limited.

#pragma once

#include "CoreMinimal.h"
#include "YcodeLinkToolSet.h"

// Blueprint navigation: open a Blueprint (optionally at a node GUID) in the
// editor and answer the path questions the IDE asks while turning log text
// into links.
class FYcodeBlueprintTools final : public FYcodeLinkToolSet
{
public:
	FYcodeBlueprintTools();

private:
	static FYcodeLinkToolResult OpenBlueprint(const FString& PathName, const FString& GuidText);
	static FYcodeLinkToolResult FindBlueprintUsages(const FString& ClassName, int32 Limit);
	static void BringEditorToFront();
};
