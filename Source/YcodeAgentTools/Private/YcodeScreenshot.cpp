// Copyright Pix Philosophy (HK) Limited.

#include "YcodeAgentToolSets.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ILevelEditor.h"
#include "LevelEditor.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/ObjectThumbnail.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "RenderingThread.h"
#include "SLevelViewport.h"
#include "UObject/SoftObjectPath.h"
#include "Widgets/SWindow.h"

namespace
{
	struct FCapture
	{
		bool bSuccess = false;
		FString Path;
		int32 Width = 0;
		int32 Height = 0;
		FString SourceApi;
		FString Error;
		TArray<uint8> Png;
	};

	FCapture Fail(const FString& Error, const FString& SourceApi = FString())
	{
		FCapture Capture;
		Capture.Error = Error;
		Capture.SourceApi = SourceApi;
		return Capture;
	}

	FString OutputDirectory()
	{
		const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ScreenShotDir(), TEXT("Ycode")));
		IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);
		return Dir;
	}

	FString TimestampedFilename(const FString& Label)
	{
		const FDateTime Now = FDateTime::UtcNow();
		return FString::Printf(TEXT("%04d%02d%02d-%02d%02d%02d_%s.png"), Now.GetYear(), Now.GetMonth(), Now.GetDay(),
		                       Now.GetHour(), Now.GetMinute(), Now.GetSecond(), *Label);
	}

	bool EncodePng(const void* Bytes, int64 NumBytes, int32 Width, int32 Height, ERGBFormat Format, TArray<uint8>& OutPng)
	{
		if (!Bytes || NumBytes <= 0 || Width <= 0 || Height <= 0)
		{
			return false;
		}
		IImageWrapperModule& Wrappers = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const TSharedPtr<IImageWrapper> Png = Wrappers.CreateImageWrapper(EImageFormat::PNG);
		if (!Png.IsValid() || !Png->SetRaw(Bytes, NumBytes, Width, Height, Format, 8))
		{
			return false;
		}
		const TArray64<uint8>& Compressed = Png->GetCompressed(100);
		OutPng.Reset();
		OutPng.Append(Compressed.GetData(), static_cast<int32>(Compressed.Num()));
		return true;
	}

	FCapture Finish(FCapture Capture, const FString& Label, const void* Bytes, int64 NumBytes, int32 Width, int32 Height, ERGBFormat Format)
	{
		if (!EncodePng(Bytes, NumBytes, Width, Height, Format, Capture.Png))
		{
			return Fail(TEXT("PNG encode failed"), Capture.SourceApi);
		}
		Capture.Path = FPaths::Combine(OutputDirectory(), TimestampedFilename(Label));
		if (!FFileHelper::SaveArrayToFile(Capture.Png, *Capture.Path))
		{
			return Fail(FString::Printf(TEXT("Cannot write %s"), *Capture.Path), Capture.SourceApi);
		}
		Capture.bSuccess = true;
		Capture.Width = Width;
		Capture.Height = Height;
		return Capture;
	}

	// Slate copies on the render thread; flush so the pixels can be read here.
	bool CaptureWidget(const TSharedRef<SWidget>& Widget, TArray<FColor>& OutPixels, FIntVector& OutSize)
	{
		if (!FSlateApplication::IsInitialized() || !FSlateApplication::Get().TakeScreenshot(Widget, OutPixels, OutSize))
		{
			return false;
		}
		FlushRenderingCommands();
		return OutPixels.Num() > 0 && OutSize.X > 0 && OutSize.Y > 0;
	}

	FCapture CaptureEditorWindow()
	{
		FCapture Capture;
		Capture.SourceApi = TEXT("SlateApplication.TakeScreenshot(SWindow)");
		if (!FSlateApplication::IsInitialized())
		{
			return Fail(TEXT("Slate is not initialized"), Capture.SourceApi);
		}
		// The active window is null while another application (the IDE) has
		// focus; fall back to the first visible top-level window.
		TSharedPtr<SWindow> Window = FSlateApplication::Get().GetActiveTopLevelRegularWindow();
		if (!Window.IsValid())
		{
			for (const TSharedRef<SWindow>& Top : FSlateApplication::Get().GetInteractiveTopLevelWindows())
			{
				if (Top->IsVisible() && !Top->IsWindowMinimized())
				{
					Window = Top;
					break;
				}
			}
		}
		if (!Window.IsValid())
		{
			return Fail(TEXT("No visible editor window"), Capture.SourceApi);
		}
		TArray<FColor> Pixels;
		FIntVector Size;
		if (!CaptureWidget(Window.ToSharedRef(), Pixels, Size))
		{
			return Fail(TEXT("TakeScreenshot failed for the window"), Capture.SourceApi);
		}
		return Finish(Capture, TEXT("editor_window"), Pixels.GetData(), Pixels.Num() * sizeof(FColor), Size.X, Size.Y, ERGBFormat::BGRA);
	}

	FCapture CaptureViewport()
	{
		FCapture Capture;
		Capture.SourceApi = TEXT("SlateApplication.TakeScreenshot(SLevelViewport)");
		FLevelEditorModule* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
		const TSharedPtr<ILevelEditor> Instance = LevelEditor ? LevelEditor->GetLevelEditorInstance().Pin() : nullptr;
		if (!Instance.IsValid())
		{
			return Fail(TEXT("No level editor instance"), Capture.SourceApi);
		}
		const TSharedPtr<SLevelViewport> Viewport = Instance->GetActiveViewportInterface();
		if (!Viewport.IsValid())
		{
			return Fail(TEXT("The level editor has no active viewport"), Capture.SourceApi);
		}
		TArray<FColor> Pixels;
		FIntVector Size;
		if (!CaptureWidget(Viewport.ToSharedRef(), Pixels, Size))
		{
			return Fail(TEXT("TakeScreenshot failed for the viewport"), Capture.SourceApi);
		}
		return Finish(Capture, TEXT("viewport"), Pixels.GetData(), Pixels.Num() * sizeof(FColor), Size.X, Size.Y, ERGBFormat::BGRA);
	}

	UObject* ResolveAsset(const FString& AssetPath)
	{
		if (UObject* Loaded = FSoftObjectPath(AssetPath).TryLoad())
		{
			return Loaded;
		}
		return FSoftObjectPath(AssetPath + TEXT(".") + FPaths::GetBaseFilename(AssetPath)).TryLoad();
	}

	bool TryLoadThumbnailFromPackage(const FString& AssetPath, FObjectThumbnail& OutThumbnail)
	{
		IAssetRegistry* Registry = IAssetRegistry::Get();
		if (!Registry)
		{
			return false;
		}
		FAssetData Data = Registry->GetAssetByObjectPath(FSoftObjectPath(AssetPath));
		if (!Data.IsValid())
		{
			Data = Registry->GetAssetByObjectPath(FSoftObjectPath(AssetPath + TEXT(".") + FPaths::GetBaseFilename(AssetPath)));
		}
		return Data.IsValid() && ThumbnailTools::LoadThumbnailFromPackage(Data, OutThumbnail);
	}

	FCapture CaptureAssetPreview(const FString& AssetPath, int32 Width, int32 Height, bool bForceLive)
	{
		FCapture Capture;
		FObjectThumbnail Buffer;
		const FObjectThumbnail* Source = nullptr;

		if (!bForceLive)
		{
			if (UObject* Loaded = FindObject<UObject>(nullptr, *AssetPath))
			{
				const FObjectThumbnail* Cached = ThumbnailTools::FindCachedThumbnail(Loaded->GetFullName());
				if (Cached && Cached->HasValidImageData())
				{
					Source = Cached;
					Capture.SourceApi = TEXT("ThumbnailTools.CachedThumbnail(Memory)");
				}
			}
			if (!Source && TryLoadThumbnailFromPackage(AssetPath, Buffer) && Buffer.HasValidImageData())
			{
				Source = &Buffer;
				Capture.SourceApi = TEXT("ThumbnailTools.LoadThumbnailFromPackage");
			}
		}
		// Live rendering can stall on assets with long streaming chains, so it
		// is opt-in; NeverFlush keeps it bounded.
		if (!Source && bForceLive)
		{
			UObject* Asset = ResolveAsset(AssetPath);
			if (!Asset)
			{
				return Fail(FString::Printf(TEXT("Asset not found or failed to load: %s"), *AssetPath));
			}
			const uint32 W = Width > 0 ? Width : ThumbnailTools::DefaultThumbnailSize;
			const uint32 H = Height > 0 ? Height : ThumbnailTools::DefaultThumbnailSize;
			ThumbnailTools::RenderThumbnail(Asset, W, H, ThumbnailTools::EThumbnailTextureFlushMode::NeverFlush, nullptr, &Buffer);
			Source = &Buffer;
			Capture.SourceApi = TEXT("ThumbnailTools.RenderThumbnail(NeverFlush)");
		}
		if (!Source || !Source->HasValidImageData())
		{
			return Fail(FString::Printf(TEXT("No thumbnail available for %s%s"), *AssetPath,
				bForceLive ? TEXT(": live render produced no data") : TEXT(": no cached thumbnail; open the asset once or pass forceLive=true")), Capture.SourceApi);
		}
		const TArray<uint8>& Bytes = Source->GetUncompressedImageData();
		return Finish(Capture, FString::Printf(TEXT("preview_%s"), *FPaths::GetBaseFilename(AssetPath)),
		              Bytes.GetData(), Bytes.Num(), Source->GetImageWidth(), Source->GetImageHeight(), ERGBFormat::BGRA);
	}
}

