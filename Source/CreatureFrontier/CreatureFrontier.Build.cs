using UnrealBuildTool;

public class CreatureFrontier : ModuleRules
{
    public CreatureFrontier(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "GameplayTags", "AIModule", "NavigationSystem"
        });
        PrivateDependencyModuleNames.AddRange(new[] { "Slate", "SlateCore" });
        if (Target.bBuildEditor) PrivateDependencyModuleNames.Add("UnrealEd");
    }
}
