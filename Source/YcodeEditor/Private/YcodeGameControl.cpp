// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#include "YcodeGameControl.h"

#include "YcodeLinkLog.h"

#include "Editor.h"
#include "Framework/Commands/InputBindingManager.h"
#include "Kismet2/DebuggerCommands.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	const FName PlayWorldContext(TEXT("PlayWorld"));

	struct FPlayModeEntry
	{
		const TCHAR* Name;
		EPlayModeType Mode;
		FName Command;
	};

	// Names are the wire vocabulary; commands are the PlayWorld context
	// entries the editor toolbar binds. QuickLaunch has no command of its own.
	const FPlayModeEntry PlayModes[] = {
		{ TEXT("Viewport"),      PlayMode_InViewPort,      TEXT("PlayInViewport") },
		{ TEXT("EditorFloating"), PlayMode_InEditorFloating, TEXT("PlayInEditorFloating") },
		{ TEXT("MobilePreview"), PlayMode_InMobilePreview, TEXT("PlayInMobilePreview") },
		{ TEXT("NewProcess"),    PlayMode_InNewProcess,    TEXT("PlayInNewProcess") },
		{ TEXT("VR"),            PlayMode_InVR,            TEXT("PlayInVR") },
		{ TEXT("Simulate"),      PlayMode_Simulate,        TEXT("Simulate") },
		{ TEXT("QuickLaunch"),   PlayMode_QuickLaunch,     NAME_None },
	};

	TArray<FString> PlayModeNames()
	{
		TArray<FString> Names;
		for (const FPlayModeEntry& Entry : PlayModes)
		{
			Names.Add(Entry.Name);
		}
		return Names;
	}

	const FPlayModeEntry* FindPlayMode(const FString& Name)
	{
		for (const FPlayModeEntry& Entry : PlayModes)
		{
			if (Name.Equals(Entry.Name, ESearchCase::IgnoreCase))
			{
				return &Entry;
			}
		}
		return nullptr;
	}

	const FPlayModeEntry* FindPlayMode(EPlayModeType Mode)
	{
		for (const FPlayModeEntry& Entry : PlayModes)
		{
			if (Entry.Mode == Mode)
			{
				return &Entry;
			}
		}
		return nullptr;
	}

	const TCHAR* NetModeName(EPlayNetMode Mode)
	{
		switch (Mode)
		{
		case PIE_ListenServer: return TEXT("ListenServer");
		case PIE_Client: return TEXT("Client");
		default: return TEXT("Standalone");
		}
	}

	bool ParseNetMode(const FString& Name, EPlayNetMode& Out)
	{
		if (Name.Equals(TEXT("Standalone"), ESearchCase::IgnoreCase)) { Out = PIE_Standalone; return true; }
		if (Name.Equals(TEXT("ListenServer"), ESearchCase::IgnoreCase)) { Out = PIE_ListenServer; return true; }
		if (Name.Equals(TEXT("Client"), ESearchCase::IgnoreCase)) { Out = PIE_Client; return true; }
		return false;
	}
}

FYcodeGameControl::FYcodeGameControl()
{
	CacheCommands();
	// The PlayWorld commands are registered by the level editor, which may
	// load after this module; re-cache whenever that context changes.
	CommandsChangedHandle = FBindingContext::CommandsChanged.AddLambda([this](const FBindingContext& Context)
	{
		if (Context.GetContextName() == PlayWorldContext)
		{
			CacheCommands();
		}
	});
	BindEditorDelegates();
	RegisterTools();
	LastSettingsJson = YcodeLinkJson::ToCompactString(PlaySettingsJson());
	if (GEditor && GEditor->PlayWorld)
	{
		PlayState = TEXT("Play");
	}
}

FYcodeGameControl::~FYcodeGameControl()
{
	UnbindEditorDelegates();
	FBindingContext::CommandsChanged.Remove(CommandsChangedHandle);
}

void FYcodeGameControl::CacheCommands()
{
	FInputBindingManager& Bindings = FInputBindingManager::Get();
	auto Cache = [&Bindings](FCommandRef& Ref, const FName& Name)
	{
		Ref.Name = Name;
		Ref.Command = Name.IsNone() ? nullptr : Bindings.FindCommandInContext(PlayWorldContext, Name);
	};
	for (const FPlayModeEntry& Entry : PlayModes)
	{
		FCommandRef& Ref = ModeCommands.FindOrAdd(Entry.Name);
		Cache(Ref, Entry.Command);
	}
	Cache(ResumeCommand, TEXT("ResumePlaySession"));
	Cache(PauseCommand, TEXT("PausePlaySession"));
	Cache(StopCommand, TEXT("StopPlaySession"));
	Cache(StepCommand, TEXT("SingleFrameAdvance"));
}

