#include "CraftingCatalog.h"
#include "InventoryComponent.h"

UCraftingCatalog* UCraftingCatalog::Load()
{ return LoadObject<UCraftingCatalog>(nullptr,TEXT("/Game/Progression/DA_CraftingCatalog.DA_CraftingCatalog")); }
const FFrontierRecipe* UCraftingCatalog::Find(FName Id) const
{ return Recipes.FindByPredicate([Id](const FFrontierRecipe& Recipe){return Recipe.Id==Id;}); }
FString UCraftingCatalog::ItemName(EFrontierItem Item)
{
    if(auto* Catalog=Load()) for(const auto& Entry:Catalog->Items) if(Entry.Item==Item) return Entry.DisplayName.ToString();
    return TEXT("Unavailable item");
}
FString UCraftingCatalog::FormatItems(const TMap<EFrontierItem,int32>& Costs)
{
    FString Result;
    if(auto* Catalog=Load()) for(const auto& Entry:Catalog->Items)
        if(const int32* Amount=Costs.Find(Entry.Item)) Result+=FString::Printf(TEXT("%s%d %s"),Result.IsEmpty()?TEXT(""):TEXT(" + "),*Amount,*Entry.DisplayName.ToString());
    return Result;
}
bool UCraftingCatalog::Validate() const
{
    TSet<EFrontierItem> SeenItems;
    for(const auto& Entry:Items)
    {
        if(Entry.Item>=EFrontierItem::Count || SeenItems.Contains(Entry.Item) || Entry.DisplayName.IsEmpty() || !FMath::IsFinite(Entry.CaptureMultiplier) || Entry.CaptureMultiplier<0 || Entry.CaptureMultiplier>3) return false;
        SeenItems.Add(Entry.Item);
    }
    if(SeenItems.Num()!=static_cast<int32>(EFrontierItem::Count) || Recipes.IsEmpty() || Recipes.Num()>32) return false;
    TSet<FName> SeenRecipes;
    for(const auto& Recipe:Recipes)
    {
        if(Recipe.Id.IsNone() || SeenRecipes.Contains(Recipe.Id) || Recipe.DisplayName.IsEmpty() || Recipe.Inputs.IsEmpty() ||
            !UInventoryComponent::Validate(Recipe.Inputs) || static_cast<uint8>(Recipe.Reward)>static_cast<uint8>(ERecipeReward::CrystalStudy) ||
            static_cast<uint8>(Recipe.RequiredResearch)>static_cast<uint8>(EFrontierResearch::CrystalStudy)) return false;
        for(const auto& Cost:Recipe.Inputs) if(Cost.Value<=0) return false;
        if(Recipe.Reward==ERecipeReward::Item && (Recipe.Output>=EFrontierItem::Count || Recipe.OutputAmount<=0 || Recipe.OutputAmount>999)) return false;
        if(Recipe.Reward==ERecipeReward::CrystalStudy && Recipe.RequiredResearch!=EFrontierResearch::CrystalSample) return false;
        SeenRecipes.Add(Recipe.Id);
    }
    return true;
}
