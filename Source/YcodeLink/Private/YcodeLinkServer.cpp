// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#include "YcodeLinkServer.h"

#include "YcodeLinkLog.h"

#include "Async/Async.h"
#include "HAL/PlatformTime.h"
#include "HttpPath.h"
#include "HttpServerConstants.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Misc/Guid.h"
#include "Misc/ScopeLock.h"

namespace
{
	constexpr uint32 BasePort = 46100;
	constexpr uint32 PortRange = 200;
	constexpr double KeepAliveSeconds = 15.0;
	constexpr double SessionIdleSeconds = 30.0 * 60.0;
	constexpr double DeadStreamSeconds = 30.0;
	constexpr int64 RotateStreamBytes = 4 * 1024 * 1024;

	constexpr int32 RpcParseError = -32700;
	constexpr int32 RpcInvalidRequest = -32600;
	constexpr int32 RpcMethodNotFound = -32601;
	constexpr int32 RpcInvalidParams = -32602;
	constexpr int32 RpcResourceNotFound = -32002;

	const TCHAR* const SessionHeader = TEXT("Mcp-Session-Id");

	const TArray<FString>& SupportedProtocolVersions()
	{
		static const TArray<FString> Versions = {
			TEXT("2025-11-25"), TEXT("2025-06-18"), TEXT("2025-03-26"), TEXT("2024-11-05"),
		};
		return Versions;
	}

	// The engine lowercases request header keys; compare case-insensitively
	// so the code does not depend on that detail.
	const FString* FindHeader(const FHttpServerRequest& Request, const TCHAR* Key)
	{
		for (const TPair<FString, TArray<FString>>& Pair : Request.Headers)
		{
			if (Pair.Key.Equals(Key, ESearchCase::IgnoreCase) && !Pair.Value.IsEmpty())
			{
				return &Pair.Value[0];
			}
		}
		return nullptr;
	}

	TUniquePtr<FHttpServerResponse> EmptyResponse(EHttpServerResponseCodes Code)
	{
		TUniquePtr<FHttpServerResponse> Response = MakeUnique<FHttpServerResponse>();
		Response->Code = Code;
		return Response;
	}

	TUniquePtr<FHttpServerResponse> JsonResponse(const TSharedRef<FJsonObject>& Body, const FString* SessionId,
	                                              EHttpServerResponseCodes Code = EHttpServerResponseCodes::Ok)
	{
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(YcodeLinkJson::ToUtf8(Body), TEXT("application/json"));
		Response->Code = Code;
		if (SessionId)
		{
			Response->Headers.Add(SessionHeader, { *SessionId });
		}
		return Response;
	}

	TSharedRef<FJsonObject> RpcEnvelope(const TSharedPtr<FJsonValue>& Id)
	{
		TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
		Message->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
		Message->SetField(TEXT("id"), Id.IsValid() ? Id : MakeShared<FJsonValueNull>());
		return Message;
	}

	TSharedRef<FJsonObject> RpcResult(const TSharedPtr<FJsonValue>& Id, const TSharedRef<FJsonObject>& Result)
	{
		TSharedRef<FJsonObject> Message = RpcEnvelope(Id);
		Message->SetObjectField(TEXT("result"), Result);
		return Message;
	}

	TSharedRef<FJsonObject> RpcError(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Text)
	{
		TSharedRef<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetNumberField(TEXT("code"), Code);
		Error->SetStringField(TEXT("message"), Text);
		TSharedRef<FJsonObject> Message = RpcEnvelope(Id);
		Message->SetObjectField(TEXT("error"), Error);
		return Message;
	}

	void CompleteWithError(const FHttpResultCallback& OnComplete, const TSharedPtr<FJsonValue>& Id, int32 Code,
	                       const FString& Text, const FString* SessionId = nullptr,
	                       EHttpServerResponseCodes HttpCode = EHttpServerResponseCodes::BadRequest)
	{
		OnComplete(JsonResponse(RpcError(Id, Code, Text), SessionId, HttpCode));
	}

