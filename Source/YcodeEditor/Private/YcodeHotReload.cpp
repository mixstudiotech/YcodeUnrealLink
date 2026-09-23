// Copyright Pix Philosophy (HK) Limited.

#include "YcodeHotReload.h"

#include "YcodeLinkLog.h"

#include "Modules/ModuleManager.h"
#if WITH_LIVE_CODING
#include "ILiveCodingModule.h"
#endif
#if WITH_HOT_RELOAD
#include "Misc/HotReloadInterface.h"
#endif

namespace
{
	constexpr float PollIntervalSeconds = 0.25f;

#if WITH_LIVE_CODING
	ILiveCodingModule* LiveCoding()
	{
		return FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
	}

	const TCHAR* CompileResultName(ELiveCodingCompileResult Result)
	{
		switch (Result)
		{
		case ELiveCodingCompileResult::Success: return TEXT("Success");
		case ELiveCodingCompileResult::NoChanges: return TEXT("NoChanges");
		case ELiveCodingCompileResult::InProgress: return TEXT("InProgress");
		case ELiveCodingCompileResult::CompileStillActive: return TEXT("CompileStillActive");
		case ELiveCodingCompileResult::NotStarted: return TEXT("NotStarted");
		case ELiveCodingCompileResult::Failure: return TEXT("Failure");
		case ELiveCodingCompileResult::Cancelled: return TEXT("Cancelled");
		}
		return TEXT("Unknown");
	}
#endif

#if WITH_HOT_RELOAD
	IHotReloadInterface* HotReload()
	{
		static const FName HotReloadModuleName(TEXT("HotReload"));
		return FModuleManager::GetModulePtr<IHotReloadInterface>(HotReloadModuleName);
	}
#endif
}

FYcodeHotReload::FYcodeHotReload()
{
	Add(TEXT("ue_hot_reload_state"), TEXT("Whether code can be reloaded into the running editor (Live Coding when enabled, Hot Reload otherwise) and whether a reload is compiling right now. Includes the Live Coding details. Changes are pushed as notifications/ycode/hot_reload."),
		FYcodeLinkSchema().Build(), true,
		[this](const TSharedRef<FJsonObject>&, const FYcodeLinkToolCallback& Done)
		{
			Done(FYcodeLinkToolResult::Object(StateJson()));
		});

	Add(TEXT("ue_hot_reload"), TEXT("Reload changed C++ into the running editor: a Live Coding compile when Live Coding is enabled, otherwise a Hot Reload from the editor."),
		FYcodeLinkSchema().Build(), false,
		[this](const TSharedRef<FJsonObject>&, const FYcodeLinkToolCallback& Done)
		{
			Done(Trigger());
		});

	Add(TEXT("ue_live_coding_state"), TEXT("Live Coding state: enabled by default, enabled for this session, can be enabled, started, compiling."),
		FYcodeLinkSchema().Build(), true,
		[this](const TSharedRef<FJsonObject>&, const FYcodeLinkToolCallback& Done)
		{
			Done(FYcodeLinkToolResult::Object(StateJson()));
		});

	Add(TEXT("ue_live_coding_enable"), TEXT("Enable or disable Live Coding by default (persisted setting) and/or for the current session."),
		FYcodeLinkSchema()
			.Boolean(TEXT("byDefault"), TEXT("Persisted Live Coding setting"))
			.Boolean(TEXT("forSession"), TEXT("Enable for this editor session (starts the Live Coding console)"))
			.Build(), false,
		[this](const TSharedRef<FJsonObject>& Args, const FYcodeLinkToolCallback& Done)
		{
			Done(LiveCodingEnable(Args));
		});

	Add(TEXT("ue_live_coding_compile"), TEXT("Start a Live Coding compile. With wait=true the call returns after the patch completed and reports the compile result."),
		FYcodeLinkSchema().Boolean(TEXT("wait"), TEXT("Block until the compile finishes")).Build(), false,
		[this](const TSharedRef<FJsonObject>& Args, const FYcodeLinkToolCallback& Done)
		{
			Done(LiveCodingCompile(YcodeLinkJson::GetBool(Args, TEXT("wait"), false)));
		});

	LastStateJson = YcodeLinkJson::ToCompactString(StateJson());
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FYcodeHotReload::Tick), PollIntervalSeconds);
#if WITH_LIVE_CODING
	if (ILiveCodingModule* Module = LiveCoding())
	{
		PatchCompleteHandle = Module->GetOnPatchCompleteDelegate().AddLambda([this]()
		{
			Notify(TEXT("notifications/ycode/live_coding_patch_complete"), MakeShared<FJsonObject>());
		});
	}
#endif
}

FYcodeHotReload::~FYcodeHotReload()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	}
#if WITH_LIVE_CODING
	if (PatchCompleteHandle.IsValid())
	{
		if (ILiveCodingModule* Module = LiveCoding())
		{
			Module->GetOnPatchCompleteDelegate().Remove(PatchCompleteHandle);
		}
	}
#endif
}

bool FYcodeHotReload::Tick(float)
{
	const TSharedRef<FJsonObject> State = StateJson();
	const FString Serialized = YcodeLinkJson::ToCompactString(State);
	if (Serialized != LastStateJson)
	{
		LastStateJson = Serialized;
		Notify(TEXT("notifications/ycode/hot_reload"), State);
	}
	return true;
}

TSharedRef<FJsonObject> FYcodeHotReload::StateJson() const
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	bool bAvailable = false;
	bool bCompiling = false;
	FString Backend = TEXT("None");

	TSharedRef<FJsonObject> Live = MakeShared<FJsonObject>();
	bool bLiveCodingSupported = false;
