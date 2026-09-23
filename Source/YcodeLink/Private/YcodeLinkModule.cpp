// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#include "YcodeLinkModule.h"

#include "YcodeLinkFile.h"
#include "YcodeLinkLog.h"
#include "YcodeLinkServer.h"

#include "Engine/Engine.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/Regex.h"
#include "Logging/LogVerbosity.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersion.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogYcodeLink);

IMPLEMENT_MODULE(FYcodeLinkModule, YcodeLink);

namespace
{
	constexpr const TCHAR* PluginName = TEXT("YcodeLink");
	constexpr const TCHAR* LinkRoutePath = TEXT("/mcp");
	constexpr int32 MaxEntriesPerNotification = 200;
	// UModelContextProtocolSettings (Engine/Plugins/Experimental/ModelContextProtocol)
	// is config=EditorPerProjectUserSettings; reading it through GConfig keeps
	// this plugin free of a compile-time dependency on that NoRedist plugin.
	constexpr const TCHAR* EpicMcpSettingsSection = TEXT("/Script/ModelContextProtocolEngine.ModelContextProtocolSettings");
	constexpr const TCHAR* EpicMcpModuleName = TEXT("ModelContextProtocol");

	FString ProjectDisplayName()
	{
		const FString Name = FApp::GetProjectName();
		return Name.IsEmpty() ? FString(TEXT("<ENGINE>")) : Name;
	}

	// [begin, end) character ranges of regex matches, as the IDE needs them to
	// turn Blueprint paths and C++ methods in a log line into links.
	void AppendRanges(const FRegexPattern& Pattern, const FString& Text, bool bBlueprintPathsOnly, TArray<TSharedPtr<FJsonValue>>& Out)
	{
		FRegexMatcher Matcher(Pattern, Text);
		while (Matcher.FindNext())
		{
			const int32 Begin = Matcher.GetMatchBeginning();
			const int32 End = Matcher.GetMatchEnding();
			if (bBlueprintPathsOnly && !FPackageName::IsValidObjectPath(Text.Mid(Begin, End - Begin)))
			{
				continue;
			}
			TArray<TSharedPtr<FJsonValue>> Range;
			Range.Add(MakeShared<FJsonValueNumber>(Begin));
			Range.Add(MakeShared<FJsonValueNumber>(End));
			Out.Add(MakeShared<FJsonValueArray>(Range));
		}
	}

	TSharedRef<FJsonObject> LogEntryToJson(const FYcodeLinkLogEntry& Entry)
	{
		static const FRegexPattern PathPattern(TEXT("(/[\\w\\.]+)+"));
		static const FRegexPattern MethodPattern(TEXT("[0-9a-z_A-Z]+::~?[0-9a-z_A-Z]+"));

		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("seq"), static_cast<double>(Entry.Seq));
		Json->SetNumberField(TEXT("time"), Entry.UnixTime);
		Json->SetStringField(TEXT("verbosity"), ::ToString(Entry.Verbosity));
		Json->SetStringField(TEXT("category"), Entry.Category.ToString());
		Json->SetStringField(TEXT("text"), Entry.Text);
		TArray<TSharedPtr<FJsonValue>> BlueprintPaths;
		AppendRanges(PathPattern, Entry.Text, /*bBlueprintPathsOnly=*/true, BlueprintPaths);
		Json->SetArrayField(TEXT("bpPaths"), BlueprintPaths);
		TArray<TSharedPtr<FJsonValue>> Methods;
		AppendRanges(MethodPattern, Entry.Text, /*bBlueprintPathsOnly=*/false, Methods);
		Json->SetArrayField(TEXT("methods"), Methods);
		return Json;
	}

	TSharedRef<FJsonObject> LogEntriesToJson(const TArray<FYcodeLinkLogEntry>& Entries, int32 Start, int32 Count, uint64 LastSeq)
	{
		TArray<TSharedPtr<FJsonValue>> List;
		List.Reserve(Count);
		for (int32 Index = Start; Index < Start + Count && Index < Entries.Num(); ++Index)
		{
			List.Add(MakeShared<FJsonValueObject>(LogEntryToJson(Entries[Index])));
		}
		TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetArrayField(TEXT("entries"), List);
		Params->SetNumberField(TEXT("lastSeq"), static_cast<double>(LastSeq));
		return Params;
	}
}

// ---------------------------------------------------------------- module --

