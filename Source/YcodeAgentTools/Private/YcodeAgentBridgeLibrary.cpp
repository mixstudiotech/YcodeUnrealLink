// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#include "YcodeAgentBridgeLibrary.h"

#include "AssetToolsModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphUtilities.h"
#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "Factories/Factory.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/IConsoleManager.h"
#include "IAssetTools.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/CoreMiscDefines.h"
#include "Modules/ModuleManager.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraSystem.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "ObjectTools.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UObjectIterator.h"
#include "WidgetBlueprint.h"
#include "Widgets/Notifications/SNotificationList.h"

DEFINE_LOG_CATEGORY_STATIC(LogYcodeAgentBridge, Log, All);

namespace
{
	bool GDialogsSuppressed = false;
	bool GPreviousUnattended = false;

	UBlueprint* LoadBlueprintFromPath(const FString& Path)
	{
		return Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(Path));
	}

	UEdGraph* FindGraphInBlueprint(UBlueprint* Blueprint, const FString& GraphName)
	{
		if (!Blueprint)
		{
			return nullptr;
		}
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			if (Graph && Graph->GetName() == GraphName)
			{
				return Graph;
			}
		}
		return nullptr;
	}

	UK2Node* FindNodeByName(UEdGraph* Graph, const FString& NodeName)
	{
		if (!Graph)
		{
			return nullptr;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->GetName() == NodeName)
			{
				return Cast<UK2Node>(Node);
			}
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Equals(NodeName, ESearchCase::IgnoreCase))
			{
				return Cast<UK2Node>(Node);
			}
		}
		return nullptr;
	}

	UClass* FindClassByName(const FString& Name, const TCHAR* DefaultPackage)
	{
		if (UClass* Class = FindObject<UClass>(nullptr, *FString::Printf(TEXT("%s.%s"), DefaultPackage, *Name)))
		{
			return Class;
		}
		return FindObject<UClass>(nullptr, *Name);
	}

	void ConfigureK2Node(UK2Node* Node, const TSharedPtr<FJsonObject>& Params)
	{
		if (!Node || !Params.IsValid())
		{
			return;
		}
		if (UK2Node_CallFunction* CallFunction = Cast<UK2Node_CallFunction>(Node))
		{
			FString FunctionReference;
			if (Params->TryGetStringField(TEXT("FunctionReference"), FunctionReference))
			{
				// "ClassName::FuncName" or "/Script/Module.ClassName:FuncName"
				FString ClassName;
				FString FunctionName;
				UClass* FunctionClass = nullptr;
				if (FunctionReference.Split(TEXT("::"), &ClassName, &FunctionName))
				{
					FunctionClass = FindClassByName(ClassName, TEXT("/Script/Engine"));
				}
				else if (FunctionReference.Split(TEXT(":"), &ClassName, &FunctionName, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
				{
					FunctionClass = FindObject<UClass>(nullptr, *ClassName);
				}
				if (FunctionClass)
				{
					if (UFunction* Function = FunctionClass->FindFunctionByName(*FunctionName))
					{
						CallFunction->SetFromFunction(Function);
					}
				}
			}
		}
		else if (UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(Node))
		{
			FString EventName;
			if (Params->TryGetStringField(TEXT("CustomFunctionName"), EventName))
			{
				CustomEvent->CustomFunctionName = FName(*EventName);
			}
		}
		else if (UK2Node_VariableGet* VariableGet = Cast<UK2Node_VariableGet>(Node))
		{
			FString VariableName;
			if (Params->TryGetStringField(TEXT("VariableName"), VariableName))
			{
				VariableGet->VariableReference.SetSelfMember(FName(*VariableName));
			}
		}
		else if (UK2Node_VariableSet* VariableSet = Cast<UK2Node_VariableSet>(Node))
		{
			FString VariableName;
			if (Params->TryGetStringField(TEXT("VariableName"), VariableName))
			{
				VariableSet->VariableReference.SetSelfMember(FName(*VariableName));
			}
		}
		else if (UK2Node_DynamicCast* DynamicCast = Cast<UK2Node_DynamicCast>(Node))
		{
			FString TargetTypeName;
			if (Params->TryGetStringField(TEXT("TargetType"), TargetTypeName))
			{
				if (UClass* TargetClass = FindClassByName(TargetTypeName, TEXT("/Script/Engine")))
				{
					DynamicCast->TargetType = TargetClass;
				}
			}
		}
	}

	UWidgetBlueprint* LoadWidgetBlueprint(const FString& Path)
	{
		return Cast<UWidgetBlueprint>(UEditorAssetLibrary::LoadAsset(Path));
	}

	UClass* FindWidgetClass(const FString& Name)
	{
		static const TMap<FString, UClass*> Common = {
			{ TEXT("TextBlock"), UTextBlock::StaticClass() },
			{ TEXT("Button"), UButton::StaticClass() },
			{ TEXT("Image"), UImage::StaticClass() },
			{ TEXT("ProgressBar"), UProgressBar::StaticClass() },
		};
		if (UClass* const* Found = Common.Find(Name))
		{
			return *Found;
		}
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->GetName() == Name && It->IsChildOf(UWidget::StaticClass()))
			{
				return *It;
			}
		}
		return nullptr;
	}

	bool ImportPropertyText(const FProperty* Property, const FString& ValueText, void* PropertyData, UObject* Owner)
	{
		return Property->ImportText_Direct(*ValueText, PropertyData, Owner, PPF_None) != nullptr;
	}

	FString WriteJson(TFunctionRef<void(TJsonWriter<>&)> Body, bool bArray)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		bArray ? Writer->WriteArrayStart() : Writer->WriteObjectStart();
		Body(*Writer);
		bArray ? Writer->WriteArrayEnd() : Writer->WriteObjectEnd();
		Writer->Close();
		return Out;
	}
}