	TArray<uint8> Utf8Bytes(const FString& Text)
	{
		const FTCHARToUTF8 Converted(*Text, Text.Len());
		TArray<uint8> Bytes;
		Bytes.Append(reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
		return Bytes;
	}

	TArray<uint8> SseFrame(const TSharedRef<FJsonObject>& Message)
	{
		return Utf8Bytes(TEXT("event: message\ndata: ") + YcodeLinkJson::ToCompactString(Message) + TEXT("\n\n"));
	}

	TArray<uint8> SseComment(const TCHAR* Text)
	{
		return Utf8Bytes(FString::Printf(TEXT(": %s\n\n"), Text));
	}

	// Browsers send Origin; anything but a loopback origin is a DNS-rebinding
	// attempt against a server that only ever listens on localhost.
	bool OriginAllowed(const FHttpServerRequest& Request)
	{
		const FString* Origin = FindHeader(Request, TEXT("origin"));
		if (!Origin || Origin->IsEmpty())
		{
			return true;
		}
		FString Scheme;
		FString HostAndPort;
		if (!Origin->Split(TEXT("://"), &Scheme, &HostAndPort))
		{
			return false;
		}
		FString Host = HostAndPort;
		if (Host.StartsWith(TEXT("[")))
		{
			int32 Close = INDEX_NONE;
			if (Host.FindChar(TEXT(']'), Close))
			{
				Host = Host.Left(Close + 1);
			}
		}
		else
		{
			FString Port;
			Host.Split(TEXT(":"), &Host, &Port);
		}
		return Host == TEXT("localhost") || Host == TEXT("127.0.0.1") || Host == TEXT("[::1]");
	}
}

FYcodeLinkServer::FYcodeLinkServer()
	: Name(TEXT("ycode-link"))
	, Version(TEXT("1.0.0"))
{
}

FYcodeLinkServer::~FYcodeLinkServer()
{
	Stop();
}

void FYcodeLinkServer::SetServerInfo(const FString& InName, const FString& InVersion, const FString& InInstructions)
{
	Name = InName;
	Version = InVersion;
	Instructions = InInstructions;
}

const TCHAR* FYcodeLinkServer::LatestProtocolVersion()
{
	return *SupportedProtocolVersions()[0];
}

FString FYcodeLinkServer::GetUrl() const
{
	return IsRunning() ? FString::Printf(TEXT("http://127.0.0.1:%u%s"), Port, *UrlPath) : FString();
}

bool FYcodeLinkServer::Start(const FString& InUrlPath, uint32 PreferredPort)
{
	if (IsRunning())
	{
		return true;
	}
	UrlPath = InUrlPath;
	const FHttpPath RoutePath(UrlPath);
	if (!RoutePath.IsValidPath())
	{
		UE_LOG(LogYcodeLink, Error, TEXT("Invalid MCP route path '%s'"), *UrlPath);
		return false;
	}

	// GetHttpRouter only reports a bind failure once listeners are enabled;
	// enabling them first turns the call into a port probe.
	FHttpServerModule& Http = FHttpServerModule::Get();
	Http.StartAllListeners();

	TArray<uint32> Candidates;
	if (PreferredPort != 0)
	{
		Candidates.Add(PreferredPort);
	}
	for (uint32 Candidate = BasePort; Candidate < BasePort + PortRange; ++Candidate)
	{
		Candidates.AddUnique(Candidate);
	}
	for (uint32 Candidate : Candidates)
	{
		if (TSharedPtr<IHttpRouter> Bound = Http.GetHttpRouter(Candidate, /*bFailOnBindFailure=*/true))
		{
			Router = Bound;
			Port = Candidate;
			break;
		}
	}
	if (!Router.IsValid())
	{
		UE_LOG(LogYcodeLink, Error, TEXT("No free loopback port in %u-%u; the Ycode link is unavailable."), BasePort, BasePort + PortRange - 1);
		return false;
	}

	Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	PostRoute = Router->BindRoute(RoutePath, EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateSP(this, &FYcodeLinkServer::HandlePost));
	GetRoute = Router->BindRoute(RoutePath, EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateSP(this, &FYcodeLinkServer::HandleGet));
	DeleteRoute = Router->BindRoute(RoutePath, EHttpServerRequestVerbs::VERB_DELETE,
		FHttpRequestHandler::CreateSP(this, &FYcodeLinkServer::HandleDelete));
	UE_LOG(LogYcodeLink, Log, TEXT("Ycode link listening on %s"), *GetUrl());
	return true;
}

void FYcodeLinkServer::Stop()
{
	for (TPair<FString, TSharedRef<FSession>>& Pair : Sessions)
	{
		CloseEventStream(*Pair.Value);
	}
	Sessions.Empty();
	if (Router.IsValid())
	{
		for (FHttpRouteHandle* Handle : { &PostRoute, &GetRoute, &DeleteRoute })
		{
			if (Handle->IsValid())
			{
				Router->UnbindRoute(*Handle);
			}
			Handle->Reset();
		}
		Router.Reset();
	}
	Port = 0;
	Token.Empty();
	{
		FScopeLock Guard(&PendingLock);
		PendingFrames.Reset();
	}
}

// ------------------------------------------------------------------ tools --

void FYcodeLinkServer::RegisterTool(const FYcodeLinkTool& Tool)
{
	if (Tool.Name.IsEmpty() || !Tool.Handler)
	{
		return;
	}
	if (!Tools.Contains(Tool.Name))
	{
		ToolOrder.Add(Tool.Name);
	}
	Tools.Add(Tool.Name, Tool);
}

void FYcodeLinkServer::UnregisterTool(const FString& ToolName)
{
	Tools.Remove(ToolName);
	ToolOrder.Remove(ToolName);
}

// ------------------------------------------------------------ notifications --

void FYcodeLinkServer::Notify(const FString& Method, const TSharedPtr<FJsonObject>& Params)
{
	if (OpenStreams.Load() <= 0)
	{
		return;
	}
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	Message->SetStringField(TEXT("method"), Method);
	Message->SetObjectField(TEXT("params"), Params.IsValid() ? Params : MakeShared<FJsonObject>());
	TArray<uint8> Frame = SseFrame(Message);
	FScopeLock Guard(&PendingLock);
	PendingFrames.Add(MoveTemp(Frame));
}

void FYcodeLinkServer::Enqueue(FEventStream& Stream, const TArray<uint8>& Bytes)
{
	Stream.BytesQueued += Bytes.Num();
	Stream.LastWriteSeconds = FPlatformTime::Seconds();
	Stream.Queue->Enqueue(Bytes);
}

void FYcodeLinkServer::Tick()
{
	if (!IsRunning())
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();

	TArray<TArray<uint8>> Frames;
	{
		FScopeLock Guard(&PendingLock);
		Frames = MoveTemp(PendingFrames);
		PendingFrames.Reset();
	}

	for (auto It = Sessions.CreateIterator(); It; ++It)
	{
		FSession& Session = *It.Value();
		if (Now - Session.LastActivitySeconds > SessionIdleSeconds)
		{
			CloseEventStream(Session);
			It.RemoveCurrent();
			continue;
		}
		if (!Session.Stream.IsValid())
		{
			continue;
		}
		FEventStream& Stream = *Session.Stream;
		for (const TArray<uint8>& Frame : Frames)
		{
			Enqueue(Stream, Frame);
		}
		if (Now - Stream.LastWriteSeconds > KeepAliveSeconds)
		{
			Enqueue(Stream, SseComment(TEXT("keepalive")));
		}
		// The HTTP layer never tells us when a client went away; a queue
		// nobody drains is the only symptom, and the size cap bounds what a
		// long session can accumulate (the engine keeps every written byte in
		// the response body until the stream closes).
		if (Stream.Queue->IsEmpty())
		{
			Stream.LastEmptySeconds = Now;
		}
		const bool bDead = Now - Stream.LastEmptySeconds > DeadStreamSeconds;
		if (bDead || Stream.BytesQueued > RotateStreamBytes)
		{
			UE_LOG(LogYcodeLink, Verbose, TEXT("Closing event stream of session %s (%s)"), *Session.Id, bDead ? TEXT("no consumer") : TEXT("rotation"));
			CloseEventStream(Session);
		}
	}
}

// ---------------------------------------------------------------- requests --

bool FYcodeLinkServer::Authorize(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) const
{
	if (!OriginAllowed(Request))
	{
		UE_LOG(LogYcodeLink, Warning, TEXT("Rejected request with a non-loopback Origin"));
		OnComplete(EmptyResponse(EHttpServerResponseCodes::Forbidden));
		return false;
	}
	if (Token.IsEmpty())
	{
		return true;
	}
	if (const FString* Bearer = FindHeader(Request, TEXT("authorization")))
	{
		if (Bearer->StartsWith(TEXT("Bearer ")) && Bearer->Mid(7).TrimStartAndEnd() == Token)
		{
			return true;
		}
	}
	if (const FString* Direct = FindHeader(Request, TEXT("x-ycode-token")))
	{
		if (Direct->TrimStartAndEnd() == Token)
		{
			return true;
		}
	}
	OnComplete(EmptyResponse(EHttpServerResponseCodes::Denied));
	return false;
}

TSharedPtr<FYcodeLinkServer::FSession> FYcodeLinkServer::FindSession(const FHttpServerRequest& Request, FString* OutHeaderValue) const
{
	const FString* Value = FindHeader(Request, SessionHeader);
	if (OutHeaderValue)
	{
		*OutHeaderValue = Value ? *Value : FString();
	}
	if (!Value)
	{
		return nullptr;
	}
	const TSharedRef<FSession>* Session = Sessions.Find(*Value);
	return Session ? TSharedPtr<FSession>(*Session) : nullptr;
}

bool FYcodeLinkServer::HandlePost(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	if (!Authorize(Request, OnComplete))
	{
		return true;
	}

	const FString BodyText(FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num()));
	const TSharedPtr<FJsonObject> Rpc = YcodeLinkJson::Parse(BodyText);
	if (!Rpc.IsValid())
	{
		const bool bBatch = BodyText.TrimStart().StartsWith(TEXT("["));
		CompleteWithError(OnComplete, nullptr, bBatch ? RpcInvalidRequest : RpcParseError,
		                  bBatch ? TEXT("JSON-RPC batches are not supported") : TEXT("Request body is not a JSON object"));
		return true;
	}

