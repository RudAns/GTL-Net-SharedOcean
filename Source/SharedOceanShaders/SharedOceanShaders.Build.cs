using UnrealBuildTool;

public class SharedOceanShaders : ModuleRules
{
	public SharedOceanShaders(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "RenderCore", "RHI" });
	}
}
