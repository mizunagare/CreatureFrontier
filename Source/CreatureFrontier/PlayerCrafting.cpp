#include "CFPlayerCharacter.h"
#include "CraftingCatalog.h"
#include "ResearchComponent.h"
#include "InventoryComponent.h"
#include "PlayerEquipmentComponent.h"
#include "BaseBuildingComponent.h"
#include "Kismet/GameplayStatics.h"

FString ACFPlayerCharacter::GetRecipeStatus(const FFrontierRecipe& Recipe) const
{
    if(Health<=0) return TEXT("Downed - respawn before crafting");
    const auto& Gear=Equipment->GetState();
    if((Recipe.Reward==ERecipeReward::Spear && Gear.bOwnSpear) || (Recipe.Reward==ERecipeReward::Vest && Gear.bOwnVest)) return TEXT("OWNED - use 6 / 7 to equip or remove");
    if(Recipe.Reward==ERecipeReward::CrystalStudy && Research->Has(EFrontierResearch::CrystalStudy)) return TEXT("OWNED - crystal study already completed");
    if(!Research->Has(Recipe.RequiredResearch)) return TEXT("LOCKED - ")+Research->GetHint();
    if(Recipe.Reward==ERecipeReward::Item && Recipe.OutputAmount>UInventoryComponent::StackLimit-(Inventory->Count(Recipe.Output)-Recipe.Inputs.FindRef(Recipe.Output))) return TEXT("OUTPUT FULL - limit 999 per item");
    if(!IsNearWorkStation()) return TEXT("Need a work station within 8m");
    if(Recipe.bBaseOnly && FVector::Dist2D(GetActorLocation(),UBaseBuildingComponent::BaseOrigin())>800) return TEXT("Return to the original base workbench to process / study / craft");
    if(auto* Catalog=UCraftingCatalog::Load()) for(const auto& Item:Catalog->Items)
    {
        const int32 Missing=Recipe.Inputs.FindRef(Item.Item)-Inventory->Count(Item.Item);
        if(Missing>0) return FString::Printf(TEXT("Need %d more %s"),Missing,*Item.DisplayName.ToString());
    }
    return TEXT("READY - Enter crafts one");
}
bool ACFPlayerCharacter::CraftRecipe(FName RecipeId)
{
    if(!HasAuthority() || Health<=0 || UGameplayStatics::IsGamePaused(this)) return false;
    const auto* Catalog=UCraftingCatalog::Load(); const auto* Recipe=Catalog?Catalog->Find(RecipeId):nullptr;
    if(!Recipe || !Catalog->Validate()) { Feedback=TEXT("Crafting catalog unavailable or invalid"); return false; }
    Feedback=GetRecipeStatus(*Recipe); if(!Feedback.StartsWith(TEXT("READY"))) return false;
    TMap<EFrontierItem,int32> Outputs;
    if(Recipe->Reward==ERecipeReward::Item) Outputs.Add(Recipe->Output,Recipe->OutputAmount);
    if(!Inventory->Transact(Recipe->Inputs,Outputs)) { Feedback=TEXT("Crafting transaction failed; no items spent"); return false; }
    // Preflight above guarantees these single-threaded unique state changes cannot fail.
    if(Recipe->Reward==ERecipeReward::Spear) Equipment->GrantCrafted(true);
    if(Recipe->Reward==ERecipeReward::Vest) Equipment->GrantCrafted(false);
    if(Recipe->Reward==ERecipeReward::CrystalStudy) Research->CompleteCrystalStudy();
    Feedback=TEXT("Crafted: ")+Recipe->DisplayName.ToString();
    return true;
}
