// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#include "YcodeWideStringWrapper.h"

#include "HAL/UnrealMemory.h"

namespace YcodeDebuggerSupport
{
	FWideStringWrapper::FWideStringWrapper(char* Buffer, uint32 BufferLength)
		: WideStringLength(reinterpret_cast<uint32*>(Buffer))
		, AvailableStringSpaceInBytes(BufferLength - LengthPrefixSize)
		, PointerToString(reinterpret_cast<wchar_t*>(Buffer + LengthPrefixSize))
	{
		*WideStringLength = 0;
		PointerToString[0] = L'\0';
	}

	uint32 FWideStringWrapper::CopyFromNullTerminatedStr(const wchar_t* Source, uint32 SourceLength) const
	{
		const uint32 MaxChars = AvailableStringSpaceInBytes / sizeof(wchar_t) - 1;
		const uint32 Count = Source ? (SourceLength < MaxChars ? SourceLength : MaxChars) : 0;
		if (Count > 0)
		{
			FMemory::Memcpy(PointerToString, Source, Count * sizeof(wchar_t));
		}
		PointerToString[Count] = L'\0';
		*WideStringLength = Count;
		return Count;
	}
}
