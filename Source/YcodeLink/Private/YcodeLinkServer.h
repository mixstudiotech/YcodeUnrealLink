// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "HttpResultCallback.h"
#include "HttpRouteHandle.h"
#include "Templates/Atomic.h"
#include "Templates/SharedPointer.h"
#include "YcodeLinkTool.h"

class IHttpRouter;
struct FHttpServerRequest;
struct FHttpServerResponse;
enum class EHttpServerResponseCodes : uint16;

// Loopback MCP server (Streamable HTTP transport) built on the engine's
// HTTPServer module. One route serves POST (JSON-RPC), GET (the SSE event
// stream carrying server-to-client notifications) and DELETE (end session).
//
// The IDE is the client: it initializes a session, lists the tools every
// plugin module registered, calls them, and keeps a GET stream open for the
// log / play-state / hot-reload notifications. The same endpoint is what the
// Ycode agent (and any other MCP client given the token) connects to.
class FYcodeLinkServer final : public TSharedFromThis<FYcodeLinkServer>
{
public:
	FYcodeLinkServer();
	~FYcodeLinkServer();

	void SetServerInfo(const FString& InName, const FString& InVersion, const FString& InInstructions);

	// Binds the routes on `PreferredPort`, or on the first free port of the
	// plugin's range when that is 0 or taken. False when no port could be bound.
	bool Start(const FString& InUrlPath, uint32 PreferredPort);
	void Stop();
	bool IsRunning() const { return Router.IsValid(); }
	uint32 GetPort() const { return Port; }
	FString GetUrl() const;
	// Bearer token every request must carry; regenerated per editor run.
	const FString& GetToken() const { return Token; }
	static const TCHAR* LatestProtocolVersion();

	void RegisterTool(const FYcodeLinkTool& Tool);
	void UnregisterTool(const FString& Name);

	// Any thread. Dropped when no event stream is open.
	void Notify(const FString& Method, const TSharedPtr<FJsonObject>& Params);
	bool HasEventStreams() const { return OpenStreams.Load() > 0; }

	// Game thread, once per frame: drains queued notifications into the open
	// streams, sends keep-alives, rotates oversized streams, expires sessions.
	void Tick();

private:
	struct FEventStream
	{
		TSharedPtr<TQueue<TArray<uint8>, EQueueMode::Spsc>> Queue;
		TSharedPtr<TAtomic<bool>> Complete;
		int64 BytesQueued = 0;
		double LastWriteSeconds = 0.0;
		double LastEmptySeconds = 0.0;
	};

	struct FSession
	{
		FString Id;
		FString ProtocolVersion;
		bool bInitialized = false;
		double LastActivitySeconds = 0.0;
		TSharedPtr<FEventStream> Stream;
	};

	bool HandlePost(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleGet(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleDelete(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	// Origin (DNS rebinding) and bearer-token checks. False means a response
	// was already written.
	bool Authorize(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) const;
	TSharedPtr<FSession> FindSession(const FHttpServerRequest& Request, FString* OutHeaderValue) const;

	void HandleInitialize(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Params, const FHttpResultCallback& OnComplete);
	void HandleToolsList(const TSharedPtr<FJsonValue>& Id, const FSession& Session, const FHttpResultCallback& OnComplete) const;
	void HandleToolsCall(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Params, const FSession& Session, const FHttpResultCallback& OnComplete);

	void OpenEventStream(const TSharedRef<FSession>& Session, const FHttpResultCallback& OnComplete);
	void CloseEventStream(FSession& Session);
	static void Enqueue(FEventStream& Stream, const TArray<uint8>& Bytes);

	FString Name;
	FString Version;
	FString Instructions;
	FString UrlPath;
	uint32 Port = 0;
	FString Token;

	TSharedPtr<IHttpRouter> Router;
	FHttpRouteHandle PostRoute;
	FHttpRouteHandle GetRoute;
	FHttpRouteHandle DeleteRoute;

	TMap<FString, TSharedRef<FSession>> Sessions;
	TMap<FString, FYcodeLinkTool> Tools;
	TArray<FString> ToolOrder;

	FCriticalSection PendingLock;
	TArray<TArray<uint8>> PendingFrames;
	TAtomic<int32> OpenStreams{0};
};