// -------------------------------------------------------------------- cvar --

FString UYcodeAgentBridgeLibrary::ReadCVar(const FString& Name)
{
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Name);
	if (!CVar)
	{
		UE_LOG(LogYcodeAgentBridge, Warning, TEXT("CVar '%s' not found"), *Name);
		return FString();
	}
	return CVar->GetString();
}

bool UYcodeAgentBridgeLibrary::WriteCVar(const FString& Name, const FString& Value)
{
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Name);
	if (!CVar)
	{
		UE_LOG(LogYcodeAgentBridge, Warning, TEXT("CVar '%s' not found"), *Name);
		return false;
	}
	if (CVar->TestFlags(ECVF_ReadOnly))
	{
		UE_LOG(LogYcodeAgentBridge, Warning, TEXT("CVar '%s' is read-only"), *Name);
		return false;
	}
	CVar->Set(*Value, ECVF_SetByConsole);
	return true;
}

FString UYcodeAgentBridgeLibrary::GetCVarInfo(const FString& Name)
{
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Name);
	if (!CVar)
	{
		return TEXT("{}");
	}
	return WriteJson([&](TJsonWriter<>& W)
	{
		W.WriteValue(TEXT("name"), Name);
		W.WriteValue(TEXT("value"), CVar->GetString());
		W.WriteValue(TEXT("help"), CVar->GetHelp());
		W.WriteValue(TEXT("type"), CVar->IsVariableInt() ? TEXT("int") : CVar->IsVariableFloat() ? TEXT("float") : CVar->IsVariableString() ? TEXT("string") : TEXT("unknown"));
		W.WriteValue(TEXT("read_only"), CVar->TestFlags(ECVF_ReadOnly));
		W.WriteValue(TEXT("scalability"), CVar->TestFlags(ECVF_Scalability));
	}, false);
}

// ------------------------------------------------------------ notification --

