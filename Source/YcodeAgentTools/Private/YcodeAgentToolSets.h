// Copyright Pix Philosophy (HK) Limited.

#pragma once

#include "CoreMinimal.h"
#include "YcodeLinkToolSet.h"

// One tool set per feature; each constructor registers its tools with the
// YcodeLink module and the base destructor unregisters them.

class FYcodePythonTools final : public FYcodeLinkToolSet
{
public:
	FYcodePythonTools();
};

class FYcodeAssetSearch final : public FYcodeLinkToolSet
{
public:
	FYcodeAssetSearch();
};

class FYcodeScreenshot final : public FYcodeLinkToolSet
{
public:
	FYcodeScreenshot();
};

class FYcodeViewportCamera final : public FYcodeLinkToolSet
{
public:
	FYcodeViewportCamera();
};

class FYcodeActorSpawner final : public FYcodeLinkToolSet
{
public:
	FYcodeActorSpawner();
};

// Wire <-> UE math. Objects are {x,y,z} / {pitch,yaw,roll}; missing fields
// keep the default.
namespace YcodeAgentJson
{
	FVector ToVector(const TSharedPtr<FJsonObject>& Object, const FVector& Default);
	FRotator ToRotator(const TSharedPtr<FJsonObject>& Object, const FRotator& Default);
	TSharedRef<FJsonObject> FromVector(const FVector& Vector);
	TSharedRef<FJsonObject> FromRotator(const FRotator& Rotator);
	FYcodeLinkToolResult Failure(const FString& Error);
}
