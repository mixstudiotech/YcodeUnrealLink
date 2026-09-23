// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#include "YcodeLocator.h"

#include "IYcodeLink.h"

#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsPlatformMisc.h"
#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include <winreg.h>
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
	const TCHAR* const ExecutableName = TEXT("ycode.exe");

	FString ExistingExecutable(const FString& Candidate)
	{
		if (Candidate.IsEmpty())
		{
			return FString();
		}
		FString Path = Candidate;
		FPaths::NormalizeFilename(Path);
		if (FPaths::DirectoryExists(Path))
		{
			Path = FPaths::Combine(Path, ExecutableName);
		}
		return FPaths::FileExists(Path) ? FPaths::ConvertRelativePathToFull(Path) : FString();
	}
}

FString YcodeLocator::FindExecutable()
{
	if (const IYcodeLinkModule* Link = IYcodeLinkModule::GetPtr())
	{
		if (const TOptional<FYcodeLinkIdeInfo> Ide = Link->GetRegisteredIde())
		{
			if (const FString Path = ExistingExecutable(Ide->ExecutablePath); !Path.IsEmpty())
			{
				return Path;
			}
		}
	}

	if (const FString Path = ExistingExecutable(FPlatformMisc::GetEnvironmentVariable(TEXT("YCODE_PATH"))); !Path.IsEmpty())
	{
		return Path;
	}

#if PLATFORM_WINDOWS
	FString InstallDir;
	if (FWindowsPlatformMisc::QueryRegKey(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\Pix Philosophy\\Ycode"), TEXT("InstallDir"), InstallDir))
	{
		if (const FString Path = ExistingExecutable(InstallDir); !Path.IsEmpty())
		{
			return Path;
		}
	}
#endif

	for (const TCHAR* Variable : { TEXT("ProgramFiles"), TEXT("ProgramW6432") })
	{
		const FString Root = FPlatformMisc::GetEnvironmentVariable(Variable);
		if (Root.IsEmpty())
		{
			continue;
		}
		if (const FString Path = ExistingExecutable(FPaths::Combine(Root, TEXT("Pix Philosophy"), TEXT("Ycode"))); !Path.IsEmpty())
		{
			return Path;
		}
	}
	return FString();
}
