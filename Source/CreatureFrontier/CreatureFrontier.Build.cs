using UnrealBuildTool;

// 伙伴姿态与其本地QA均属于现有游戏模块，不增加运行时模块依赖。
public class CreatureFrontier : ModuleRules
{
    public CreatureFrontier(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "GameplayTags", "AIModule", "NavigationSystem"
        });
        PrivateDependencyModuleNames.AddRange(new[] { "Slate", "SlateCore", "ApplicationCore" });
        if (Target.bBuildEditor) PrivateDependencyModuleNames.Add("UnrealEd");
    }
}
