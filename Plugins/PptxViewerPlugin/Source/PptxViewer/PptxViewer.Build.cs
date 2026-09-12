// PptxViewer.Build.cs
// PPTX Viewer 插件的模块依赖配置

using UnrealBuildTool;

public class PptxViewer : ModuleRules
{
    public PptxViewer(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "UMG",              // UUserWidget
            "Slate",
            "SlateCore",
            "WebBrowserWidget", // UWebBrowser UMG 控件
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "HTTP",             // FHttpModule, 文件上传
            "DesktopPlatform",  // IDesktopPlatform, 文件选择对话框
            "Json",             // JSON 序列化/反序列化
            "JsonUtilities",
        });
    }
}
