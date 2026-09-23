// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#include "YcodeLinkFile.h"

#include "YcodeLinkLog.h"
#include "YcodeLinkTool.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace YcodeLinkFile
{
	FString GetDirectory()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("Ycode")));
	}

	FString GetPath()
	{
		return FPaths::Combine(GetDirectory(), TEXT("EditorLink.json"));
	}

	bool Write(const FYcodeLinkFileInfo& Info)
	{
		const FString Directory = GetDirectory();
		if (!FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*Directory))
		{
			UE_LOG(LogYcodeLink, Warning, TEXT("Cannot create %s; the IDE will not discover this editor."), *Directory);
			return false;
		}

		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("version"), 1);
		Json->SetNumberField(TEXT("pid"), Info.ProcessId);
		Json->SetNumberField(TEXT("port"), Info.Port);
		Json->SetStringField(TEXT("url"), Info.Url);
		Json->SetStringField(TEXT("token"), Info.Token);
		Json->SetStringField(TEXT("projectName"), Info.ProjectName);
		Json->SetStringField(TEXT("projectFile"), Info.ProjectFile);
		Json->SetStringField(TEXT("executable"), Info.ExecutableName);
		Json->SetStringField(TEXT("executablePath"), Info.ExecutablePath);
		Json->SetStringField(TEXT("engineVersion"), Info.EngineVersion);
		Json->SetStringField(TEXT("pluginVersion"), Info.PluginVersion);
		Json->SetStringField(TEXT("protocolVersion"), Info.ProtocolVersion);
		Json->SetStringField(TEXT("started"), Info.Started.ToIso8601());

		const FString Target = GetPath();
		const FString Temp = FPaths::Combine(Directory, TEXT("~EditorLink.json"));
		if (!FFileHelper::SaveArrayToFile(YcodeLinkJson::ToUtf8(Json), *Temp))
		{
			UE_LOG(LogYcodeLink, Warning, TEXT("Cannot write %s"), *Temp);
			return false;
		}
		if (!IFileManager::Get().Move(*Target, *Temp, /*bReplace=*/true, /*bEvenIfReadOnly=*/true))
		{
			UE_LOG(LogYcodeLink, Warning, TEXT("Cannot move %s into place"), *Temp);
			return false;
		}
		return true;
	}

	void Remove()
	{
		const FString Target = GetPath();
		if (IFileManager::Get().FileExists(*Target))
		{
			IFileManager::Get().Delete(*Target, /*bRequireExists=*/false, /*bEvenReadOnly=*/true, /*bQuiet=*/true);
		}
	}
}
