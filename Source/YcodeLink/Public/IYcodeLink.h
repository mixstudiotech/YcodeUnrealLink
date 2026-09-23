// Copyright Pix Philosophy (HK) Limited.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "Misc/Optional.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "YcodeLinkTool.h"

// The IDE instance that announced itself through `ue_ide_register`. Source
// code access prefers it over any installed Ycode, so "Open in Ycode" lands in
// the workbench that is actually talking to this editor.
struct FYcodeLinkIdeInfo
{
	FString Name;
	FString Version;
	FString ExecutablePath;
	uint32 ProcessId = 0;
};

// Module interface of the Ycode editor link. Other plugin modules register
// their MCP tools here and push server-to-client notifications through
// Notify(); the IDE side talks to the HTTP endpoint this module owns.
class YCODELINK_API IYcodeLinkModule : public IModuleInterface
{
public:
	static FName GetModuleName()
	{
		static const FName ModuleName(TEXT("YcodeLink"));
		return ModuleName;
	}

	static IYcodeLinkModule& Get()
	{
		return FModuleManager::GetModuleChecked<IYcodeLinkModule>(GetModuleName());
	}

	static IYcodeLinkModule* GetPtr()
	{
		return FModuleManager::GetModulePtr<IYcodeLinkModule>(GetModuleName());
	}

	// Tool names are unique; registering an existing name replaces it.
	virtual void RegisterTool(const FYcodeLinkTool& Tool) = 0;
	virtual void UnregisterTool(const FString& Name) = 0;

	// Broadcasts a JSON-RPC notification (`method`, `params`) to every IDE
	// session with an open event stream. Safe from any thread; delivery
	// happens on the game thread tick. Nothing is queued while no stream is
	// open.
	virtual void Notify(const FString& Method, const TSharedPtr<FJsonObject>& Params) = 0;

	virtual bool IsServerRunning() const = 0;
	virtual FString GetServerUrl() const = 0;
	virtual FString GetLinkFilePath() const = 0;

	virtual TOptional<FYcodeLinkIdeInfo> GetRegisteredIde() const = 0;
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnIdeRegistered, const FYcodeLinkIdeInfo&);
	virtual FOnIdeRegistered& OnIdeRegistered() = 0;
};