void FYcodeGameControl::BindEditorDelegates()
{
	BeginPieHandle = FEditorDelegates::BeginPIE.AddLambda([this](bool) { SetPlayState(TEXT("Play")); });
	EndPieHandle = FEditorDelegates::EndPIE.AddLambda([this](bool) { SetPlayState(TEXT("Idle")); });
	PausePieHandle = FEditorDelegates::PausePIE.AddLambda([this](bool) { SetPlayState(TEXT("Pause")); });
	ResumePieHandle = FEditorDelegates::ResumePIE.AddLambda([this](bool) { SetPlayState(TEXT("Play")); });
	SingleStepPieHandle = FEditorDelegates::SingleStepPIE.AddLambda([this](bool)
	{
		SetPlayState(TEXT("Play"));
		SetPlayState(TEXT("Pause"));
	});
	PropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda([this](UObject* Object, FPropertyChangedEvent&)
	{
		if (Object && Object == GetMutableDefault<ULevelEditorPlaySettings>())
		{
			NotifyPlaySettingsIfChanged();
		}
	});
}

void FYcodeGameControl::UnbindEditorDelegates()
{
	FEditorDelegates::BeginPIE.Remove(BeginPieHandle);
	FEditorDelegates::EndPIE.Remove(EndPieHandle);
	FEditorDelegates::PausePIE.Remove(PausePieHandle);
	FEditorDelegates::ResumePIE.Remove(ResumePieHandle);
	FEditorDelegates::SingleStepPIE.Remove(SingleStepPieHandle);
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
}

// ------------------------------------------------------------------ state --

void FYcodeGameControl::SetPlayState(const TCHAR* State)
{
	PlayState = State;
	Notify(TEXT("notifications/ycode/play_state"), PlayStateJson());
}

TSharedRef<FJsonObject> FYcodeGameControl::PlayStateJson() const
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("state"), PlayState);
	Json->SetBoolField(TEXT("isPlaying"), PlayState != TEXT("Idle"));
	Json->SetBoolField(TEXT("isPaused"), PlayState == TEXT("Pause"));
	return Json;
}

TSharedRef<FJsonObject> FYcodeGameControl::PlaySettingsJson() const
{
	const ULevelEditorPlaySettings* Settings = GetDefault<ULevelEditorPlaySettings>();
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	const FPlayModeEntry* Mode = FindPlayMode(Settings->LastExecutedPlayModeType.GetValue());
	Json->SetStringField(TEXT("playMode"), Mode ? Mode->Name : TEXT("Viewport"));
	int32 Clients = 1;
	Settings->GetPlayNumberOfClients(Clients);
	Json->SetNumberField(TEXT("numberOfClients"), Clients);
	EPlayNetMode NetMode = PIE_Standalone;
	Settings->GetPlayNetMode(NetMode);
	Json->SetStringField(TEXT("netMode"), NetModeName(NetMode));
	Json->SetBoolField(TEXT("dedicatedServer"), Settings->bLaunchSeparateServer);
	Json->SetBoolField(TEXT("spawnAtPlayerStart"), Settings->LastExecutedPlayModeLocation == PlayLocation_DefaultPlayerStart);
	bool bRunUnderOneProcess = true;
	Settings->GetRunUnderOneProcess(bRunUnderOneProcess);
	Json->SetBoolField(TEXT("runUnderOneProcess"), bRunUnderOneProcess);
	return Json;
}

void FYcodeGameControl::NotifyPlaySettingsIfChanged()
{
	const TSharedRef<FJsonObject> Settings = PlaySettingsJson();
	const FString Serialized = YcodeLinkJson::ToCompactString(Settings);
	if (Serialized == LastSettingsJson)
	{
		return;
	}
	LastSettingsJson = Serialized;
	Notify(TEXT("notifications/ycode/play_settings"), Settings);
}