FYcodeScreenshot::FYcodeScreenshot()
{
	Add(TEXT("ue_screenshot"), TEXT("Capture the Unreal Editor: the whole editor window, the active level viewport, or an asset preview thumbnail. The PNG is written under Saved/Screenshots/<Platform>/Ycode and its path returned; inline=true also returns the image content."),
		FYcodeLinkSchema()
			.Enum(TEXT("kind"), TEXT("What to capture"), { TEXT("EditorWindow"), TEXT("Viewport"), TEXT("AssetPreview") }, true)
			.String(TEXT("assetPath"), TEXT("AssetPreview: long package path, e.g. /Game/Foo/BP_Hero"))
			.Integer(TEXT("width"), TEXT("AssetPreview live render width (0 = default)"))
			.Integer(TEXT("height"), TEXT("AssetPreview live render height (0 = default)"))
			.Boolean(TEXT("forceLive"), TEXT("AssetPreview: render live instead of using the cached thumbnail"))
			.Boolean(TEXT("inline"), TEXT("Include the PNG as an image content block"))
			.Build(), false,
		[](const TSharedRef<FJsonObject>& Args, const FYcodeLinkToolCallback& Done)
		{
			const FString Kind = YcodeLinkJson::GetString(Args, TEXT("kind"), TEXT("Viewport"));
			FCapture Capture;
			if (Kind == TEXT("EditorWindow"))
			{
				Capture = CaptureEditorWindow();
			}
			else if (Kind == TEXT("Viewport"))
			{
				Capture = CaptureViewport();
			}
			else if (Kind == TEXT("AssetPreview"))
			{
				const FString AssetPath = YcodeLinkJson::GetString(Args, TEXT("assetPath"));
				Capture = AssetPath.IsEmpty()
					? Fail(TEXT("assetPath is required for AssetPreview"))
					: CaptureAssetPreview(AssetPath, YcodeLinkJson::GetInt(Args, TEXT("width"), 0), YcodeLinkJson::GetInt(Args, TEXT("height"), 0), YcodeLinkJson::GetBool(Args, TEXT("forceLive"), false));
			}
			else
			{
				Capture = Fail(FString::Printf(TEXT("Unknown kind '%s'"), *Kind));
			}

			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), Capture.bSuccess);
			Result->SetStringField(TEXT("path"), Capture.Path);
			Result->SetNumberField(TEXT("width"), Capture.Width);
			Result->SetNumberField(TEXT("height"), Capture.Height);
			Result->SetStringField(TEXT("sourceApi"), Capture.SourceApi);
			Result->SetStringField(TEXT("error"), Capture.Error);
			FYcodeLinkToolResult ToolResult = FYcodeLinkToolResult::Object(Result, !Capture.bSuccess);
			if (Capture.bSuccess && YcodeLinkJson::GetBool(Args, TEXT("inline"), false))
			{
				ToolResult.WithImage(Capture.Png);
			}
			Done(ToolResult);
		});
}
