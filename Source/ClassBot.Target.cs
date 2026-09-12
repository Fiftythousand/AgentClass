using UnrealBuildTool;
using System.Collections.Generic;

public class ClassBotTarget : TargetRules
{
    public ClassBotTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V6;

        ExtraModuleNames.AddRange(new string[] { "ClassBot" });
    }
}