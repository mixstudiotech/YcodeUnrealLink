// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/UICommandInfo.h"
#include "YcodeLinkToolSet.h"

// Play-In-Editor control and play settings, driven through the same
// PlayWorld commands the editor toolbar uses, so what the IDE triggers is
// exactly what a click in the editor does. Play state and settings changes
// are pushed to the IDE as notifications.
class FYcodeGameControl final : public FYcodeLinkToolSet
{
public:
	FYcodeGameControl();
	~FYcodeGameControl() override;

private:
	struct FCommandRef
	{
		FName Name;
		TSharedPtr<FUICommandInfo> Command;
	};

	void CacheCommands();
	void BindEditorDelegates();
	void UnbindEditorDelegates();
	void RegisterTools();

	FYcodeLinkToolResult RunCommand(const FCommandRef& Command) const;
	FYcodeLinkToolResult Play(const FString& ModeName) const;
	TSharedRef<FJsonObject> PlayStateJson() const;
	TSharedRef<FJsonObject> PlaySettingsJson() const;
	FYcodeLinkToolResult ApplyPlaySettings(const TSharedRef<FJsonObject>& Args);

	void SetPlayState(const TCHAR* State);
	void NotifyPlaySettingsIfChanged();

	TMap<FString, FCommandRef> ModeCommands;
	FCommandRef ResumeCommand;
	FCommandRef PauseCommand;
	FCommandRef StopCommand;
	FCommandRef StepCommand;

	FDelegateHandle CommandsChangedHandle;
	FDelegateHandle BeginPieHandle;
	FDelegateHandle EndPieHandle;
	FDelegateHandle PausePieHandle;
	FDelegateHandle ResumePieHandle;
	FDelegateHandle SingleStepPieHandle;
	FDelegateHandle PropertyChangedHandle;

	FString PlayState = TEXT("Idle");
	FString LastSettingsJson;
};