	const TSharedPtr<FJsonValue> Id = Rpc->TryGetField(TEXT("id"));
	const FString Method = YcodeLinkJson::GetString(Rpc.ToSharedRef(), TEXT("method"));
	const TSharedPtr<FJsonObject> Params = YcodeLinkJson::GetObject(Rpc.ToSharedRef(), TEXT("params"));

	if (Method == TEXT("initialize"))
	{
		HandleInitialize(Id, Params, OnComplete);
		return true;
	}

	FString SessionHeaderValue;
	const TSharedPtr<FSession> Session = FindSession(Request, &SessionHeaderValue);
	if (Session.IsValid())
	{
		Session->LastActivitySeconds = FPlatformTime::Seconds();
	}

	if (Method == TEXT("ping"))
	{
		OnComplete(JsonResponse(RpcResult(Id, MakeShared<FJsonObject>()), Session ? &Session->Id : nullptr));
		return true;
	}

	if (!Session.IsValid())
	{
		// Unknown session ids get 404 so a client that outlived a previous
		// editor run re-initializes instead of retrying forever.
		const bool bUnknown = !SessionHeaderValue.IsEmpty();
		CompleteWithError(OnComplete, Id, RpcInvalidRequest,
		                  bUnknown ? TEXT("Unknown Mcp-Session-Id; initialize again") : TEXT("Missing Mcp-Session-Id header"),
		                  nullptr, bUnknown ? EHttpServerResponseCodes::NotFound : EHttpServerResponseCodes::BadRequest);
		return true;
	}