void UYcodeAgentBridgeLibrary::ShowNotification(const FString& Text, const FString& Type, float Duration)
{
	FNotificationInfo Info(FText::FromString(Text));
	Info.bFireAndForget = true;
	Info.ExpireDuration = Duration > 0.0f ? Duration : 3.0f;
	SNotificationItem::ECompletionState State = SNotificationItem::CS_None;
	if (Type.Equals(TEXT("success"), ESearchCase::IgnoreCase))
	{
		State = SNotificationItem::CS_Success;
	}
	else if (Type.Equals(TEXT("error"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("fail"), ESearchCase::IgnoreCase))
	{
		State = SNotificationItem::CS_Fail;
	}
	const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
	if (Item.IsValid() && State != SNotificationItem::CS_None)
	{
		Item->SetCompletionState(State);
	}
}

void UYcodeAgentBridgeLibrary::SetSuppressModalDialogs(bool bSuppress)
{
	GDialogsSuppressed = bSuppress;
	if (bSuppress)
	{
		GPreviousUnattended = GIsRunningUnattendedScript;
		GIsRunningUnattendedScript = true;
	}
	else
	{
		GIsRunningUnattendedScript = GPreviousUnattended;
	}
}

bool UYcodeAgentBridgeLibrary::IsSuppressingModalDialogs()
{
	return GDialogsSuppressed;
}

// ------------------------------------------------------------------ assets --

bool UYcodeAgentBridgeLibrary::ForceDeleteAsset(const FString& PackagePath)
{
	UObject* Asset = UEditorAssetLibrary::LoadAsset(PackagePath);
	if (!Asset)
	{
		UE_LOG(LogYcodeAgentBridge, Warning, TEXT("Asset '%s' not found"), *PackagePath);
		return false;
	}
	TArray<UObject*> ToDelete{ Asset };
	return ObjectTools::ForceDeleteObjects(ToDelete, /*bShowConfirmation=*/false) > 0;
}

int32 UYcodeAgentBridgeLibrary::ForceDeleteAssets(const TArray<FString>& PackagePaths)
{
	TArray<UObject*> ToDelete;
	for (const FString& Path : PackagePaths)
	{
		if (UObject* Asset = UEditorAssetLibrary::LoadAsset(Path))
		{
			ToDelete.Add(Asset);
		}
	}
	return ToDelete.IsEmpty() ? 0 : ObjectTools::ForceDeleteObjects(ToDelete, false);
}

FString UYcodeAgentBridgeLibrary::DuplicateAsset(const FString& SourcePath, const FString& DestPath)
{
	if (!UEditorAssetLibrary::DoesAssetExist(SourcePath))
	{
		return FString();
	}
	FString DestPackage;
	FString DestName;
	DestPath.Split(TEXT("/"), &DestPackage, &DestName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	if (DestName.IsEmpty())
	{
		DestName = DestPackage;
		DestPackage = TEXT("/Game");
	}
	IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* Duplicate = Tools.DuplicateAsset(DestName, DestPackage, UEditorAssetLibrary::LoadAsset(SourcePath));
	if (!Duplicate)
	{
		UE_LOG(LogYcodeAgentBridge, Warning, TEXT("DuplicateAsset failed for '%s' -> '%s/%s'"), *SourcePath, *DestPackage, *DestName);
		return FString();
	}
	const FString NewPath = Duplicate->GetPathName();
	UEditorAssetLibrary::SaveAsset(NewPath, false);
	return NewPath;
}

UObject* UYcodeAgentBridgeLibrary::EnsureAsset(const FString& PackagePath, const FString& AssetName, const FString& ClassName, const FString& FactoryClassName)
{
	const FString FullPath = PackagePath / AssetName + TEXT(".") + AssetName;
	if (UEditorAssetLibrary::DoesAssetExist(FullPath))
	{
		return UEditorAssetLibrary::LoadAsset(FullPath);
	}
	UClass* AssetClass = FindClassByName(ClassName, TEXT("/Script/Engine"));
	if (!AssetClass)
	{
		UE_LOG(LogYcodeAgentBridge, Warning, TEXT("EnsureAsset: unknown class '%s'"), *ClassName);
		return nullptr;
	}
	UFactory* Factory = nullptr;
	if (!FactoryClassName.IsEmpty())
	{
		if (UClass* FactoryClass = FindClassByName(FactoryClassName, TEXT("/Script/UnrealEd")))
		{
			Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
		}
	}
	if (!Factory)
	{
		static const TMap<FName, FName> AutoFactories = {
			{ TEXT("Material"), TEXT("/Script/UnrealEd.MaterialFactoryNew") },
			{ TEXT("DataTable"), TEXT("/Script/UnrealEd.DataTableFactory") },
			{ TEXT("WidgetBlueprint"), TEXT("/Script/UMGEditor.WidgetBlueprintFactory") },
		};
		if (const FName* FactoryPath = AutoFactories.Find(FName(*ClassName)))
		{
			if (UClass* FactoryClass = FindObject<UClass>(nullptr, *FactoryPath->ToString()))
			{
				Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
			}
		}
	}
	if (!Factory)
	{
		UE_LOG(LogYcodeAgentBridge, Warning, TEXT("EnsureAsset: no factory for class '%s'"), *ClassName);
		return nullptr;
	}
	IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* Created = Tools.CreateAsset(AssetName, PackagePath, AssetClass, Factory);
	if (!Created)
	{
		UE_LOG(LogYcodeAgentBridge, Warning, TEXT("EnsureAsset: CreateAsset failed for '%s/%s'"), *PackagePath, *AssetName);
	}
	return Created;
}

// -------------------------------------------------------------- blueprints --

FString UYcodeAgentBridgeLibrary::GetAllBlueprintGraphs(const FString& BlueprintPath)
{
	UBlueprint* Blueprint = LoadBlueprintFromPath(BlueprintPath);
	if (!Blueprint)
	{
		UE_LOG(LogYcodeAgentBridge, Warning, TEXT("GetAllBlueprintGraphs: Blueprint '%s' not found"), *BlueprintPath);
		return TEXT("[]");
	}
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	TSet<UEdGraph*> Seen;
	return WriteJson([&](TJsonWriter<>& W)
	{
		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph || Seen.Contains(Graph))
			{
				continue;
			}
			Seen.Add(Graph);
			const TCHAR* Type =
				Blueprint->UbergraphPages.Contains(Graph) ? TEXT("ubergraph") :
				Blueprint->FunctionGraphs.Contains(Graph) ? TEXT("function") :
				Blueprint->MacroGraphs.Contains(Graph) ? TEXT("macro") :
				Blueprint->DelegateSignatureGraphs.Contains(Graph) ? TEXT("delegate") : TEXT("other");
			W.WriteObjectStart();
			W.WriteValue(TEXT("name"), Graph->GetName());
			W.WriteValue(TEXT("type"), FString(Type));
			W.WriteValue(TEXT("num_nodes"), Graph->Nodes.Num());
			W.WriteObjectEnd();
		}
	}, true);
}

