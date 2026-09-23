// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

using UnrealBuildTool;

// Core of the Ycode editor plugin: the loopback MCP endpoint every other
// module registers its tools with, the EditorLink.json handshake file the IDE
// discovers the endpoint through, and the output-log capture that starts at
// PreDefault so nothing the engine prints before the IDE connects is lost.
public class YcodeLink : ModuleRules
{
	public YcodeLink(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"Json",
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"CoreUObject",
			"Engine",
			"HTTPServer",
			"Projects",
		});
	}
}
