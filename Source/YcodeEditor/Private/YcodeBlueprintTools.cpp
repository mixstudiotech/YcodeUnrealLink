// Copyright Pix Philosophy (HK) Limited.

#include "YcodeBlueprintTools.h"

#include "YcodeLinkLog.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "Framework/Docking/TabManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/TopLevelAssetPath.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/SWindow.h"

FYcodeBlueprintTools::FYcodeBlueprintTools()
{
	Add(TEXT("ue_find_blueprint_usages"), TEXT("Blueprints that derive from a C++ class (live asset registry, so unsaved and freshly created Blueprints count), plus the packages that reference the class's /Script module. Member-level usages (functions, properties) come from the IDE's offline .uasset index."),
		FYcodeLinkSchema()
			.String(TEXT("className"), TEXT("C++ class: short name (e.g. Character), prefixed name (ACharacter) or object path (/Script/Engine.Character)"), true)
			.Integer(TEXT("limit"), TEXT("Maximum entries per list, default 500"))
			.Build(), true,
		[](const TSharedRef<FJsonObject>& Args, const FYcodeLinkToolCallback& Done)
		{
			Done(FindBlueprintUsages(YcodeLinkJson::GetString(Args, TEXT("className")), FMath::Clamp(YcodeLinkJson::GetInt(Args, TEXT("limit"), 500), 1, 5000)));
		});

	Add(TEXT("ue_open_blueprint"), TEXT("Open a Blueprint (or any asset) in its editor and bring the Unreal Editor window to the front. With a node guid the graph focuses that node. Path is a long package or object path, e.g. /Game/Heroes/BP_Hero."),
		FYcodeLinkSchema()
			.String(TEXT("pathName"), TEXT("Package or object path of the asset"), true)
			.String(TEXT("guid"), TEXT("Optional Blueprint node GUID to focus"))
			.Build(), false,
		[](const TSharedRef<FJsonObject>& Args, const FYcodeLinkToolCallback& Done)
		{
			Done(OpenBlueprint(YcodeLinkJson::GetString(Args, TEXT("pathName")), YcodeLinkJson::GetString(Args, TEXT("guid"))));
		});

	Add(TEXT("ue_is_blueprint_path"), TEXT("Whether a string is a valid Unreal object path (the shape Blueprint references in log lines have)."),
		FYcodeLinkSchema().String(TEXT("pathName"), TEXT("Candidate path"), true).Build(), true,
		[](const TSharedRef<FJsonObject>& Args, const FYcodeLinkToolCallback& Done)
		{
			const FString PathName = YcodeLinkJson::GetString(Args, TEXT("pathName"));
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("pathName"), PathName);
			Result->SetBoolField(TEXT("isBlueprintPath"), FPackageName::IsValidObjectPath(PathName));
			Done(FYcodeLinkToolResult::Object(Result));
		});

	Add(TEXT("ue_package_path_for_file"), TEXT("Convert an on-disk .uasset/.umap path into the long package path the editor uses (and back: a package path into the file path)."),
		FYcodeLinkSchema()
			.String(TEXT("file"), TEXT("Absolute path of a .uasset or .umap"))
			.String(TEXT("packagePath"), TEXT("Long package path such as /Game/Maps/Lobby"))
			.Build(), true,
		[](const TSharedRef<FJsonObject>& Args, const FYcodeLinkToolCallback& Done)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			const FString File = YcodeLinkJson::GetString(Args, TEXT("file"));
			const FString PackagePath = YcodeLinkJson::GetString(Args, TEXT("packagePath"));
			if (!File.IsEmpty())
			{
				FString LongPackageName;
				if (!FPackageName::TryConvertFilenameToLongPackageName(File, LongPackageName))
				{
					Done(FYcodeLinkToolResult::Error(FString::Printf(TEXT("%s is not under a mounted content root"), *File)));
					return;
				}
				Result->SetStringField(TEXT("file"), File);
				Result->SetStringField(TEXT("packagePath"), LongPackageName);
				Result->SetStringField(TEXT("objectPath"), LongPackageName + TEXT(".") + FPackageName::GetShortName(LongPackageName));
			}
			else if (!PackagePath.IsEmpty())
			{
				const FString PackageName = FPackageName::ObjectPathToPackageName(PackagePath);
				FString Filename;
				const bool bMap = FPackageName::DoesPackageExist(PackageName, &Filename);
				if (!bMap)
				{
					Done(FYcodeLinkToolResult::Error(FString::Printf(TEXT("Package %s does not exist on disk"), *PackageName)));
					return;
				}
				Result->SetStringField(TEXT("packagePath"), PackageName);
				Result->SetStringField(TEXT("file"), FPaths::ConvertRelativePathToFull(Filename));
			}
			else
			{
				Done(FYcodeLinkToolResult::Error(TEXT("file or packagePath is required")));
				return;
			}
			Done(FYcodeLinkToolResult::Object(Result));
		});
}

void FYcodeBlueprintTools::BringEditorToFront()
{
	const TSharedPtr<SWindow> Window = FGlobalTabmanager::Get()->GetRootWindow();
	if (!Window.IsValid())
	{
		return;
	}
	if (Window->IsWindowMinimized())
	{
		Window->Restore();
	}
	else
	{
		Window->HACK_ForceToFront();
	}
}

