using UnrealBuildTool;
using System.Collections.Generic;

public class ClassBotEditorTarget : TargetRules
{
    public ClassBotEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;

        ExtraModuleNames.AddRange(new string[] { "ClassBot"});
    }
}