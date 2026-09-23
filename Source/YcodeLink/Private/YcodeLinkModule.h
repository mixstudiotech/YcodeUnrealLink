// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "IYcodeLink.h"
#include "YcodeLinkLogCapture.h"

class FYcodeLinkServer;

class FYcodeLinkModule final : public IYcodeLinkModule
{
public:
	// IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual bool SupportsDynamicReloading() override { return false; }

	// IYcodeLinkModule
	virtual void RegisterTool(const FYcodeLinkTool& Tool) override;
	virtual void UnregisterTool(const FString& ToolName) override;
	virtual void Notify(const FString& Method, const TSharedPtr<FJsonObject>& Params) override;
	virtual bool IsServerRunning() const override;
	virtual FString GetServerUrl() const override;
	virtual FString GetLinkFilePath() const override;
	virtual TOptional<FYcodeLinkIdeInfo> GetRegisteredIde() const override { return RegisteredIde; }
	virtual FOnIdeRegistered& OnIdeRegistered() override { return IdeRegistered; }

private:
	void StartServer();
	void StopServer();
	bool Tick(float DeltaTime);
	void StreamCapturedLog();
	void RegisterCoreTools();
	TSharedRef<FJsonObject> BuildEditorInfo() const;
	TSharedRef<FJsonObject> BuildEpicMcpStatus() const;

	TSharedPtr<FYcodeLinkServer> Server;
	FYcodeLinkLogCapture LogCapture;
	FTSTicker::FDelegateHandle TickerHandle;
	FDelegateHandle PostEngineInitHandle;
	TOptional<FYcodeLinkIdeInfo> RegisteredIde;
	FOnIdeRegistered IdeRegistered;
	FString PluginVersion;
};
