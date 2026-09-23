// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#include "YcodeAgentToolSets.h"

#include "Engine/Engine.h"
#include "IPythonScriptPlugin.h"
#include "PythonScriptTypes.h"
#include "UObject/GarbageCollection.h"

namespace
{
	constexpr int32 MaxOutputChars = 10000;

	FString CapString(const FString& Text)
	{
		if (Text.Len() <= MaxOutputChars)
		{
			return Text;
		}
		const int32 Half = MaxOutputChars / 2;
		return Text.Left(Half) + TEXT("\n[... output truncated ...]\n") + Text.Right(Half);
	}

	FString JoinLogOutput(const TArray<FPythonLogOutputEntry>& Entries)
	{
		TArray<FString> Lines;
		Lines.Reserve(Entries.Num());
		for (const FPythonLogOutputEntry& Entry : Entries)
		{
			Lines.Add(Entry.Output);
		}
		return FString::Join(Lines, TEXT("\n"));
	}

	// isolated: Private file scope, cleared afterwards so UObject references the
	// script created (loaded Blueprints, assets) are released before the next GC
	// tick. Non-isolated batch steps share __main__ across calls.
	void ConfigureCommand(FPythonCommandEx& Command, const FString& Script, bool bIsolated)
	{
		Command.Command = Script;
		Command.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
		Command.FileExecutionScope = bIsolated ? EPythonFileExecutionScope::Private : EPythonFileExecutionScope::Public;
	}

	// The Private scope is one persistent dict shared by every Private-scoped
	// call, not a fresh namespace; clear the user names so refs are released.
	void ClearPrivateScope(IPythonScriptPlugin& Python)
	{
		FPythonCommandEx Clear;
		Clear.Command = TEXT("[globals().pop(k) for k in list(globals()) if not k.startswith('__')]");
		Clear.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
		Clear.FileExecutionScope = EPythonFileExecutionScope::Private;
		Python.ExecPythonCommandEx(Clear);
	}

	// Never collect synchronously here: this runs inside an HTTP tick, which is
	// not a GC-safe point (a Live Coding re-instance may still be fixing up).
	// ForceGarbageCollection only raises a flag serviced at the next safe tick.
	void PostExecGarbageCollect(IPythonScriptPlugin& Python)
	{
		FPythonCommandEx Gc;
		Gc.Command = TEXT("import gc; gc.collect()");
		Gc.ExecutionMode = EPythonCommandExecutionMode::ExecuteStatement;
		Python.ExecPythonCommandEx(Gc);
		if (GEngine && !IsGarbageCollecting())
		{
			GEngine->ForceGarbageCollection(false);
		}
	}

	TSharedRef<FJsonObject> RunScript(IPythonScriptPlugin* Python, const FString& Script, bool bIsolated, bool& bOutSuccess)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		if (!Python || !Python->IsPythonAvailable())
		{
			bOutSuccess = false;
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("output"), FString());
			Result->SetStringField(TEXT("result"), FString());
			Result->SetStringField(TEXT("error"), TEXT("Python plugin not available"));
			return Result;
		}

		FPythonCommandEx Command;
		ConfigureCommand(Command, Script, bIsolated);
		bOutSuccess = Python->ExecPythonCommandEx(Command);

		Result->SetBoolField(TEXT("success"), bOutSuccess);
		Result->SetStringField(TEXT("output"), Command.LogOutput.Num() > 0 ? CapString(JoinLogOutput(Command.LogOutput)) : FString());
		// Failure traces are written to CommandResult.
		Result->SetStringField(TEXT("result"), bOutSuccess ? CapString(Command.CommandResult) : FString());
		Result->SetStringField(TEXT("error"), bOutSuccess ? FString() : CapString(Command.CommandResult));

		if (bIsolated)
		{
			ClearPrivateScope(*Python);
		}
		PostExecGarbageCollect(*Python);
		return Result;
	}
}

FYcodePythonTools::FYcodePythonTools()
{
	Add(TEXT("ue_execute_python"), TEXT("Run a Python script inside the Unreal Editor (the editor's embedded Python with the `unreal` module). Returns stdout/log output, the result and the error trace. Editor helpers that the stock API lacks are exposed as unreal.YcodeAgentBridgeLibrary (CVars, notifications, Blueprint graph edits, widget trees, Niagara)."),
		FYcodeLinkSchema()
			.String(TEXT("script"), TEXT("Python source to execute as a file"), true)
			.Boolean(TEXT("isolated"), TEXT("Run in a private, cleared-afterwards scope (default true). false shares __main__ with later calls."))
			.Build(), false,
		[](const TSharedRef<FJsonObject>& Args, const FYcodeLinkToolCallback& Done)
		{
			const FString Script = YcodeLinkJson::GetString(Args, TEXT("script"));
			if (Script.IsEmpty())
			{
				Done(FYcodeLinkToolResult::Error(TEXT("script is required")));
				return;
			}
			bool bSuccess = false;
			const TSharedRef<FJsonObject> Result = RunScript(IPythonScriptPlugin::Get(), Script, YcodeLinkJson::GetBool(Args, TEXT("isolated"), true), bSuccess);
			Done(FYcodeLinkToolResult::Object(Result, !bSuccess));
		});

	Add(TEXT("ue_execute_python_batch"), TEXT("Run several Python scripts in order in a shared scope, stopping at the first failure. Returns one result per executed script and the index of the last successful one, so a caller can resume with startFrom."),
		FYcodeLinkSchema()
			.StringArray(TEXT("scripts"), TEXT("Scripts to run in order"), true)
			.Integer(TEXT("startFrom"), TEXT("Index of the first script to run (default 0)"))
			.Build(), false,
		[](const TSharedRef<FJsonObject>& Args, const FYcodeLinkToolCallback& Done)
		{
			const TArray<FString> Scripts = YcodeLinkJson::GetStringArray(Args, TEXT("scripts"));
			const int32 StartFrom = FMath::Max(0, YcodeLinkJson::GetInt(Args, TEXT("startFrom"), 0));
			TArray<TSharedPtr<FJsonValue>> Results;
			int32 LastSuccessful = StartFrom - 1;
			bool bAllOk = true;
			IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
			for (int32 Index = StartFrom; Index < Scripts.Num(); ++Index)
			{
				bool bSuccess = false;
				Results.Add(MakeShared<FJsonValueObject>(RunScript(Python, Scripts[Index], /*bIsolated=*/false, bSuccess)));
				if (!bSuccess)
				{
					bAllOk = false;
					break;
				}
				LastSuccessful = Index;
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetArrayField(TEXT("results"), Results);
			Result->SetNumberField(TEXT("lastSuccessfulIndex"), LastSuccessful);
			Done(FYcodeLinkToolResult::Object(Result, !bAllOk));
		});
}