	if (Method.StartsWith(TEXT("notifications/")))
	{
		if (Method == TEXT("notifications/initialized"))
		{
			Session->bInitialized = true;
		}
		OnComplete(EmptyResponse(EHttpServerResponseCodes::Accepted));
		return true;
	}

	if (Method == TEXT("tools/list"))
	{
		HandleToolsList(Id, *Session, OnComplete);
	}
	else if (Method == TEXT("tools/call"))
	{
		HandleToolsCall(Id, Params, *Session, OnComplete);
	}
	else if (Method == TEXT("resources/list"))
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("resources"), {});
		OnComplete(JsonResponse(RpcResult(Id, Result), &Session->Id));
	}
	else if (Method == TEXT("resources/read"))
	{
		CompleteWithError(OnComplete, Id, RpcResourceNotFound, TEXT("Resource not found"), &Session->Id);
	}
	else
	{
		CompleteWithError(OnComplete, Id, RpcMethodNotFound, FString::Printf(TEXT("Method not found: %s"), *Method), &Session->Id);
	}
	return true;
}

void FYcodeLinkServer::HandleInitialize(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Params, const FHttpResultCallback& OnComplete)
{
	FString Requested;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("protocolVersion"), Requested);
	}
	const FString Negotiated = SupportedProtocolVersions().Contains(Requested) ? Requested : FString(LatestProtocolVersion());

	TSharedRef<FSession> Session = MakeShared<FSession>();
	Session->Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	Session->ProtocolVersion = Negotiated;
	Session->LastActivitySeconds = FPlatformTime::Seconds();
	Sessions.Add(Session->Id, Session);

	TSharedRef<FJsonObject> Capabilities = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> ToolsCapability = MakeShared<FJsonObject>();
	ToolsCapability->SetBoolField(TEXT("listChanged"), false);
	Capabilities->SetObjectField(TEXT("tools"), ToolsCapability);
	Capabilities->SetObjectField(TEXT("resources"), MakeShared<FJsonObject>());
	Capabilities->SetObjectField(TEXT("logging"), MakeShared<FJsonObject>());

	TSharedRef<FJsonObject> ServerInfo = MakeShared<FJsonObject>();
	ServerInfo->SetStringField(TEXT("name"), Name);
	ServerInfo->SetStringField(TEXT("title"), TEXT("Ycode Link (Unreal Editor)"));
	ServerInfo->SetStringField(TEXT("version"), Version);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("protocolVersion"), Negotiated);
	Result->SetObjectField(TEXT("capabilities"), Capabilities);
	Result->SetObjectField(TEXT("serverInfo"), ServerInfo);
	if (!Instructions.IsEmpty())
	{
		Result->SetStringField(TEXT("instructions"), Instructions);
	}
	OnComplete(JsonResponse(RpcResult(Id, Result), &Session->Id));
}

