// Copyright Pix Philosophy (HK) Limited.

#include "YcodeAgentToolSets.h"

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Class.h"
#include "UObject/TopLevelAssetPath.h"

namespace
{
	// Short-name class lookup over native and Blueprint-generated classes,
	// trying the usual prefixes so "Actor" finds AActor.
	UClass* ResolveClassByShortName(const FString& ShortName)
	{
		if (ShortName.IsEmpty())
		{
			return nullptr;
		}
		static const TCHAR* Prefixes[] = { TEXT(""), TEXT("U"), TEXT("A"), TEXT("F") };
		for (const TCHAR* Prefix : Prefixes)
		{
			if (UClass* Class = UClass::TryFindTypeSlow<UClass>(Prefix + ShortName))
			{
				return Class;
			}
		}
		return nullptr;
	}

	FString PackageNameToDiskPath(const FString& PackageName)
	{
		FString DiskPath;
		if (!FPackageName::TryConvertLongPackageNameToFilename(PackageName, DiskPath, FPackageName::GetAssetPackageExtension()))
		{
			return PackageName;
		}
		return FPaths::ConvertRelativePathToFull(DiskPath);
	}
}

FYcodeAssetSearch::FYcodeAssetSearch()
{
	Add(TEXT("ue_search_assets"), TEXT("Search the editor asset registry (live, includes unsaved and freshly imported assets). Filters by name substring, base class short name (e.g. Blueprint, StaticMesh, Actor for Blueprint actors) and package path (default /Game)."),
		FYcodeLinkSchema()
			.String(TEXT("query"), TEXT("Case-insensitive substring of the asset name"))
			.String(TEXT("baseClass"), TEXT("Short class name; subclasses are included"))
			.String(TEXT("packagePath"), TEXT("Root package path to search recursively, default /Game"))
			.Integer(TEXT("limit"), TEXT("Maximum number of results, default 200, at most 5000"))
			.Build(), true,
		[](const TSharedRef<FJsonObject>& Args, const FYcodeLinkToolCallback& Done)
		{
			const FString Query = YcodeLinkJson::GetString(Args, TEXT("query")).ToLower();
			const FString BaseClass = YcodeLinkJson::GetString(Args, TEXT("baseClass"));
			const FString PackagePath = YcodeLinkJson::GetString(Args, TEXT("packagePath"), TEXT("/Game"));
			const int32 Limit = FMath::Clamp(YcodeLinkJson::GetInt(Args, TEXT("limit"), 200), 1, 5000);

			FARFilter Filter;
			Filter.bRecursivePaths = true;
			Filter.bRecursiveClasses = true;
			Filter.PackagePaths.Add(FName(*PackagePath));
			if (!BaseClass.IsEmpty())
			{
				UClass* Class = ResolveClassByShortName(BaseClass);
				if (!Class)
				{
					Done(FYcodeLinkToolResult::Error(FString::Printf(TEXT("Unknown class '%s'"), *BaseClass)));
					return;
				}
				Filter.ClassPaths.Add(FTopLevelAssetPath(Class));
			}

			TArray<FAssetData> Found;
			IAssetRegistry::GetChecked().GetAssets(Filter, Found);

			TArray<TSharedPtr<FJsonValue>> Assets;
			for (const FAssetData& Asset : Found)
			{
				if (Assets.Num() >= Limit)
				{
					break;
				}
				// Placed actor instances (One File Per Actor) are not the
				// standalone assets a search is after.
				if (!Asset.IsTopLevelAsset())
				{
					continue;
				}
				const FString Name = Asset.AssetName.ToString();
				if (!Query.IsEmpty() && !Name.ToLower().Contains(Query))
				{
					continue;
				}
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("assetName"), Name);
				Entry->SetStringField(TEXT("packagePath"), Asset.PackageName.ToString());
				Entry->SetStringField(TEXT("objectPath"), Asset.GetObjectPathString());
				Entry->SetStringField(TEXT("assetPath"), PackageNameToDiskPath(Asset.PackageName.ToString()));
				Entry->SetStringField(TEXT("assetClass"), Asset.AssetClassPath.GetAssetName().ToString());
				if (!BaseClass.IsEmpty())
				{
					Entry->SetStringField(TEXT("baseClass"), BaseClass);
				}
				Assets.Add(MakeShared<FJsonValueObject>(Entry));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetArrayField(TEXT("assets"), Assets);
			Result->SetNumberField(TEXT("scanned"), Found.Num());
			Done(FYcodeLinkToolResult::Object(Result));
		});
}
