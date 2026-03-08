using UnrealBuildTool;

public class MeshOptimizer : ModuleRules
{
	public MeshOptimizer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// Editor
			"UnrealEd",
			"Slate",
			"SlateCore",
			"EditorStyle",
			"ToolMenus",
			"PropertyEditor",
			"EditorScriptingUtilities",
			"Blutility",

			// Mesh processing
			"GeometryCore",
			"MeshConversion",
			"MeshDescription",
			"StaticMeshDescription",
			"MeshUtilities",
			"MeshMergeUtilities",
			"MeshReductionInterface",
			"RawMesh",
			"DynamicMesh",

			// UI
			"EditorWidgets",
			"WorkspaceMenuStructure",
			"Projects",
			"ContentBrowser",
			"AssetRegistry"
		});
	}
}