FString UYcodeAgentBridgeLibrary::GetBlueprintGraphNodes(const FString& BlueprintPath, const FString& GraphName)
{
	UEdGraph* Graph = FindGraphInBlueprint(LoadBlueprintFromPath(BlueprintPath), GraphName);
	if (!Graph)
	{
		UE_LOG(LogYcodeAgentBridge, Warning, TEXT("GetBlueprintGraphNodes: graph '%s' not found in '%s'"), *GraphName, *BlueprintPath);
		return TEXT("[]");
	}
	return WriteJson([&](TJsonWriter<>& W)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}
			W.WriteObjectStart();
			W.WriteValue(TEXT("name"), Node->GetName());
			W.WriteValue(TEXT("guid"), Node->NodeGuid.ToString());
			W.WriteValue(TEXT("class"), Node->GetClass()->GetName());
			W.WriteValue(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
			W.WriteArrayStart(TEXT("pins"));
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin)
				{
					continue;
				}
				W.WriteObjectStart();
				W.WriteValue(TEXT("name"), Pin->GetName());
				W.WriteValue(TEXT("direction"), Pin->Direction == EGPD_Output ? TEXT("output") : TEXT("input"));
				W.WriteValue(TEXT("type"), Pin->PinType.PinCategory.ToString());
				W.WriteValue(TEXT("connected"), Pin->LinkedTo.Num() > 0);
				W.WriteObjectEnd();
			}
			W.WriteArrayEnd();
			W.WriteObjectEnd();
		}
	}, true);
}

FString UYcodeAgentBridgeLibrary::AddBlueprintNode(const FString& BlueprintPath, const FString& GraphName, const FString& NodeClassName, const FString& NodeParamsJson, int32 X, int32 Y)
{
	UBlueprint* Blueprint = LoadBlueprintFromPath(BlueprintPath);
	UEdGraph* Graph = FindGraphInBlueprint(Blueprint, GraphName);
	if (!Graph)
	{
		return FString();
	}
	UClass* NodeClass = FindClassByName(NodeClassName, TEXT("/Script/BlueprintGraph"));
	if (!NodeClass)
	{
		NodeClass = FindClassByName(NodeClassName, TEXT("/Script/Engine"));
	}
	if (!NodeClass || !NodeClass->IsChildOf(UK2Node::StaticClass()))
	{
		return FString();
	}
	TSharedPtr<FJsonObject> Params;
	if (!NodeParamsJson.IsEmpty())
	{
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(NodeParamsJson), Params);
	}
	UK2Node* Node = NewObject<UK2Node>(Graph, NodeClass, NAME_None, RF_Transactional);
	if (!Node)
	{
		return FString();
	}
	Node->NodePosX = X;
	Node->NodePosY = Y;
	ConfigureK2Node(Node, Params);
	Node->CreateNewGuid();
	Node->PostPlacedNewNode();
	Node->AllocateDefaultPins();
	Graph->AddNode(Node, false, false);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return Node->GetName();
}