FYcodeLinkToolResult FYcodeGameControl::ApplyPlaySettings(const TSharedRef<FJsonObject>& Args)
{
	ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	TArray<FString> Applied;

	if (Args->HasField(TEXT("playMode")))
	{
		const FString ModeName = YcodeLinkJson::GetString(Args, TEXT("playMode"));
		const FPlayModeEntry* Mode = FindPlayMode(ModeName);
		if (!Mode)
		{
			return FYcodeLinkToolResult::Error(FString::Printf(TEXT("Unknown playMode '%s'; expected one of %s"), *ModeName, *FString::Join(PlayModeNames(), TEXT(", "))));
		}
		Settings->LastExecutedPlayModeType = Mode->Mode;
		Applied.Add(TEXT("playMode"));
	}
	if (Args->HasField(TEXT("numberOfClients")))
	{
		Settings->SetPlayNumberOfClients(FMath::Clamp(YcodeLinkJson::GetInt(Args, TEXT("numberOfClients"), 1), 1, 64));
		Applied.Add(TEXT("numberOfClients"));
	}
	if (Args->HasField(TEXT("netMode")))
	{
		EPlayNetMode NetMode = PIE_Standalone;
		const FString Name = YcodeLinkJson::GetString(Args, TEXT("netMode"));
		if (!ParseNetMode(Name, NetMode))
		{
			return FYcodeLinkToolResult::Error(FString::Printf(TEXT("Unknown netMode '%s'; expected Standalone, ListenServer or Client"), *Name));
		}
		Settings->SetPlayNetMode(NetMode);
		Applied.Add(TEXT("netMode"));
	}
	if (Args->HasField(TEXT("dedicatedServer")))
	{
		Settings->bLaunchSeparateServer = YcodeLinkJson::GetBool(Args, TEXT("dedicatedServer"), false);
		Applied.Add(TEXT("dedicatedServer"));
	}
	if (Args->HasField(TEXT("spawnAtPlayerStart")))
	{
		Settings->LastExecutedPlayModeLocation = YcodeLinkJson::GetBool(Args, TEXT("spawnAtPlayerStart"), true)
			? PlayLocation_DefaultPlayerStart
			: PlayLocation_CurrentCameraLocation;
		Applied.Add(TEXT("spawnAtPlayerStart"));
	}
	if (Args->HasField(TEXT("runUnderOneProcess")))
	{
		Settings->SetRunUnderOneProcess(YcodeLinkJson::GetBool(Args, TEXT("runUnderOneProcess"), true));
		Applied.Add(TEXT("runUnderOneProcess"));
	}

	if (!Applied.IsEmpty())
	{
		Settings->PostEditChange();
		Settings->SaveConfig();
		NotifyPlaySettingsIfChanged();
	}
	TSharedRef<FJsonObject> Result = PlaySettingsJson();
	TArray<TSharedPtr<FJsonValue>> AppliedValues;
	for (const FString& Field : Applied)
	{
		AppliedValues.Add(MakeShared<FJsonValueString>(Field));
	}
	Result->SetArrayField(TEXT("applied"), AppliedValues);
	return FYcodeLinkToolResult::Object(Result);
}

// --------------------------------------------------------------- commands --

FYcodeLinkToolResult FYcodeGameControl::RunCommand(const FCommandRef& Command) const
{
	if (!Command.Command.IsValid())
	{
		return FYcodeLinkToolResult::Error(FString::Printf(TEXT("Command '%s' is not registered in Unreal Editor"), *Command.Name.ToString()));
	}
	const TSharedPtr<FUICommandList> Actions = FPlayWorldCommands::GlobalPlayWorldActions;
	if (!Actions.IsValid() || !Actions->TryExecuteAction(Command.Command.ToSharedRef()))
	{
		return FYcodeLinkToolResult::Error(FString::Printf(TEXT("Command '%s' was rejected by Unreal Editor (not available in the current state)"), *Command.Name.ToString()));
	}
	TSharedRef<FJsonObject> Result = PlayStateJson();
	Result->SetStringField(TEXT("command"), Command.Name.ToString());
	return FYcodeLinkToolResult::Object(Result);
}

