// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#pragma once

#include "HAL/Platform.h"

namespace YcodeDebuggerSupport
{
	// A [uint32 length][wchar_t chars...] slot inside the shared result buffer
	// the debugger reads after calling YcodeDebuggerSupport_GetBlueprintFunction.
	class FWideStringWrapper
	{
	public:
		FWideStringWrapper(char* Buffer, uint32 BufferLength);

		// Copies at most what fits (keeping a terminating null) and records
		// the copied length in the prefix. Returns the number of chars copied.
		uint32 CopyFromNullTerminatedStr(const wchar_t* Source, uint32 SourceLength) const;

	private:
		static constexpr uint32 LengthPrefixSize = sizeof(uint32);

		uint32* WideStringLength;
		uint32 AvailableStringSpaceInBytes;
		wchar_t* PointerToString;
	};
}