bool UYcodeAgentBridgeLibrary::ConnectBlueprintPins(const FString& BlueprintPath, const FString& GraphName, const FString& SourceNodeName, const FString& SourcePinName, const FString& TargetNodeName, const FString& TargetPinName)
{
	UBlueprint* Blueprint = LoadBlueprintFromPath(BlueprintPath);
	UEdGraph* Graph = FindGraphInBlueprint(Blueprint, GraphName);
	UK2Node* Source = FindNodeByName(Graph, SourceNodeName);
	UK2Node* Target = FindNodeByName(Graph, TargetNodeName);
	if (!Source || !Target)
	{
		return false;
	}
	auto FindPin = [](UK2Node* Node, const FString& Name) -> UEdGraphPin*
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->GetName() == Name)
			{
				return Pin;
			}
		}
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->GetDisplayName().ToString().Equals(Name, ESearchCase::IgnoreCase))
			{
				return Pin;
			}
		}
		return nullptr;
	};
	UEdGraphPin* SourcePin = FindPin(Source, SourcePinName);
	UEdGraphPin* TargetPin = FindPin(Target, TargetPinName);
	if (!SourcePin || !TargetPin)
	{
		return false;
	}
	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (!Schema || Schema->CanCreateConnection(SourcePin, TargetPin).Response == CONNECT_RESPONSE_DISALLOW)
	{
		return false;
	}
	const bool bConnected = Schema->TryCreateConnection(SourcePin, TargetPin);
	if (bConnected)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
	else
	{
		UE_LOG(LogYcodeAgentBridge, Warning, TEXT("ConnectBlueprintPins: '%s'.%s -> '%s'.%s failed"), *SourceNodeName, *SourcePinName, *TargetNodeName, *TargetPinName);
	}
	return bConnected;
}

bool UYcodeAgentBridgeLibrary::RemoveBlueprintNode(const FString& BlueprintPath, const FString& GraphName, const FString& NodeName)
{
	UBlueprint* Blueprint = LoadBlueprintFromPath(BlueprintPath);
	UK2Node* Node = FindNodeByName(FindGraphInBlueprint(Blueprint, GraphName), NodeName);
	if (!Node)
	{
		return false;
	}
	FBlueprintEditorUtils::RemoveNode(Blueprint, Node);
	return true;
}

bool UYcodeAgentBridgeLibrary::SetPinDefaultValue(const FString& BlueprintPath, const FString& GraphName, const FString& NodeName, const FString& PinName, const FString& DefaultValue)
{
	UBlueprint* Blueprint = LoadBlueprintFromPath(BlueprintPath);
	UK2Node* Node = FindNodeByName(FindGraphInBlueprint(Blueprint, GraphName), NodeName);
	if (!Node)
	{
		return false;
	}
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin->GetName() == PinName && Pin->Direction == EGPD_Input)
		{
			const UEdGraphSchema* Schema = Node->GetSchema();
			if (!Schema)
			{
				return false;
			}
			Schema->TrySetDefaultValue(*Pin, DefaultValue);
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
			return true;
		}
	}
	return false;
}