void FYcodeLinkServer::HandleToolsList(const TSharedPtr<FJsonValue>& Id, const FSession& Session, const FHttpResultCallback& OnComplete) const
{
	TArray<TSharedPtr<FJsonValue>> List;
	for (const FString& ToolName : ToolOrder)
	{
		const FYcodeLinkTool* Tool = Tools.Find(ToolName);
		if (!Tool)
		{
			continue;
		}
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Tool->Name);
		Entry->SetStringField(TEXT("description"), Tool->Description);
		Entry->SetObjectField(TEXT("inputSchema"), Tool->InputSchema.IsValid() ? Tool->InputSchema : FYcodeLinkSchema().Build());
		TSharedRef<FJsonObject> Annotations = MakeShared<FJsonObject>();
		Annotations->SetBoolField(TEXT("readOnlyHint"), Tool->bReadOnly);
		Annotations->SetBoolField(TEXT("openWorldHint"), false);
		Entry->SetObjectField(TEXT("annotations"), Annotations);
		List.Add(MakeShared<FJsonValueObject>(Entry));
	}
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("tools"), List);
	OnComplete(JsonResponse(RpcResult(Id, Result), &Session.Id));
}

void FYcodeLinkServer::HandleToolsCall(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Params, const FSession& Session, const FHttpResultCallback& OnComplete)
{
	const FString ToolName = Params.IsValid() ? YcodeLinkJson::GetString(Params.ToSharedRef(), TEXT("name")) : FString();
	const FYcodeLinkTool* Tool = ToolName.IsEmpty() ? nullptr : Tools.Find(ToolName);
	if (!Tool)
	{
		CompleteWithError(OnComplete, Id, RpcInvalidParams, FString::Printf(TEXT("Unknown tool: %s"), *ToolName), &Session.Id);
		return;
	}
	const TSharedPtr<FJsonObject> ArgsObject = YcodeLinkJson::GetObject(Params.ToSharedRef(), TEXT("arguments"));
	const TSharedRef<FJsonObject> Args = ArgsObject.IsValid() ? ArgsObject.ToSharedRef() : MakeShared<FJsonObject>();

	UE_LOG(LogYcodeLink, Verbose, TEXT("tools/call %s"), *ToolName);

	// Completion may come from any thread and, defensively, more than once;
	// the first game-thread delivery writes the response, later ones are
	// dropped. The weak server pointer covers completion after shutdown.
	TWeakPtr<FYcodeLinkServer> WeakThis = AsShared();
	const FString SessionId = Session.Id;
	TSharedRef<bool> bDelivered = MakeShared<bool>(false);
	FYcodeLinkToolCallback Complete = [WeakThis, OnComplete, Id, SessionId, ToolName, bDelivered](const FYcodeLinkToolResult& Result)
	{
		const TSharedPtr<FJsonObject> ResultJson = Result.Json.IsValid()
			? Result.Json
			: FYcodeLinkToolResult::Error(TEXT("Tool returned no result")).Json;
		auto Deliver = [WeakThis, OnComplete, Id, SessionId, ToolName, bDelivered, ResultJson]()
		{
			if (*bDelivered)
			{
				UE_LOG(LogYcodeLink, Warning, TEXT("Tool %s completed twice; ignoring the second result"), *ToolName);
				return;
			}
			*bDelivered = true;
			if (!WeakThis.IsValid())
			{
				return;
			}
			OnComplete(JsonResponse(RpcResult(Id, ResultJson.ToSharedRef()), &SessionId));
		};
		if (IsInGameThread())
		{
			Deliver();
		}
		else
		{
			AsyncTask(ENamedThreads::GameThread, MoveTemp(Deliver));
		}
	};
	Tool->Handler(Args, Complete);
}

