// Copyright Pix Philosophy (HK) Limited.

#include "YcodeSourceCodeAccessor.h"

#include "YcodeLocator.h"

#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformProcess.h"
#include "ISourceCodeAccessModule.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "YcodeSourceCodeAccessor"

DEFINE_LOG_CATEGORY_STATIC(LogYcodeSourceCodeAccess, Log, All);

namespace
{
	// Engine files reached through a game project's relative paths may not
	// exist at that spot; re-root them at the engine, as the other accessors do.
	TOptional<FString> ResolvePathToFile(const FString& FullPath)
	{
		FString Path = FullPath;
		if (FPaths::IsRelative(Path))
		{
			Path = FPaths::ConvertRelativePathToFull(Path);
		}
		if (FPaths::FileExists(Path))
		{
			return Path;
		}
		FPaths::NormalizeFilename(Path);
		static const TCHAR* SubDirs[] = { TEXT("/Engine/Source/"), TEXT("/Engine/Plugins/") };
		for (const TCHAR* SubDir : SubDirs)
		{
			const int32 Index = Path.Find(SubDir);
			if (Index != INDEX_NONE)
			{
				const FString Candidate = FPaths::RootDir() + Path.RightChop(Index + 1);
				if (FPaths::FileExists(Candidate))
				{
					return Candidate;
				}
			}
		}
		return {};
	}

	FString Quote(const FString& Value)
	{
		return FString::Printf(TEXT("\"%s\""), *Value);
	}
}

void FYcodeSourceCodeAccessor::RefreshAvailability()
{
	ExecutablePath = YcodeLocator::FindExecutable();
}

bool FYcodeSourceCodeAccessor::CanAccessSourceCode() const
{
	return !ExecutablePath.IsEmpty();
}

FName FYcodeSourceCodeAccessor::GetFName() const
{
	return FName(TEXT("Ycode"));
}

FText FYcodeSourceCodeAccessor::GetNameText() const
{
	return LOCTEXT("YcodeDisplayName", "Ycode");
}

FText FYcodeSourceCodeAccessor::GetDescriptionText() const
{
	return LOCTEXT("YcodeDisplayDesc", "Open source code files in Ycode");
}

FString FYcodeSourceCodeAccessor::ProjectArgument()
{
	const FString ProjectFile = FPaths::GetProjectFilePath();
	if (!ProjectFile.IsEmpty())
	{
		return FPaths::ConvertRelativePathToFull(ProjectFile);
	}
	return FPaths::ConvertRelativePathToFull(FPaths::RootDir());
}

bool FYcodeSourceCodeAccessor::DoesSolutionExist() const
{
	return FPaths::FileExists(ProjectArgument()) || FPaths::DirectoryExists(ProjectArgument());
}

bool FYcodeSourceCodeAccessor::Launch(const FString& Arguments) const
{
	if (ExecutablePath.IsEmpty() || !FPaths::FileExists(ExecutablePath))
	{
		FNotificationInfo Info(LOCTEXT("YcodeNotFound", "Ycode is not installed (set YCODE_PATH or install Ycode)"));
		Info.bFireAndForget = true;
		FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::CS_Fail);
		return false;
	}
	ISourceCodeAccessModule& Module = FModuleManager::LoadModuleChecked<ISourceCodeAccessModule>(TEXT("SourceCodeAccess"));
	Module.OnLaunchingCodeAccessor().Broadcast();
	FProcHandle Process = FPlatformProcess::CreateProc(*ExecutablePath, *Arguments, /*bLaunchDetached=*/true, /*bLaunchHidden=*/false,
	                                                   /*bLaunchReallyHidden=*/false, nullptr, 0, nullptr, nullptr);
	const bool bLaunched = Process.IsValid();
	if (!bLaunched)
	{
		UE_LOG(LogYcodeSourceCodeAccess, Warning, TEXT("Cannot start %s %s"), *ExecutablePath, *Arguments);
	}
	FPlatformProcess::CloseProc(Process);
	Module.OnDoneLaunchingCodeAccessor().Broadcast(bLaunched);
	return bLaunched;
}

bool FYcodeSourceCodeAccessor::OpenSolution()
{
	return Launch(Quote(ProjectArgument()));
}

bool FYcodeSourceCodeAccessor::OpenSolutionAtPath(const FString& InSolutionPath)
{
	// Callers pass a solution base name; the project descriptor is what Ycode
	// loads, so map anything inside the project back to it.
	FString Target = InSolutionPath;
	if (Target.EndsWith(TEXT(".sln")))
	{
		Target.LeftChopInline(4);
	}
	const FString ProjectFile = FPaths::GetProjectFilePath();
	if (!ProjectFile.IsEmpty() && FPaths::IsUnderDirectory(Target, FPaths::ProjectDir()))
	{
		Target = FPaths::ConvertRelativePathToFull(ProjectFile);
	}
	return Launch(Quote(Target));
}

bool FYcodeSourceCodeAccessor::OpenFileAtLine(const FString& FullPath, int32 LineNumber, int32 ColumnNumber)
{
	const TOptional<FString> Path = ResolvePathToFile(FullPath);
	if (!Path.IsSet())
	{
		return false;
	}
	FString Target = Path.GetValue();
	if (LineNumber > 0)
	{
		Target += FString::Printf(TEXT(":%d"), LineNumber);
		if (ColumnNumber > 0)
		{
			Target += FString::Printf(TEXT(":%d"), ColumnNumber);
		}
	}
	return Launch(Quote(ProjectArgument()) + TEXT(" -g ") + Quote(Target));
}

bool FYcodeSourceCodeAccessor::OpenSourceFiles(const TArray<FString>& AbsoluteSourcePaths)
{
	FString Arguments = Quote(ProjectArgument());
	int32 Resolved = 0;
	for (const FString& FullPath : AbsoluteSourcePaths)
	{
		if (const TOptional<FString> Path = ResolvePathToFile(FullPath))
		{
			Arguments += TEXT(" -g ") + Quote(Path.GetValue());
			++Resolved;
		}
	}
	return Resolved > 0 && Launch(Arguments);
}

bool FYcodeSourceCodeAccessor::AddSourceFiles(const TArray<FString>&, const TArray<FString>&)
{
	// Ycode loads the .uproject and watches the source tree; there is no
	// solution file to update.
	return true;
}

bool FYcodeSourceCodeAccessor::SaveAllOpenDocuments() const
{
	return false;
}

#undef LOCTEXT_NAMESPACE