void FYcodeLinkModule::StartupModule()
{
	LogCapture.Install();

	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName))
	{
		PluginVersion = Plugin->GetDescriptor().VersionName;
	}

	Server = MakeShared<FYcodeLinkServer>();
	Server->SetServerInfo(TEXT("ycode-link"), PluginVersion,
		TEXT("Tools prefixed ue_ act on the running Unreal Editor that the Ycode IDE is attached to: "
		     "read its output log, control Play-In-Editor, open Blueprints, trigger Live Coding, "
		     "run editor Python, search assets, take screenshots, drive the viewport camera and spawn actors. "
		     "Results are JSON; check `isError` and the `error` field before assuming success."));
	RegisterCoreTools();

	if (IsRunningCommandlet())
	{
		return;
	}
	// The HTTP server needs an initialized engine; a plugin enabled from the
	// Plugins browser starts after that point and cannot wait for the delegate.
	if (GEngine)
	{
		StartServer();
	}
	else
	{
		PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FYcodeLinkModule::StartServer);
	}
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FYcodeLinkModule::Tick));
}

void FYcodeLinkModule::ShutdownModule()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}
	StopServer();
	Server.Reset();
	LogCapture.Uninstall();
}

void FYcodeLinkModule::StartServer()
{
	if (!Server.IsValid() || Server->IsRunning())
	{
		return;
	}
	uint32 PreferredPort = 0;
	FParse::Value(FCommandLine::Get(), TEXT("YcodeLinkPort="), PreferredPort);
	if (!Server->Start(LinkRoutePath, PreferredPort))
	{
		return;
	}

	FYcodeLinkFileInfo Info;
	Info.ProcessId = FPlatformProcess::GetCurrentProcessId();
	Info.Port = Server->GetPort();
	Info.Url = Server->GetUrl();
	Info.Token = Server->GetToken();
	Info.ProjectName = ProjectDisplayName();
	Info.ProjectFile = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	Info.ExecutableName = FPlatformProcess::ExecutableName(/*bRemoveExtension=*/false);
	Info.ExecutablePath = FPlatformProcess::ExecutablePath();
	Info.EngineVersion = FEngineVersion::Current().ToString();
	Info.PluginVersion = PluginVersion;
	Info.ProtocolVersion = FYcodeLinkServer::LatestProtocolVersion();
	Info.Started = FDateTime::UtcNow();
	YcodeLinkFile::Write(Info);
	UE_LOG(LogYcodeLink, Log, TEXT("Editor link file: %s"), *YcodeLinkFile::GetPath());
}

void FYcodeLinkModule::StopServer()
{
	YcodeLinkFile::Remove();
	if (Server.IsValid())
	{
		Server->Stop();
	}
}

bool FYcodeLinkModule::Tick(float)
{
	if (Server.IsValid())
	{
		Server->Tick();
		StreamCapturedLog();
	}
	return true;
}

void FYcodeLinkModule::StreamCapturedLog()
{
	if (!Server->HasEventStreams())
	{
		// Nobody listening: the ring keeps the backlog for ue_log_tail.
		LogCapture.DiscardPending();
		return;
	}
	TArray<FYcodeLinkLogEntry> Entries;
	LogCapture.DrainPending(Entries);
	const uint64 LastSeq = LogCapture.LastSeq();
	for (int32 Start = 0; Start < Entries.Num(); Start += MaxEntriesPerNotification)
	{
		Server->Notify(TEXT("notifications/ycode/log"),
			LogEntriesToJson(Entries, Start, MaxEntriesPerNotification, LastSeq));
	}
}

// ------------------------------------------------------------- interface --

void FYcodeLinkModule::RegisterTool(const FYcodeLinkTool& Tool)
{
	if (Server.IsValid())
	{
		Server->RegisterTool(Tool);
	}
}

void FYcodeLinkModule::UnregisterTool(const FString& ToolName)
{
	if (Server.IsValid())
	{
		Server->UnregisterTool(ToolName);
	}
}

void FYcodeLinkModule::Notify(const FString& Method, const TSharedPtr<FJsonObject>& Params)
{
	if (Server.IsValid())
	{
		Server->Notify(Method, Params);
	}
}

bool FYcodeLinkModule::IsServerRunning() const
{
	return Server.IsValid() && Server->IsRunning();
}

FString FYcodeLinkModule::GetServerUrl() const
{
	return Server.IsValid() ? Server->GetUrl() : FString();
}

FString FYcodeLinkModule::GetLinkFilePath() const
{
	return YcodeLinkFile::GetPath();
}

// ------------------------------------------------------------ core tools --