FYcodeLinkToolResult FYcodeGameControl::Play(const FString& ModeName) const
{
	const FPlayModeEntry* Mode = nullptr;
	if (!ModeName.IsEmpty())
	{
		Mode = FindPlayMode(ModeName);
		if (!Mode)
		{
			return FYcodeLinkToolResult::Error(FString::Printf(TEXT("Unknown play mode '%s'; expected one of %s"), *ModeName, *FString::Join(PlayModeNames(), TEXT(", "))));
		}
	}
	else
	{
		Mode = FindPlayMode(GetDefault<ULevelEditorPlaySettings>()->LastExecutedPlayModeType.GetValue());
		if (!Mode)
		{
			Mode = &PlayModes[0];
		}
	}
	const FCommandRef* Command = ModeCommands.Find(Mode->Name);
	if (!Command || Command->Name.IsNone())
	{
		return FYcodeLinkToolResult::Error(FString::Printf(TEXT("Play mode '%s' cannot be started from the IDE; pick it in the editor toolbar"), Mode->Name));
	}
	return RunCommand(*Command);
}

// ------------------------------------------------------------------ tools --

void FYcodeGameControl::RegisterTools()
{
	Add(TEXT("ue_play_state"), TEXT("Current Play-In-Editor state: Idle, Play or Pause. Changes are pushed as notifications/ycode/play_state."),
		FYcodeLinkSchema().Build(), true,
		[this](const TSharedRef<FJsonObject>&, const FYcodeLinkToolCallback& Done)
		{
			Done(FYcodeLinkToolResult::Object(PlayStateJson()));
		});

	Add(TEXT("ue_play"), TEXT("Start a Play-In-Editor session using the play mode last used in the editor, or the given mode."),
		FYcodeLinkSchema().Enum(TEXT("mode"), TEXT("Play mode override"), PlayModeNames()).Build(), false,
		[this](const TSharedRef<FJsonObject>& Args, const FYcodeLinkToolCallback& Done)
		{
			Done(Play(YcodeLinkJson::GetString(Args, TEXT("mode"))));
		});

	Add(TEXT("ue_pause"), TEXT("Pause the running Play-In-Editor session."), FYcodeLinkSchema().Build(), false,
		[this](const TSharedRef<FJsonObject>&, const FYcodeLinkToolCallback& Done) { Done(RunCommand(PauseCommand)); });

	Add(TEXT("ue_resume"), TEXT("Resume a paused Play-In-Editor session."), FYcodeLinkSchema().Build(), false,
		[this](const TSharedRef<FJsonObject>&, const FYcodeLinkToolCallback& Done) { Done(RunCommand(ResumeCommand)); });

	Add(TEXT("ue_stop"), TEXT("Stop the Play-In-Editor session."), FYcodeLinkSchema().Build(), false,
		[this](const TSharedRef<FJsonObject>&, const FYcodeLinkToolCallback& Done) { Done(RunCommand(StopCommand)); });

	Add(TEXT("ue_frame_skip"), TEXT("Advance the paused Play-In-Editor session by a single frame."), FYcodeLinkSchema().Build(), false,
		[this](const TSharedRef<FJsonObject>&, const FYcodeLinkToolCallback& Done) { Done(RunCommand(StepCommand)); });

	Add(TEXT("ue_get_play_settings"), TEXT("Read the Play-In-Editor settings: play mode, number of clients, net mode, dedicated server, spawn location, run under one process."),
		FYcodeLinkSchema().Build(), true,
		[this](const TSharedRef<FJsonObject>&, const FYcodeLinkToolCallback& Done)
		{
			Done(FYcodeLinkToolResult::Object(PlaySettingsJson()));
		});

	Add(TEXT("ue_set_play_settings"), TEXT("Change Play-In-Editor settings. Only the fields given are changed; the result is the full settings object plus the list of applied fields."),
		FYcodeLinkSchema()
			.Enum(TEXT("playMode"), TEXT("Play mode used by ue_play and the editor Play button"), PlayModeNames())
			.Integer(TEXT("numberOfClients"), TEXT("Number of clients (1-64)"))
			.Enum(TEXT("netMode"), TEXT("Network mode"), { TEXT("Standalone"), TEXT("ListenServer"), TEXT("Client") })
			.Boolean(TEXT("dedicatedServer"), TEXT("Launch a separate dedicated server"))
			.Boolean(TEXT("spawnAtPlayerStart"), TEXT("true: spawn at the default player start; false: at the current camera location"))
			.Boolean(TEXT("runUnderOneProcess"), TEXT("Run all clients in one process"))
			.Build(), false,
		[this](const TSharedRef<FJsonObject>& Args, const FYcodeLinkToolCallback& Done)
		{
			Done(ApplyPlaySettings(Args));
		});
}