FYcodeLinkToolResult FYcodeBlueprintTools::OpenBlueprint(const FString& PathName, const FString& GuidText)
{
	if (PathName.IsEmpty())
	{
		return FYcodeLinkToolResult::Error(TEXT("pathName is required"));
	}
	if (!GEditor)
	{
		return FYcodeLinkToolResult::Error(TEXT("Editor is not available"));
	}

	const FString PackageName = FPackageName::ObjectPathToPackageName(PathName);
	UPackage* Package = LoadPackage(nullptr, *PackageName, LOAD_NoRedirects);
	if (!Package)
	{
		return FYcodeLinkToolResult::Error(FString::Printf(TEXT("Cannot load package %s"), *PackageName));
	}
	Package->FullyLoad();

	// "/Game/A/BP_X.BP_X" names the object; "/Game/A/BP_X" means the asset
	// with the package's short name.
	FString ObjectName = FPackageName::ObjectPathToObjectName(PathName);
	if (ObjectName.IsEmpty() || ObjectName == PathName)
	{
		ObjectName = FPackageName::GetShortName(PackageName);
	}
	UObject* Object = FindObject<UObject>(Package, *ObjectName);
	if (!Object)
	{
		return FYcodeLinkToolResult::Error(FString::Printf(TEXT("No object %s in %s"), *ObjectName, *PackageName));
	}

	BringEditorToFront();

	FString Opened = TEXT("asset");
	FGuid NodeGuid;
	const bool bHasGuid = !GuidText.IsEmpty() && FGuid::Parse(GuidText, NodeGuid);
	if (const UBlueprint* Blueprint = Cast<UBlueprint>(Object))
	{
		UEdGraphNode* Node = bHasGuid ? FBlueprintEditorUtils::GetNodeByGUID(Blueprint, NodeGuid) : nullptr;
		if (Node)
		{
			FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Node);
			Opened = TEXT("node");
		}
		else
		{
			FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Blueprint);
			Opened = TEXT("blueprint");
		}
	}
	else
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Object);
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("packagePath"), PackageName);
	Result->SetStringField(TEXT("objectPath"), Object->GetPathName());
	Result->SetStringField(TEXT("class"), Object->GetClass()->GetName());
	Result->SetStringField(TEXT("opened"), Opened);
	return FYcodeLinkToolResult::Object(Result);
}

FYcodeLinkToolResult FYcodeBlueprintTools::FindBlueprintUsages(const FString& ClassName, int32 Limit)
{
	if (ClassName.IsEmpty())
	{
		return FYcodeLinkToolResult::Error(TEXT("className is required"));
	}
	UClass* Class = nullptr;
	if (ClassName.StartsWith(TEXT("/")))
	{
		Class = FindObject<UClass>(nullptr, *ClassName);
	}
	else
	{
		for (const TCHAR* Prefix : { TEXT(""), TEXT("U"), TEXT("A"), TEXT("F") })
		{
			Class = UClass::TryFindTypeSlow<UClass>(Prefix + ClassName);
			if (Class)
			{
				break;
			}
		}
	}
	if (!Class)
	{
		return FYcodeLinkToolResult::Error(FString::Printf(TEXT("Unknown class '%s'"), *ClassName));
	}

	IAssetRegistry& Registry = IAssetRegistry::GetChecked();
	TSet<FTopLevelAssetPath> Derived;
	Registry.GetDerivedClassNames({ FTopLevelAssetPath(Class) }, {}, Derived);

	TArray<TSharedPtr<FJsonValue>> Blueprints;
	for (const FTopLevelAssetPath& Path : Derived)
	{
		if (Blueprints.Num() >= Limit)
		{
			break;
		}
		// Generated classes are "<Blueprint>_C" inside the Blueprint's package;
		// native subclasses live in /Script packages and are not assets.
		FString AssetName = Path.GetAssetName().ToString();
		const FString PackageName = Path.GetPackageName().ToString();
		if (PackageName.StartsWith(TEXT("/Script/")) || !AssetName.RemoveFromEnd(TEXT("_C")))
		{
			continue;
		}
		// Compiler scratch classes of the same Blueprint, not assets.
		if (AssetName.StartsWith(TEXT("SKEL_")) || AssetName.StartsWith(TEXT("REINST_")) || AssetName.StartsWith(TEXT("TRASHCLASS_")))
		{
			continue;
		}
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("packagePath"), PackageName);
		Entry->SetStringField(TEXT("assetName"), AssetName);
		Entry->SetStringField(TEXT("objectPath"), PackageName + TEXT(".") + AssetName);
		Entry->SetStringField(TEXT("generatedClass"), Path.ToString());
		const FAssetData Asset = Registry.GetAssetByObjectPath(FSoftObjectPath(PackageName + TEXT(".") + AssetName));
		if (Asset.IsValid())
		{
			Entry->SetStringField(TEXT("assetClass"), Asset.AssetClassPath.GetAssetName().ToString());
			FString ParentClass;
			if (Asset.GetTagValue(FName(TEXT("ParentClass")), ParentClass))
			{
				Entry->SetStringField(TEXT("parentClass"), ParentClass);
			}
		}
		Blueprints.Add(MakeShared<FJsonValueObject>(Entry));
	}

	// Package-level dependency on the class's module: every asset that touches
	// anything in that module, which bounds where member usages can be.
	const FString ModulePackage = Class->GetOutermost()->GetName();
	TArray<FName> Referencers;
	Registry.GetReferencers(FName(*ModulePackage), Referencers);
	TArray<TSharedPtr<FJsonValue>> Packages;
	for (const FName& Referencer : Referencers)
	{
		if (Packages.Num() >= Limit)
		{
			break;
		}
		Packages.Add(MakeShared<FJsonValueString>(Referencer.ToString()));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("className"), Class->GetPathName());
	Result->SetStringField(TEXT("modulePackage"), ModulePackage);
	Result->SetArrayField(TEXT("derivedBlueprints"), Blueprints);
	Result->SetArrayField(TEXT("referencingPackages"), Packages);
	return FYcodeLinkToolResult::Object(Result);
}