bool UYcodeAgentBridgeLibrary::AddBlueprintVariable(const FString& BlueprintPath, const FString& VariableName, const FString& PinCategoryName, const FString& PinSubCategoryName, const FString& PinSubCategoryObject, const FString& ContainerType, bool bIsReference)
{
	UBlueprint* Blueprint = LoadBlueprintFromPath(BlueprintPath);
	if (!Blueprint)
	{
		UE_LOG(LogYcodeAgentBridge, Warning, TEXT("AddBlueprintVariable: Blueprint '%s' not found"), *BlueprintPath);
		return false;
	}
	FEdGraphPinType PinType;
	PinType.PinCategory = FName(*PinCategoryName);
	PinType.PinSubCategory = FName(*PinSubCategoryName);
	if (!PinSubCategoryObject.IsEmpty())
	{
		PinType.PinSubCategoryObject = FindObject<UObject>(nullptr, *PinSubCategoryObject);
	}
	PinType.ContainerType =
		ContainerType.Equals(TEXT("Array"), ESearchCase::IgnoreCase) ? EPinContainerType::Array :
		ContainerType.Equals(TEXT("Set"), ESearchCase::IgnoreCase) ? EPinContainerType::Set :
		ContainerType.Equals(TEXT("Map"), ESearchCase::IgnoreCase) ? EPinContainerType::Map : EPinContainerType::None;
	PinType.bIsReference = bIsReference;
	if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*VariableName), PinType))
	{
		UE_LOG(LogYcodeAgentBridge, Warning, TEXT("AddBlueprintVariable: AddMemberVariable failed for '%s'"), *VariableName);
		return false;
	}
	return true;
}

bool UYcodeAgentBridgeLibrary::RemoveBlueprintVariable(const FString& BlueprintPath, const FString& VariableName)
{
	UBlueprint* Blueprint = LoadBlueprintFromPath(BlueprintPath);
	if (!Blueprint)
	{
		return false;
	}
	FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, FName(*VariableName));
	return true;
}

bool UYcodeAgentBridgeLibrary::SetBlueprintVariableCategory(const FString& BlueprintPath, const FString& VariableName, const FString& CategoryName)
{
	UBlueprint* Blueprint = LoadBlueprintFromPath(BlueprintPath);
	if (!Blueprint)
	{
		return false;
	}
	FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint, FName(*VariableName), nullptr, FText::FromString(CategoryName));
	return true;
}

bool UYcodeAgentBridgeLibrary::SetBlueprintVariableDefaultValue(const FString& BlueprintPath, const FString& VariableName, const FString& ValueText)
{
	UBlueprint* Blueprint = LoadBlueprintFromPath(BlueprintPath);
	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		return false;
	}
	UObject* DefaultObject = Blueprint->GeneratedClass->GetDefaultObject();
	FProperty* Property = Blueprint->GeneratedClass->FindPropertyByName(FName(*VariableName));
	if (!DefaultObject || !Property)
	{
		UE_LOG(LogYcodeAgentBridge, Warning, TEXT("SetBlueprintVariableDefaultValue: property '%s' not found"), *VariableName);
		return false;
	}
	if (!ImportPropertyText(Property, ValueText, Property->ContainerPtrToValuePtr<void>(DefaultObject), DefaultObject))
	{
		UE_LOG(LogYcodeAgentBridge, Warning, TEXT("SetBlueprintVariableDefaultValue: cannot import '%s' into '%s'"), *ValueText, *VariableName);
		return false;
	}
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	return true;
}

FString UYcodeAgentBridgeLibrary::ExportBlueprintNodes(const FString& BlueprintPath, const FString& GraphName, const FString& NodeNamesJson)
{
	UEdGraph* Graph = FindGraphInBlueprint(LoadBlueprintFromPath(BlueprintPath), GraphName);
	if (!Graph)
	{
		return FString();
	}
	TSet<UObject*> Selected;
	if (NodeNamesJson.IsEmpty() || NodeNamesJson == TEXT("[]"))
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node)
			{
				Selected.Add(Node);
			}
		}
	}
	else
	{
		TArray<TSharedPtr<FJsonValue>> Names;
		if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(NodeNamesJson), Names))
		{
			TMap<FString, UEdGraphNode*> ByName;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Node)
				{
					ByName.Add(Node->GetName(), Node);
				}
			}
			for (const TSharedPtr<FJsonValue>& Value : Names)
			{
				if (UEdGraphNode** Found = ByName.Find(Value->AsString()))
				{
					Selected.Add(*Found);
				}
			}
		}
	}
	FString ClipboardText;
	FEdGraphUtilities::ExportNodesToText(Selected, ClipboardText);
	return ClipboardText;
}

