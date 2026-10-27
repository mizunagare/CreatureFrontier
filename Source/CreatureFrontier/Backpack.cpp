#include "CFPlayerCharacter.h"
#include "InventoryComponent.h"
#include "PlayerEquipmentComponent.h"
#include "BaseBuildingComponent.h"

void ACFPlayerCharacter::ToggleBackpack()
{
    if(bPauseMenu || Health<=0) return;
    bBackpackOpen=!bBackpackOpen;
    if(bBackpackOpen)
    {
        CancelMeleeAttack(); Building->Cancel();
        bRosterOpen=false; bJournalOpen=false; bHelpVisible=false;
        BackpackRecipe=0;
        Feedback=TEXT("Backpack | Up/Down recipe | Enter craft one | C close | WORLD LIVE");
    }
}

void ACFPlayerCharacter::StepBackpackRecipe(int32 Direction)
{
    if(bBackpackOpen && !bPauseMenu) BackpackRecipe=(BackpackRecipe+(Direction>0?1:4))%5;
}

int32 ACFPlayerCharacter::GetBackpackRecipeCost(int32 Index) const
{
    if(Index>=0 && Index<3) return UInventoryComponent::CraftCost(static_cast<EFrontierItem>(Index+1));
    if(Index==3) return FMath::Clamp(Equipment->SpearCost,1,999);
    if(Index==4) return FMath::Clamp(Equipment->VestCost,1,999);
    return 0;
}

FString ACFPlayerCharacter::GetBackpackRecipeStatus(int32 Index) const
{
    if(Index<0 || Index>=5) return TEXT("Unknown recipe");
    if(Health<=0) return TEXT("Downed - respawn before crafting");
    const auto& Gear=Equipment->GetState();
    if((Index==3 && Gear.bOwnSpear) || (Index==4 && Gear.bOwnVest)) return TEXT("OWNED - use 6 / 7 to equip or remove");
    if(Index<3 && Inventory->Count(static_cast<EFrontierItem>(Index+1))>=UInventoryComponent::StackLimit) return TEXT("OUTPUT FULL - limit 999 per item");
    if(!IsNearWorkStation()) return TEXT("Need a work station within 8m");
    const int32 Missing=GetBackpackRecipeCost(Index)-Inventory->Count(EFrontierItem::Material);
    if(Missing>0) return FString::Printf(TEXT("Need %d more materials"),Missing);
    return TEXT("READY - Enter crafts one");
}

void ACFPlayerCharacter::CraftBackpackRecipe()
{
    if(!bBackpackOpen || bPauseMenu || !HasAuthority() || Health<=0) return;
    switch(BackpackRecipe)
    {
        case 0: CraftCaptureDevice(); break;
        case 1: CraftMedicine(); break;
        case 2: CraftFood(); break;
        case 3: CraftSpear(); break;
        case 4: CraftVest(); break;
    }
}
