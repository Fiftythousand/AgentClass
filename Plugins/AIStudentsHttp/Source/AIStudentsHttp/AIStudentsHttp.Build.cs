using UnrealBuildTool;

public class AIStudentsHttp : ModuleRules
{
    public AIStudentsHttp(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "AudioCaptureCore"
        });

        PrivateDependencyModuleNames.Add("AudioCapture");
    }
}
