// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#include "YcodeBlueprintTools.h"
#include "YcodeGameControl.h"
#include "YcodeHotReload.h"
#include "YcodeShaderInfo.h"
#include "YcodeShaderPermutations.h"

#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"

// Owns the editor-side feature sets. Each one registers its tools with the
// YcodeLink module on construction and unregisters them on destruction, so
// module shutdown is just releasing the objects.
class FYcodeEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		GameControl = MakeUnique<FYcodeGameControl>();
		BlueprintTools = MakeUnique<FYcodeBlueprintTools>();
		HotReload = MakeUnique<FYcodeHotReload>();
		ShaderInfo = MakeUnique<FYcodeShaderInfo>();
		ShaderPermutations = MakeUnique<FYcodeShaderPermutations>();
	}

	virtual void ShutdownModule() override
	{
		ShaderInfo.Reset();
		HotReload.Reset();
		BlueprintTools.Reset();
		GameControl.Reset();
	}

	virtual bool SupportsDynamicReloading() override { return false; }

private:
	TUniquePtr<FYcodeGameControl> GameControl;
	TUniquePtr<FYcodeBlueprintTools> BlueprintTools;
	TUniquePtr<FYcodeHotReload> HotReload;
	TUniquePtr<FYcodeShaderInfo> ShaderInfo;
	TUniquePtr<FYcodeShaderPermutations> ShaderPermutations;
};

IMPLEMENT_MODULE(FYcodeEditorModule, YcodeEditor);
