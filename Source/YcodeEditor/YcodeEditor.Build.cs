// Copyright Pix Philosophy (HK) Limited.

using UnrealBuildTool;

// Editor-facing features of the Ycode link: Play-In-Editor control and play
// settings, Blueprint navigation, Hot Reload / Live Coding, and the shader
// source-directory mappings the IDE resolves virtual shader includes with.
public class YcodeEditor : ModuleRules
{
	public YcodeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"AssetRegistry",
			"Core",
			"CoreUObject",
			"Engine",
			"Json",
			"YcodeLink",
			"UnrealEd",
			"LevelEditor",
			"Slate",
			"SlateCore",
			"RenderCore",
			"RHI",
			"Renderer",
			"Projects",
		});

		if (Target.bWithLiveCoding)
		{
			PrivateIncludePathModuleNames.Add("LiveCoding");
		}
	}
}
