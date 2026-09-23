// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

using UnrealBuildTool;

// "Open in Ycode": an ISourceCodeAccessor that hands files, lines and the
// project to the running Ycode workbench (or starts one). Windows only, like
// the IDE itself.
public class YcodeSourceCodeAccess : ModuleRules
{
	public YcodeSourceCodeAccess(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"Slate",
			"SlateCore",
			"SourceCodeAccess",
			"YcodeLink",
		});
	}
}