FString UYcodeAgentBridgeLibrary::ImportBlueprintNodes(const FString& BlueprintPath, const FString& GraphName, const FString& ClipboardText, int32 OffsetX, int32 OffsetY)
{
	UBlueprint* Blueprint = LoadBlueprintFromPath(BlueprintPath);
	UEdGraph* Graph = FindGraphInBlueprint(Blueprint, GraphName);
	if (!Graph)
	{
		return TEXT("[]");
	}
	// Importing renames graph objects; restore the names of existing nodes so
	// names handed out earlier keep resolving to the same node.
	TMap<UEdGraphNode*, FName> NamesBefore;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node)
		{
			NamesBefore.Add(Node, Node->GetFName());
		}
	}
	TSet<UEdGraphNode*> Imported;
	FEdGraphUtilities::ImportNodesFromText(Graph, ClipboardText, Imported);

	TArray<UObject*> Parked;
	for (const TPair<UEdGraphNode*, FName>& Pair : NamesBefore)
	{
		if (!IsValid(Pair.Key) || Pair.Key->GetFName() == Pair.Value)
		{
			continue;
		}
		if (UObject* Squatter = FindObject<UObject>(Graph, *Pair.Value.ToString()))
		{
			Squatter->Rename(*MakeUniqueObjectName(Graph, Squatter->GetClass(), TEXT("YcodeImportPending")).ToString(), Graph, REN_DontCreateRedirectors);
			Parked.Add(Squatter);
		}
	}
	for (const TPair<UEdGraphNode*, FName>& Pair : NamesBefore)
	{
		if (IsValid(Pair.Key) && Pair.Key->GetFName() != Pair.Value)
		{
			Pair.Key->Rename(*Pair.Value.ToString(), Graph, REN_DontCreateRedirectors);
		}
	}
	for (UObject* Squatter : Parked)
	{
		Squatter->Rename(*MakeUniqueObjectName(Graph, Squatter->GetClass()).ToString(), Graph, REN_DontCreateRedirectors);
	}
	for (UEdGraphNode* Node : Imported)
	{
		if (Node)
		{
			Node->CreateNewGuid();
			Node->NodePosX += OffsetX;
			Node->NodePosY += OffsetY;
		}
	}
	if (!Imported.IsEmpty())
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
	return WriteJson([&](TJsonWriter<>& W)
	{
		for (UEdGraphNode* Node : Imported)
		{
			if (Node)
			{
				W.WriteValue(Node->GetName());
			}
		}
	}, true);
}

// ----------------------------------------------------------------- widgets --

bool UYcodeAgentBridgeLibrary::AddWidgetToTree(const FString& WidgetBlueprintPath, const FString& ParentWidgetName, const FString& ChildWidgetClass, const FString& ChildWidgetName)
{
	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprint(WidgetBlueprintPath);
	if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
	{
		return false;
	}
	UPanelWidget* Panel = Cast<UPanelWidget>(WidgetBlueprint->WidgetTree->FindWidget(FName(*ParentWidgetName)));
	if (!Panel)
	{
		UE_LOG(LogYcodeAgentBridge, Warning, TEXT("AddWidgetToTree: parent '%s' not found or not a panel"), *ParentWidgetName);
		return false;
	}
	UClass* ChildClass = FindWidgetClass(ChildWidgetClass);
	if (!ChildClass)
	{
		UE_LOG(LogYcodeAgentBridge, Warning, TEXT("AddWidgetToTree: widget class '%s' not found"), *ChildWidgetClass);
		return false;
	}
	UWidget* NewWidget = WidgetBlueprint->WidgetTree->ConstructWidget<UWidget>(ChildClass, FName(*ChildWidgetName));
	if (!NewWidget)
	{
		return false;
	}
	Panel->AddChild(NewWidget);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
	return true;
}

