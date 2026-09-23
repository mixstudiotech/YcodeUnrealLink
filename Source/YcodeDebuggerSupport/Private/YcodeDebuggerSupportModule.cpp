// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#include "Modules/ModuleManager.h"

// The module body is empty on purpose: what matters is that the exported
// symbols in YcodeBlueprintStackGetter.cpp are linked into the editor.
class FYcodeDebuggerSupportModule final : public IModuleInterface
{
};

IMPLEMENT_MODULE(FYcodeDebuggerSupportModule, YcodeDebuggerSupport);
