// Copyright Pix Philosophy (HK) Limited.

using UnrealBuildTool;

// Agent-facing editor tools exposed over the Ycode link: editor Python
// execution, live asset search, screenshots, viewport camera control, actor
// spawning, and the Python-visible UYcodeAgentBridgeLibrary that fills gaps in
// the stock editor Python API (CVars, notifications, Blueprint graph edits,
// widget trees, Niagara).
public class YcodeAgentTools : ModuleRules
{
	public YcodeAgentTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"AssetRegistry",
			"Core",
			"CoreUObject",
			"Engine",
			"ImageWrapper",
			"Json",
			"PythonScriptPlugin",
			"RenderCore",
			"Slate",
			"SlateCore",
			"YcodeLink",
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new[]
			{
				"AssetTools",
				"BlueprintGraph",
				"EditorScriptingUtilities",
				"Kismet",
				"LevelEditor",
				"Niagara",
				"NiagaraEditor",
				"UMG",
				"UMGEditor",
				"UnrealEd",
			});
		}
	}
}