TSharedRef<FJsonObject> FYcodeLinkModule::BuildEpicMcpStatus() const
{
	int32 Port = 8000;
	FString UrlPath = TEXT("/mcp");
	bool bAutoStart = false;
	if (GConfig)
	{
		GConfig->GetInt(EpicMcpSettingsSection, TEXT("ServerPortNumber"), Port, GEditorPerProjectIni);
		GConfig->GetString(EpicMcpSettingsSection, TEXT("ServerUrlPath"), UrlPath, GEditorPerProjectIni);
		GConfig->GetBool(EpicMcpSettingsSection, TEXT("bAutoStartServer"), bAutoStart, GEditorPerProjectIni);
	}
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(EpicMcpModuleName);

	TSharedRef<FJsonObject> Status = MakeShared<FJsonObject>();
	Status->SetBoolField(TEXT("pluginAvailable"), Plugin.IsValid());
	Status->SetBoolField(TEXT("pluginEnabled"), Plugin.IsValid() && Plugin->IsEnabled());
	Status->SetBoolField(TEXT("moduleLoaded"), FModuleManager::Get().IsModuleLoaded(EpicMcpModuleName));
	Status->SetNumberField(TEXT("port"), Port);
	Status->SetStringField(TEXT("urlPath"), UrlPath);
	Status->SetBoolField(TEXT("autoStart"), bAutoStart);
	Status->SetStringField(TEXT("url"), FString::Printf(TEXT("http://127.0.0.1:%d%s"), Port, *UrlPath));
	return Status;
}

