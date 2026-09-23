// Copyright Pix Philosophy (HK) Limited.

#include "YcodeAgentToolSets.h"

#include "Modules/ModuleManager.h"

class FYcodeAgentToolsModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		Python = MakeUnique<FYcodePythonTools>();
		AssetSearch = MakeUnique<FYcodeAssetSearch>();
		Screenshot = MakeUnique<FYcodeScreenshot>();
		ViewportCamera = MakeUnique<FYcodeViewportCamera>();
		ActorSpawner = MakeUnique<FYcodeActorSpawner>();
	}

	virtual void ShutdownModule() override
	{
		ActorSpawner.Reset();
		ViewportCamera.Reset();
		Screenshot.Reset();
		AssetSearch.Reset();
		Python.Reset();
	}

	virtual bool SupportsDynamicReloading() override { return false; }

private:
	TUniquePtr<FYcodePythonTools> Python;
	TUniquePtr<FYcodeAssetSearch> AssetSearch;
	TUniquePtr<FYcodeScreenshot> Screenshot;
	TUniquePtr<FYcodeViewportCamera> ViewportCamera;
	TUniquePtr<FYcodeActorSpawner> ActorSpawner;
};

IMPLEMENT_MODULE(FYcodeAgentToolsModule, YcodeAgentTools);

namespace YcodeAgentJson
{
	FVector ToVector(const TSharedPtr<FJsonObject>& Object, const FVector& Default)
	{
		if (!Object.IsValid())
		{
			return Default;
		}
		const TSharedRef<FJsonObject> Ref = Object.ToSharedRef();
		return FVector(
			YcodeLinkJson::GetNumber(Ref, TEXT("x"), Default.X),
			YcodeLinkJson::GetNumber(Ref, TEXT("y"), Default.Y),
			YcodeLinkJson::GetNumber(Ref, TEXT("z"), Default.Z));
	}

	FRotator ToRotator(const TSharedPtr<FJsonObject>& Object, const FRotator& Default)
	{
		if (!Object.IsValid())
		{
			return Default;
		}
		const TSharedRef<FJsonObject> Ref = Object.ToSharedRef();
		return FRotator(
			YcodeLinkJson::GetNumber(Ref, TEXT("pitch"), Default.Pitch),
			YcodeLinkJson::GetNumber(Ref, TEXT("yaw"), Default.Yaw),
			YcodeLinkJson::GetNumber(Ref, TEXT("roll"), Default.Roll));
	}

	TSharedRef<FJsonObject> FromVector(const FVector& Vector)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("x"), Vector.X);
		Json->SetNumberField(TEXT("y"), Vector.Y);
		Json->SetNumberField(TEXT("z"), Vector.Z);
		return Json;
	}

	TSharedRef<FJsonObject> FromRotator(const FRotator& Rotator)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("pitch"), Rotator.Pitch);
		Json->SetNumberField(TEXT("yaw"), Rotator.Yaw);
		Json->SetNumberField(TEXT("roll"), Rotator.Roll);
		return Json;
	}

	FYcodeLinkToolResult Failure(const FString& Error)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetBoolField(TEXT("success"), false);
		Json->SetStringField(TEXT("error"), Error);
		return FYcodeLinkToolResult::Object(Json, /*bIsError=*/true);
	}
}
