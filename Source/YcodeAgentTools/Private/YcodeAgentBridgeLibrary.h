// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "YcodeAgentBridgeLibrary.generated.h"

/**
 * Python-exposed editor helpers that fill gaps in the stock Unreal Python API.
 * Callable from ue_execute_python as
 *   unreal.YcodeAgentBridgeLibrary.method_name(args)
 * Editor-only; runs on the game thread (the Python transport dispatches there).
 */
UCLASS()
class UYcodeAgentBridgeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Console variables
	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|CVar")
	static FString ReadCVar(const FString& Name);

	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|CVar")
	static bool WriteCVar(const FString& Name, const FString& Value);

	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|CVar")
	static FString GetCVarInfo(const FString& Name);

	// Editor notification
	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|Notify")
	static void ShowNotification(const FString& Text, const FString& Type = TEXT("info"), float Duration = 0.0f);

	// Modal dialog suppression (unattended script mode)
	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|Dialog")
	static void SetSuppressModalDialogs(bool bSuppress);

	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|Dialog")
	static bool IsSuppressingModalDialogs();

	// Asset operations
	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|Asset")
	static bool ForceDeleteAsset(const FString& PackagePath);

	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|Asset")
	static int32 ForceDeleteAssets(const TArray<FString>& PackagePaths);

	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|Asset")
	static FString DuplicateAsset(const FString& SourcePath, const FString& DestPath);

	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|Asset")
	static UObject* EnsureAsset(const FString& PackagePath, const FString& AssetName,
		const FString& ClassName, const FString& FactoryClassName);

	// Blueprint graph
	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|Blueprint")
	static FString GetAllBlueprintGraphs(const FString& BlueprintPath);

	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|Blueprint")
	static FString GetBlueprintGraphNodes(const FString& BlueprintPath, const FString& GraphName);

	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|Blueprint")
	static FString AddBlueprintNode(const FString& BlueprintPath, const FString& GraphName,
		const FString& NodeClassName, const FString& NodeParamsJson, int32 X, int32 Y);

	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|Blueprint")
	static bool ConnectBlueprintPins(const FString& BlueprintPath, const FString& GraphName,
		const FString& SourceNodeName, const FString& SourcePinName,
		const FString& TargetNodeName, const FString& TargetPinName);

	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|Blueprint")
	static bool RemoveBlueprintNode(const FString& BlueprintPath, const FString& GraphName,
		const FString& NodeName);

	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|Blueprint")
	static bool SetPinDefaultValue(const FString& BlueprintPath, const FString& GraphName,
		const FString& NodeName, const FString& PinName, const FString& DefaultValue);

	// Blueprint variables
	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|Blueprint")
	static bool AddBlueprintVariable(const FString& BlueprintPath, const FString& VariableName,
		const FString& PinCategoryName, const FString& PinSubCategoryName,
		const FString& PinSubCategoryObject, const FString& ContainerType, bool bIsReference);

	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|Blueprint")
	static bool RemoveBlueprintVariable(const FString& BlueprintPath, const FString& VariableName);

	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|Blueprint")
	static bool SetBlueprintVariableCategory(const FString& BlueprintPath, const FString& VariableName,
		const FString& CategoryName);

	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|Blueprint")
	static bool SetBlueprintVariableDefaultValue(const FString& BlueprintPath, const FString& VariableName,
		const FString& ValueText);

	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|Blueprint")
	static FString ExportBlueprintNodes(const FString& BlueprintPath, const FString& GraphName,
		const FString& NodeNamesJson);

	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|Blueprint")
	static FString ImportBlueprintNodes(const FString& BlueprintPath, const FString& GraphName,
		const FString& ClipboardText, int32 OffsetX, int32 OffsetY);

	// Widget tree
	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|Widget")
	static bool AddWidgetToTree(const FString& WidgetBlueprintPath, const FString& ParentWidgetName,
		const FString& ChildWidgetClass, const FString& ChildWidgetName);

	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|Widget")
	static bool RemoveWidgetFromTree(const FString& WidgetBlueprintPath, const FString& WidgetName);

	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|Widget")
	static FString ListWidgetsInTree(const FString& WidgetBlueprintPath);

	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|Widget")
	static bool SetWidgetProperty(const FString& WidgetBlueprintPath, const FString& WidgetName,
		const FString& PropertyName, const FString& ValueText);

	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|Widget")
	static bool SetWidgetSlotProperty(const FString& WidgetBlueprintPath, const FString& WidgetName,
		const FString& PropertyName, const FString& ValueText);

	// Niagara
	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|Niagara")
	static FString GetNiagaraSystemParameters(const FString& NiagaraSystemPath);

	UFUNCTION(BlueprintCallable, Category = "YcodeAgentBridge|Niagara")
	static FString GetNiagaraSystemEmitters(const FString& NiagaraSystemPath);
};