bool UYcodeAgentBridgeLibrary::RemoveWidgetFromTree(const FString& WidgetBlueprintPath, const FString& WidgetName)
{
	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprint(WidgetBlueprintPath);
	if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
	{
		return false;
	}
	UWidget* Widget = WidgetBlueprint->WidgetTree->FindWidget(FName(*WidgetName));
	if (!Widget)
	{
		return false;
	}
	WidgetBlueprint->WidgetTree->RemoveWidget(Widget);
	if (WidgetBlueprint->WidgetTree->RootWidget == Widget)
	{
		WidgetBlueprint->WidgetTree->RootWidget = nullptr;
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
	return true;
}

FString UYcodeAgentBridgeLibrary::ListWidgetsInTree(const FString& WidgetBlueprintPath)
{
	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprint(WidgetBlueprintPath);
	if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
	{
		return TEXT("[]");
	}
	return WriteJson([&](TJsonWriter<>& W)
	{
		WidgetBlueprint->WidgetTree->ForEachWidget([&W](UWidget* Widget)
		{
			if (!Widget)
			{
				return;
			}
			W.WriteObjectStart();
			W.WriteValue(TEXT("name"), Widget->GetName());
			W.WriteValue(TEXT("class"), Widget->GetClass()->GetName());
			W.WriteValue(TEXT("parent"), Widget->GetParent() ? Widget->GetParent()->GetName() : FString());
			W.WriteValue(TEXT("slot"), Widget->Slot ? Widget->Slot->GetClass()->GetName() : FString());
			W.WriteObjectEnd();
		});
	}, true);
}

bool UYcodeAgentBridgeLibrary::SetWidgetProperty(const FString& WidgetBlueprintPath, const FString& WidgetName, const FString& PropertyName, const FString& ValueText)
{
	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprint(WidgetBlueprintPath);
	UWidget* Widget = WidgetBlueprint && WidgetBlueprint->WidgetTree ? WidgetBlueprint->WidgetTree->FindWidget(FName(*WidgetName)) : nullptr;
	if (!Widget)
	{
		return false;
	}
	FProperty* Property = Widget->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Property || !ImportPropertyText(Property, ValueText, Property->ContainerPtrToValuePtr<void>(Widget), Widget))
	{
		UE_LOG(LogYcodeAgentBridge, Warning, TEXT("SetWidgetProperty: cannot set '%s' on '%s'"), *PropertyName, *WidgetName);
		return false;
	}
	FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBlueprint);
	return true;
}

bool UYcodeAgentBridgeLibrary::SetWidgetSlotProperty(const FString& WidgetBlueprintPath, const FString& WidgetName, const FString& PropertyName, const FString& ValueText)
{
	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprint(WidgetBlueprintPath);
	UWidget* Widget = WidgetBlueprint && WidgetBlueprint->WidgetTree ? WidgetBlueprint->WidgetTree->FindWidget(FName(*WidgetName)) : nullptr;
	if (!Widget || !Widget->Slot)
	{
		return false;
	}
	FProperty* Property = Widget->Slot->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Property || !ImportPropertyText(Property, ValueText, Property->ContainerPtrToValuePtr<void>(Widget->Slot), Widget->Slot))
	{
		UE_LOG(LogYcodeAgentBridge, Warning, TEXT("SetWidgetSlotProperty: cannot set slot '%s' on '%s'"), *PropertyName, *WidgetName);
		return false;
	}
	FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBlueprint);
	return true;
}

// ----------------------------------------------------------------- niagara --

FString UYcodeAgentBridgeLibrary::GetNiagaraSystemParameters(const FString& NiagaraSystemPath)
{
	UNiagaraSystem* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(NiagaraSystemPath));
	if (!System)
	{
		return TEXT("[]");
	}
	TArray<FNiagaraVariable> Variables;
	System->GetExposedParameters().GetParameters(Variables);
	return WriteJson([&](TJsonWriter<>& W)
	{
		for (const FNiagaraVariable& Variable : Variables)
		{
			W.WriteObjectStart();
			W.WriteValue(TEXT("name"), Variable.GetName().ToString());
			W.WriteValue(TEXT("type"), Variable.GetType().GetName());
			W.WriteObjectEnd();
		}
	}, true);
}

FString UYcodeAgentBridgeLibrary::GetNiagaraSystemEmitters(const FString& NiagaraSystemPath)
{
	UNiagaraSystem* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(NiagaraSystemPath));
	if (!System)
	{
		return TEXT("[]");
	}
	return WriteJson([&](TJsonWriter<>& W)
	{
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			W.WriteObjectStart();
			W.WriteValue(TEXT("name"), Handle.GetName().ToString());
			W.WriteValue(TEXT("enabled"), Handle.GetIsEnabled());
			W.WriteObjectEnd();
		}
	}, true);
}