TSharedRef<FJsonObject> FYcodeLinkModule::BuildEditorInfo() const
{
	TSharedRef<FJsonObject> Info = MakeShared<FJsonObject>();
	Info->SetStringField(TEXT("projectName"), ProjectDisplayName());
	Info->SetStringField(TEXT("projectFile"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
	Info->SetStringField(TEXT("projectDir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
	Info->SetStringField(TEXT("engineDir"), FPaths::ConvertRelativePathToFull(FPaths::EngineDir()));
	Info->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());
	Info->SetStringField(TEXT("executable"), FPlatformProcess::ExecutableName(false));
	Info->SetStringField(TEXT("executablePath"), FPlatformProcess::ExecutablePath());
	Info->SetNumberField(TEXT("pid"), FPlatformProcess::GetCurrentProcessId());
	Info->SetStringField(TEXT("pluginVersion"), PluginVersion);
	Info->SetStringField(TEXT("serverUrl"), GetServerUrl());
	Info->SetStringField(TEXT("linkFile"), GetLinkFilePath());
	if (RegisteredIde.IsSet())
	{
		TSharedRef<FJsonObject> Ide = MakeShared<FJsonObject>();
		Ide->SetStringField(TEXT("name"), RegisteredIde->Name);
		Ide->SetStringField(TEXT("version"), RegisteredIde->Version);
		Ide->SetStringField(TEXT("executable"), RegisteredIde->ExecutablePath);
		Ide->SetNumberField(TEXT("pid"), RegisteredIde->ProcessId);
		Info->SetObjectField(TEXT("ide"), Ide);
	}
	Info->SetObjectField(TEXT("epicMcp"), BuildEpicMcpStatus());
	return Info;
}

void FYcodeLinkModule::RegisterCoreTools()
{
	FYcodeLinkTool EditorInfo;
	EditorInfo.Name = TEXT("ue_editor_info");
	EditorInfo.Description = TEXT("Describe the connected Unreal Editor: project, engine version, process id, plugin version, the IDE registered with it, and the state of the engine's own MCP server (ModelContextProtocol plugin).");
	EditorInfo.InputSchema = FYcodeLinkSchema().Build();
	EditorInfo.bReadOnly = true;
	EditorInfo.Handler = [this](const TSharedRef<FJsonObject>&, const FYcodeLinkToolCallback& Done)
	{
		Done(FYcodeLinkToolResult::Object(BuildEditorInfo()));
	};
	RegisterTool(EditorInfo);

	FYcodeLinkTool IdeRegister;
	IdeRegister.Name = TEXT("ue_ide_register");
	IdeRegister.Description = TEXT("Announce the IDE that owns this session so the editor opens source files in it (Open in Ycode).");
	IdeRegister.InputSchema = FYcodeLinkSchema()
		.String(TEXT("name"), TEXT("IDE display name, e.g. Ycode"), true)
		.String(TEXT("version"), TEXT("IDE version string"))
		.String(TEXT("executable"), TEXT("Absolute path of the IDE executable"), true)
		.Integer(TEXT("pid"), TEXT("IDE process id"))
		.Build();
	IdeRegister.Handler = [this](const TSharedRef<FJsonObject>& Args, const FYcodeLinkToolCallback& Done)
	{
		FYcodeLinkIdeInfo Ide;
		Ide.Name = YcodeLinkJson::GetString(Args, TEXT("name"));
		Ide.Version = YcodeLinkJson::GetString(Args, TEXT("version"));
		Ide.ExecutablePath = YcodeLinkJson::GetString(Args, TEXT("executable"));
		Ide.ProcessId = static_cast<uint32>(YcodeLinkJson::GetInt(Args, TEXT("pid"), 0));
		if (Ide.Name.IsEmpty() || Ide.ExecutablePath.IsEmpty())
		{
			Done(FYcodeLinkToolResult::Error(TEXT("name and executable are required")));
			return;
		}
		RegisteredIde = Ide;
		IdeRegistered.Broadcast(Ide);
		UE_LOG(LogYcodeLink, Log, TEXT("IDE registered: %s %s (%s, pid %u)"), *Ide.Name, *Ide.Version, *Ide.ExecutablePath, Ide.ProcessId);
		Done(FYcodeLinkToolResult::Object(BuildEditorInfo()));
	};
	RegisterTool(IdeRegister);

	FYcodeLinkTool LogTail;
	LogTail.Name = TEXT("ue_log_tail");
	LogTail.Description = TEXT("Return recent Unreal Editor output-log lines (newest last). Each entry has a monotonically increasing seq; pass afterSeq to fetch only lines newer than the last one seen. The live stream is delivered as notifications/ycode/log on the event stream.");
	LogTail.InputSchema = FYcodeLinkSchema()
		.Integer(TEXT("afterSeq"), TEXT("Only lines with seq greater than this (0 = from the oldest retained line)"))
		.Integer(TEXT("limit"), TEXT("Maximum number of lines, default 500, at most 5000"))
		.String(TEXT("category"), TEXT("Only this log category, e.g. LogBlueprint"))
		.Enum(TEXT("minVerbosity"), TEXT("Only lines at least this severe"), { TEXT("Fatal"), TEXT("Error"), TEXT("Warning"), TEXT("Display"), TEXT("Log"), TEXT("Verbose"), TEXT("VeryVerbose") })
		.Build();
	LogTail.bReadOnly = true;
	LogTail.Handler = [this](const TSharedRef<FJsonObject>& Args, const FYcodeLinkToolCallback& Done)
	{
		const uint64 AfterSeq = static_cast<uint64>(FMath::Max(0.0, YcodeLinkJson::GetNumber(Args, TEXT("afterSeq"), 0.0)));
		const int32 Limit = FMath::Clamp(YcodeLinkJson::GetInt(Args, TEXT("limit"), 500), 1, 5000);
		const FString Category = YcodeLinkJson::GetString(Args, TEXT("category"));
		const FString VerbosityName = YcodeLinkJson::GetString(Args, TEXT("minVerbosity"));
		const ELogVerbosity::Type MinVerbosity = VerbosityName.IsEmpty() ? ELogVerbosity::NoLogging : ParseLogVerbosityFromString(VerbosityName);
		const TArray<FYcodeLinkLogEntry> Entries = LogCapture.Tail(AfterSeq, Limit, Category, MinVerbosity);
		Done(FYcodeLinkToolResult::Object(LogEntriesToJson(Entries, 0, Entries.Num(), LogCapture.LastSeq())));
	};
	RegisterTool(LogTail);

	FYcodeLinkTool EpicMcp;
	EpicMcp.Name = TEXT("ue_epic_mcp");
	EpicMcp.Description = TEXT("Query, start or stop the Unreal Engine MCP server (Epic's ModelContextProtocol plugin, which exposes the editor Toolset Registry: assets, Blueprints, materials, actors, PIE ...). The status carries its configured URL so an MCP client can connect to it directly.");
	EpicMcp.InputSchema = FYcodeLinkSchema()
		.Enum(TEXT("action"), TEXT("status (default), start or stop"), { TEXT("status"), TEXT("start"), TEXT("stop") })
		.Integer(TEXT("port"), TEXT("Port for start; defaults to the configured ServerPortNumber"))
		.Build();
	EpicMcp.Handler = [this](const TSharedRef<FJsonObject>& Args, const FYcodeLinkToolCallback& Done)
	{
		const FString Action = YcodeLinkJson::GetString(Args, TEXT("action"), TEXT("status"));
		if (Action != TEXT("status"))
		{
			if (!FModuleManager::Get().IsModuleLoaded(EpicMcpModuleName))
			{
				Done(FYcodeLinkToolResult::Error(TEXT("The ModelContextProtocol plugin is not enabled in this project; enable it in Edit > Plugins (Unreal MCP) and restart the editor.")));
				return;
			}
			if (!GEngine)
			{
				Done(FYcodeLinkToolResult::Error(TEXT("Engine is not initialized")));
				return;
			}
			FString Command = Action == TEXT("start") ? TEXT("ModelContextProtocol.StartServer") : TEXT("ModelContextProtocol.StopServer");
			const int32 Port = YcodeLinkJson::GetInt(Args, TEXT("port"), 0);
			if (Action == TEXT("start") && Port > 0)
			{
				Command += FString::Printf(TEXT(" %d"), Port);
			}
			GEngine->Exec(nullptr, *Command);
		}
		Done(FYcodeLinkToolResult::Object(BuildEpicMcpStatus()));
	};
	RegisterTool(EpicMcp);
}
