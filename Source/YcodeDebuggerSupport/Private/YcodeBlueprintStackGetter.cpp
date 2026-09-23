// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#include "YcodeUnrealFunctions.h"
#include "YcodeWideStringWrapper.h"

#include "EdGraph/EdGraphNode.h"
#include "Internationalization/Text.h"
#include "UObject/Class.h"
#include "UObject/UObjectBaseUtility.h"

// Contract with the native debugger (LLDB via udbgsvr): while stopped in a
// Blueprint VM frame, the debugger evaluates
//   YcodeDebuggerSupport_GetBlueprintFunction(Function, Context)
// and then reads YcodeDebuggerSupportBlueprintFunctionBuffer:
//   [uint32 result code: low 16 bits = last executed line, bit 16 = source
//    class resolved, bit 17 = graph node resolved]
//   [len][1024 wchar] full function name
//   [len][1024 wchar] scope display name (Blueprint class without _C)
//   [len][1024 wchar] function / node display name
// The globals below describe that layout so the debugger never hard-codes it.

namespace YcodeDebuggerSupport
{
	constexpr int32 StringBufferSizeInWideChars = 1024;
	constexpr int32 WideCharSizeInBytes = sizeof(wchar_t);
	constexpr int32 StringSizeInBytes = StringBufferSizeInWideChars * WideCharSizeInBytes;
	constexpr int32 LengthSizeInBytes = sizeof(uint32);
	constexpr int32 ResultCodeSizeInBytes = sizeof(uint32);
	constexpr int32 StringEntrySizeInBytes = StringSizeInBytes + LengthSizeInBytes;
	constexpr int32 FirstStringOffset = ResultCodeSizeInBytes;
	constexpr int32 SecondStringOffset = FirstStringOffset + StringEntrySizeInBytes;
	constexpr int32 ThirdStringOffset = SecondStringOffset + StringEntrySizeInBytes;
	constexpr int32 BufferSizeInBytes = ResultCodeSizeInBytes + 3 * StringEntrySizeInBytes;

	constexpr uint32 SourceClassResolvedFlag = 16;
	constexpr uint32 GraphNodeResolvedFlag = 17;
}

extern "C" DLLEXPORT void __stdcall YcodeDebuggerSupport_GetBlueprintFunction(void* PFunction, void* PContext);

struct FYcodeCallContextModule
{
	void* Context;
	void* Function;
} YcodeDebuggerSupportBlueprintFunctionCallContext;

int YcodeDebuggerSupportModuleVersion = 1;
char YcodeDebuggerSupportBlueprintFunctionBuffer[YcodeDebuggerSupport::BufferSizeInBytes] = { 0 };
int YcodeDebuggerSupportBlueprintBufferSizeInBytes = YcodeDebuggerSupport::BufferSizeInBytes;
int YcodeDebuggerSupportBlueprintStringBufferSizeInChars = YcodeDebuggerSupport::StringBufferSizeInWideChars;
int YcodeDebuggerSupportBlueprintWideCharSizeInBytes = YcodeDebuggerSupport::WideCharSizeInBytes;

namespace YcodeDebuggerSupport
{
	static FWideStringWrapper GFullName(YcodeDebuggerSupportBlueprintFunctionBuffer + FirstStringOffset, StringEntrySizeInBytes);
	static FWideStringWrapper GScopeDisplayName(YcodeDebuggerSupportBlueprintFunctionBuffer + SecondStringOffset, StringEntrySizeInBytes);
	static FWideStringWrapper GFunctionDisplayName(YcodeDebuggerSupportBlueprintFunctionBuffer + ThirdStringOffset, StringEntrySizeInBytes);
	static uint32* GResultCode = reinterpret_cast<uint32*>(YcodeDebuggerSupportBlueprintFunctionBuffer);

	static void SetLastExecutedLine(uint16 Line)
	{
		*GResultCode = (*GResultCode & 0xFFFF0000u) | Line;
	}

	static void SetResultFlag(uint32 Bit)
	{
		*GResultCode |= 1u << Bit;
	}

	static void Copy(const FWideStringWrapper& Slot, const FString& Text)
	{
		Slot.CopyFromNullTerminatedStr(reinterpret_cast<const wchar_t*>(*Text), Text.Len());
	}
}

void YcodeDebuggerSupport_GetBlueprintFunction(void* PFunction, void* PContext)
{
	using namespace YcodeDebuggerSupport;

	*GResultCode = 0;
	const UObject* Context = static_cast<UObject*>(PContext);
	UFunction* Function = static_cast<UFunction*>(PFunction);
	if (!Context || !Function)
	{
		SetLastExecutedLine(__LINE__);
		return;
	}

	FString FullName;
	Function->GetFullName(nullptr, FullName, EObjectFullNameFlags::None);
	Copy(GFullName, FullName);
	SetLastExecutedLine(__LINE__);

	const UObject* Outer = Function->GetOuter();
	const UClass* SourceClass = CastToUClass(Outer);
	if (SourceClass)
	{
		SetResultFlag(SourceClassResolvedFlag);
	}
	const FString ScopeDisplayName = SourceClass
		? GetClassNameWithoutSuffix(SourceClass)
		: (Outer ? FText::FromName(Outer->GetFName()).ToString() : FString(TEXT("Null")));
	Copy(GScopeDisplayName, ScopeDisplayName);
	SetLastExecutedLine(__LINE__);

	Copy(GFunctionDisplayName, FText::FromName(Function->GetFName()).ToString());
	SetLastExecutedLine(__LINE__);

#if WITH_EDITORONLY_DATA
	if (SourceClass)
	{
		if (const UEdGraphNode* GraphNode = FindSourceNodeForCodeLocation(Context, Function))
		{
			SetResultFlag(GraphNodeResolvedFlag);
			Copy(GFunctionDisplayName, GraphNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
			SetLastExecutedLine(__LINE__);
		}
	}
#endif
}
