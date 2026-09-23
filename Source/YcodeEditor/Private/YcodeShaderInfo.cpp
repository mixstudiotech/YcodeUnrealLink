// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#include "YcodeShaderInfo.h"

#include "YcodeLinkLog.h"

#include "Engine/Engine.h"
#include "HAL/FileManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

FYcodeShaderInfo::FYcodeShaderInfo()
{
	Add(TEXT("ue_shader_source_mappings"), TEXT("Virtual shader directory mappings (e.g. /Engine -> Engine/Shaders, /Plugin/Foo -> the plugin's Shaders folder) used to resolve #include in .usf/.ush files. Also written to Intermediate/Ycode/ShaderSourceMappings.ini."),
		FYcodeLinkSchema().Build(), true,
		[this](const TSharedRef<FJsonObject>&, const FYcodeLinkToolCallback& Done)
		{
			Done(FYcodeLinkToolResult::Object(MappingsJson()));
		});

	// Mappings are registered during engine init; a plugin enabled from the
	// Plugins browser arrives after that and can write right away.
	if (GEngine)
	{
		WriteMappings();
	}
	else
	{
		PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FYcodeShaderInfo::WriteMappings);
	}
}

FYcodeShaderInfo::~FYcodeShaderInfo()
{
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
	}
}

FString FYcodeShaderInfo::MappingFilePath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("Ycode"), TEXT("ShaderSourceMappings.ini")));
}

TSharedRef<FJsonObject> FYcodeShaderInfo::MappingsJson() const
{
	TSharedRef<FJsonObject> Mappings = MakeShared<FJsonObject>();
	for (const TPair<FString, FString>& Pair : AllShaderSourceDirectoryMappings())
	{
		Mappings->SetStringField(Pair.Key, FPaths::ConvertRelativePathToFull(Pair.Value));
	}
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetObjectField(TEXT("mappings"), Mappings);
	Result->SetStringField(TEXT("file"), MappingFilePath());
	return Result;
}

void FYcodeShaderInfo::WriteMappings() const
{
	TArray<FString> Lines;
	for (const TPair<FString, FString>& Pair : AllShaderSourceDirectoryMappings())
	{
		Lines.Add(FString::Printf(TEXT("%s=%s"), *Pair.Key, *FPaths::ConvertRelativePathToFull(Pair.Value)));
	}
	Lines.Sort();

	const FString Target = MappingFilePath();
	TArray<FString> Existing;
	if (IFileManager::Get().FileExists(*Target) && FFileHelper::LoadFileToStringArray(Existing, *Target) && Existing == Lines)
	{
		return;
	}
	const FString Temp = FPaths::Combine(FPaths::GetPath(Target), TEXT("~ShaderSourceMappings.ini"));
	if (!FFileHelper::SaveStringArrayToFile(Lines, *Temp))
	{
		UE_LOG(LogYcodeLink, Warning, TEXT("Cannot write %s"), *Temp);
		return;
	}
	IFileManager::Get().Move(*Target, *Temp, /*bReplace=*/true, /*bEvenIfReadOnly=*/true);
}
