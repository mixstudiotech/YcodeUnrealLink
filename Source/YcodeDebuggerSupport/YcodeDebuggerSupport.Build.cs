// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

using UnrealBuildTool;

// Exports the helper the native debugger evaluates to render Blueprint
// frames (UFunction + context -> class / function / node display names) in
// the editor process. No dependency on the link server: the debugger reaches
// it by symbol name.
public class YcodeDebuggerSupport : ModuleRules
{
	public YcodeDebuggerSupport(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});
	}
}
