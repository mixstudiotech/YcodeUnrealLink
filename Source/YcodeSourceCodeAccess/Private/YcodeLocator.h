// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

namespace YcodeLocator
{
	// Path of ycode.exe, empty when none exists. Resolution order:
	//   1. the IDE that registered itself over the link (ue_ide_register)
	//   2. YCODE_PATH (the executable, or a directory containing ycode.exe)
	//   3. HKLM\SOFTWARE\Pix Philosophy\Ycode\InstallDir (the installer)
	//   4. %ProgramFiles%\Pix Philosophy\Ycode\ycode.exe
	FString FindExecutable();
}
