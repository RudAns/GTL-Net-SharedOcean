using UnrealBuildTool;

public class SharedOceanTarget : TargetRules
{
	public SharedOceanTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("SharedOcean");
		ExtraModuleNames.Add("SharedOceanShaders");
	}
}
