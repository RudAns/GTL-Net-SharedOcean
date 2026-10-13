using UnrealBuildTool;

public class SharedOceanEditorTarget : TargetRules
{
	public SharedOceanEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("SharedOcean");
		ExtraModuleNames.Add("SharedOceanShaders");
	}
}
