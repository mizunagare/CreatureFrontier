#include "CFPlayerCharacter.h"
#include "InventoryComponent.h"
#include "PlayerEquipmentComponent.h"
#include "BaseBuildingComponent.h"
#include "CraftingCatalog.h"

void ACFPlayerCharacter::ToggleBackpack()
{
    if(bPauseMenu || Health<=0) return;
    bBackpackOpen=!bBackpackOpen;
    if(bBackpackOpen)
    {
        CancelMeleeAttack(); Building->Cancel();
        bRosterOpen=false; bJournalOpen=false; bHelpVisible=false;
        BackpackRecipe=0;
        Feedback=TEXT("背包 | 上下选择配方 | Enter 制作一个 | C 关闭 | 世界仍运行");
    }
}

void ACFPlayerCharacter::StepBackpackRecipe(int32 Direction)
{
    const auto* Catalog=UCraftingCatalog::Load(); const int32 Count=Catalog?Catalog->Recipes.Num():0;
    if(bBackpackOpen && !bPauseMenu && Count>0) BackpackRecipe=(BackpackRecipe+(Direction>0?1:Count-1))%Count;
}

int32 ACFPlayerCharacter::GetBackpackRecipeCost(int32 Index) const
{
    if(const auto* Catalog=UCraftingCatalog::Load();Catalog && Catalog->Recipes.IsValidIndex(Index)) return Catalog->Recipes[Index].Inputs.FindRef(EFrontierItem::Material);
    return 0;
}

FString ACFPlayerCharacter::GetBackpackRecipeStatus(int32 Index) const
{
    const auto* Catalog=UCraftingCatalog::Load();
    return Catalog && Catalog->Recipes.IsValidIndex(Index)?GetRecipeStatus(Catalog->Recipes[Index]):TEXT("未知配方");
}

void ACFPlayerCharacter::CraftBackpackRecipe()
{
    if(!bBackpackOpen || bPauseMenu || !HasAuthority() || Health<=0) return;
    if(const auto* Catalog=UCraftingCatalog::Load();Catalog && Catalog->Recipes.IsValidIndex(BackpackRecipe)) CraftRecipe(Catalog->Recipes[BackpackRecipe].Id);
}
