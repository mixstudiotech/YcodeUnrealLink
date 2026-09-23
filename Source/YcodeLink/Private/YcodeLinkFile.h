// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Misc/DateTime.h"

// What the IDE needs to reach this editor. Written to
// `<Project>/Intermediate/Ycode/EditorLink.json` while the endpoint is up and
// deleted on shutdown; the IDE watches that file and checks `pid` liveness to
// tell a crashed editor from a running one.
struct FYcodeLinkFileInfo
{
	uint32 ProcessId = 0;
	uint32 Port = 0;
	FString Url;
	FString Token;
	FString ProjectName;
	FString ProjectFile;
	FString ExecutableName;
	FString ExecutablePath;
	FString EngineVersion;
	FString PluginVersion;
	FString ProtocolVersion;
	FDateTime Started;
};

namespace YcodeLinkFile
{
	// `<ProjectIntermediateDir>/Ycode` — the directory Ycode already owns for
	// the compile database, so nothing new shows up in the project tree.
	FString GetDirectory();
	FString GetPath();

	// Atomic: written next to the target and moved into place, so a reader
	// never sees a half-written file.
	bool Write(const FYcodeLinkFileInfo& Info);
	void Remove();
}
