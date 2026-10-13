using UnrealBuildTool;

public class SharedOcean : ModuleRules
{
	public SharedOcean(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "UMG", "Slate", "SlateCore", "ProceduralMeshComponent",
			"NetCore", "RenderCore", "RHI", "SharedOceanShaders"
		});
	}
}
