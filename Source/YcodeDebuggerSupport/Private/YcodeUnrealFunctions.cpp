// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#include "YcodeUnrealFunctions.h"

#include "Engine/BlueprintGeneratedClass.h"
#include "Templates/Casts.h"
#include "UObject/Class.h"

namespace YcodeDebuggerSupport
{
	UClass* FindClassForNode(const UObject* Object, const UFunction* Function)
	{
		if (Function)
		{
			return Function->GetOwnerClass();
		}
		return Object ? Object->GetClass() : nullptr;
	}

	UEdGraphNode* FindSourceNodeForCodeLocation(const UObject* Object, UFunction* Function)
	{
#if WITH_EDITORONLY_DATA
		if (!Object)
		{
			return nullptr;
		}
		if (UBlueprintGeneratedClass* Class = Cast<UBlueprintGeneratedClass>(FindClassForNode(Object, Function)))
		{
			return Class->GetDebugData().FindSourceNodeFromCodeLocation(Function, 0, true);
		}
#endif
		return nullptr;
	}

	FString GetClassNameWithoutSuffix(const UClass* Class)
	{
		if (!Class)
		{
			return TEXT("Null");
		}
		FString Result = Class->GetName();
		if (Class->ClassGeneratedBy)
		{
			Result.RemoveFromEnd(TEXT("_C"), ESearchCase::CaseSensitive);
		}
		return Result;
	}

	const UClass* CastToUClass(const UObject* Object)
	{
		if (!Object)
		{
			return nullptr;
		}
		const UClass* TypeOfObject = Object->GetClass();
		const UClass* TypeOfTypeOfObject = TypeOfObject ? TypeOfObject->GetClass() : nullptr;
		const UClass* TypeOfClass = TypeOfTypeOfObject ? TypeOfTypeOfObject->GetClass() : nullptr;
		if (!TypeOfClass || TypeOfClass != TypeOfTypeOfObject)
		{
			return nullptr;
		}
		return static_cast<const UClass*>(Object);
	}
}