bool FYcodeLinkServer::HandleGet(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	if (!Authorize(Request, OnComplete))
	{
		return true;
	}
	const FString* Accept = FindHeader(Request, TEXT("accept"));
	if (!Accept || !Accept->Contains(TEXT("text/event-stream")))
	{
		OnComplete(EmptyResponse(EHttpServerResponseCodes::BadMethod));
		return true;
	}
	FString SessionHeaderValue;
	const TSharedPtr<FSession> Session = FindSession(Request, &SessionHeaderValue);
	if (!Session.IsValid())
	{
		OnComplete(EmptyResponse(SessionHeaderValue.IsEmpty() ? EHttpServerResponseCodes::BadRequest : EHttpServerResponseCodes::NotFound));
		return true;
	}
	Session->LastActivitySeconds = FPlatformTime::Seconds();
	OpenEventStream(Session.ToSharedRef(), OnComplete);
	return true;
}

void FYcodeLinkServer::OpenEventStream(const TSharedRef<FSession>& Session, const FHttpResultCallback& OnComplete)
{
	// One stream per session: a reconnecting client replaces its old one.
	CloseEventStream(*Session);

	TSharedRef<FEventStream> Stream = MakeShared<FEventStream>();
	Stream->Queue = MakeShared<TQueue<TArray<uint8>, EQueueMode::Spsc>>();
	Stream->Complete = MakeShared<TAtomic<bool>>(false);
	Stream->LastWriteSeconds = FPlatformTime::Seconds();
	Stream->LastEmptySeconds = Stream->LastWriteSeconds;

	TUniquePtr<FHttpServerResponse> Response = MakeUnique<FHttpServerResponse>();
	Response->Code = EHttpServerResponseCodes::Ok;
	Response->Headers.Add(TEXT("Content-Type"), { TEXT("text/event-stream") });
	Response->Headers.Add(TEXT("Cache-Control"), { TEXT("no-cache") });
	Response->Headers.Add(TEXT("Connection"), { TEXT("keep-alive") });
	Response->Headers.Add(SessionHeader, { Session->Id });
	// Queue-driven body: headers go out with the first chunk, the connection
	// stays open until Complete is set, then closes so the client sees EOF
	// (there is no chunked framing to signal the end otherwise).
	Response->StreamingBodyQueue = Stream->Queue;
	Response->StreamingBodyComplete = Stream->Complete;
	Response->Flags = EHttpServerResponseFlags::CloseAfterWrite;
	OnComplete(MoveTemp(Response));

	Session->Stream = Stream;
	OpenStreams.IncrementExchange();
	Enqueue(*Stream, SseComment(TEXT("connected")));
}

void FYcodeLinkServer::CloseEventStream(FSession& Session)
{
	if (!Session.Stream.IsValid())
	{
		return;
	}
	Session.Stream->Complete->Store(true);
	Session.Stream.Reset();
	OpenStreams.DecrementExchange();
}

bool FYcodeLinkServer::HandleDelete(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	if (!Authorize(Request, OnComplete))
	{
		return true;
	}
	FString SessionHeaderValue;
	const TSharedPtr<FSession> Session = FindSession(Request, &SessionHeaderValue);
	if (!Session.IsValid())
	{
		OnComplete(EmptyResponse(EHttpServerResponseCodes::BadRequest));
		return true;
	}
	CloseEventStream(*Session);
	Sessions.Remove(Session->Id);
	OnComplete(EmptyResponse(EHttpServerResponseCodes::Accepted));
	return true;
}
