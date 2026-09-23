// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

class UClass;
class UEdGraphNode;
class UFunction;
class UObject;

namespace YcodeDebuggerSupport
{
	UClass* FindClassForNode(const UObject* Object, const UFunction* Function);
	UEdGraphNode* FindSourceNodeForCodeLocation(const UObject* Object, UFunction* Function);
	FString GetClassNameWithoutSuffix(const UClass* Class);
	// Object when it is itself a UClass (its meta-class is UClass), else null.
	const UClass* CastToUClass(const UObject* Object);
}