#if WITH_LIVE_CODING
	bLiveCodingSupported = true;
	if (const ILiveCodingModule* Module = LiveCoding())
	{
		Live->SetBoolField(TEXT("enabledByDefault"), Module->IsEnabledByDefault());
		Live->SetBoolField(TEXT("enabledForSession"), Module->IsEnabledForSession());
		Live->SetBoolField(TEXT("canEnableForSession"), Module->CanEnableForSession());
		Live->SetBoolField(TEXT("hasStarted"), Module->HasStarted());
		Live->SetBoolField(TEXT("compiling"), Module->IsCompiling());
		Live->SetBoolField(TEXT("automaticallyCompileNewClasses"), Module->AutomaticallyCompileNewClasses());
		if (Module->IsEnabledByDefault())
		{
			bAvailable = true;
			bCompiling = Module->IsCompiling();
			Backend = TEXT("LiveCoding");
		}
	}
#endif
	Live->SetBoolField(TEXT("supported"), bLiveCodingSupported);
	Json->SetObjectField(TEXT("liveCoding"), Live);

	if (Backend == TEXT("None"))
	{
#if WITH_HOT_RELOAD
		if (const IHotReloadInterface* Module = HotReload())
		{
			bAvailable = true;
			bCompiling = Module->IsCurrentlyCompiling();
			Backend = TEXT("HotReload");
		}
#endif
	}
	Json->SetBoolField(TEXT("available"), bAvailable);
	Json->SetBoolField(TEXT("compiling"), bCompiling);
	Json->SetStringField(TEXT("backend"), Backend);
	return Json;
}

FYcodeLinkToolResult FYcodeHotReload::Trigger() const
{
#if WITH_LIVE_CODING
	if (ILiveCodingModule* Module = LiveCoding())
	{
		if (Module->IsEnabledByDefault())
		{
			Module->EnableForSession(true);
			if (!Module->IsEnabledForSession())
			{
				return FYcodeLinkToolResult::Error(FString::Printf(TEXT("Live Coding could not be enabled for this session: %s"), *Module->GetEnableErrorText().ToString()));
			}
			Module->Compile();
			TSharedRef<FJsonObject> Result = StateJson();
			Result->SetStringField(TEXT("triggered"), TEXT("LiveCoding"));
			return FYcodeLinkToolResult::Object(Result);
		}
	}
#endif
#if WITH_HOT_RELOAD
	if (IHotReloadInterface* Module = HotReload())
	{
		if (Module->IsCurrentlyCompiling())
		{
			return FYcodeLinkToolResult::Error(TEXT("A Hot Reload is already compiling"));
		}
		Module->DoHotReloadFromEditor(EHotReloadFlags::None);
		TSharedRef<FJsonObject> Result = StateJson();
		Result->SetStringField(TEXT("triggered"), TEXT("HotReload"));
		return FYcodeLinkToolResult::Object(Result);
	}
#endif
	return FYcodeLinkToolResult::Error(TEXT("Neither Live Coding nor Hot Reload is available in this editor"));
}

FYcodeLinkToolResult FYcodeHotReload::LiveCodingEnable(const TSharedRef<FJsonObject>& Args) const
{
#if WITH_LIVE_CODING
	ILiveCodingModule* Module = LiveCoding();
	if (!Module)
	{
		return FYcodeLinkToolResult::Error(TEXT("The LiveCoding module is not loaded"));
	}
	if (Args->HasField(TEXT("byDefault")))
	{
		Module->EnableByDefault(YcodeLinkJson::GetBool(Args, TEXT("byDefault"), true));
	}
	if (Args->HasField(TEXT("forSession")))
	{
		const bool bEnable = YcodeLinkJson::GetBool(Args, TEXT("forSession"), true);
		Module->EnableForSession(bEnable);
		if (bEnable && !Module->IsEnabledForSession())
		{
			return FYcodeLinkToolResult::Error(FString::Printf(TEXT("Live Coding could not be enabled for this session: %s"), *Module->GetEnableErrorText().ToString()));
		}
	}
	return FYcodeLinkToolResult::Object(StateJson());
#else
	return FYcodeLinkToolResult::Error(TEXT("Live Coding is not supported on this platform"));
#endif
}

FYcodeLinkToolResult FYcodeHotReload::LiveCodingCompile(bool bWait) const
{
#if WITH_LIVE_CODING
	ILiveCodingModule* Module = LiveCoding();
	if (!Module)
	{
		return FYcodeLinkToolResult::Error(TEXT("The LiveCoding module is not loaded"));
	}
	if (!Module->IsEnabledForSession())
	{
		Module->EnableForSession(true);
		if (!Module->IsEnabledForSession())
		{
			return FYcodeLinkToolResult::Error(FString::Printf(TEXT("Live Coding is not enabled: %s"), *Module->GetEnableErrorText().ToString()));
		}
	}
	ELiveCodingCompileResult CompileResult = ELiveCodingCompileResult::InProgress;
	const bool bOk = Module->Compile(bWait ? ELiveCodingCompileFlags::WaitForCompletion : ELiveCodingCompileFlags::None, &CompileResult);
	TSharedRef<FJsonObject> Result = StateJson();
	Result->SetStringField(TEXT("result"), CompileResultName(CompileResult));
	return FYcodeLinkToolResult::Object(Result, !bOk && CompileResult == ELiveCodingCompileResult::Failure);
#else
	return FYcodeLinkToolResult::Error(TEXT("Live Coding is not supported on this platform"));
#endif
}
