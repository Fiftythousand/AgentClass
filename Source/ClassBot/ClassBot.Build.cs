using UnrealBuildTool;

public class ClassBot : ModuleRules
{
	public ClassBot(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore",
			"PptxViewer","Json","WebSockets","VaRest"
        });
	}
}