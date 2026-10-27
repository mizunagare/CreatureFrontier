#include "CFHUD.h"
#include "CFPlayerCharacter.h"
#include "InventoryComponent.h"
#include "PlayerEquipmentComponent.h"
#include "Engine/Canvas.h"

void ACFHUD::DrawBackpack(ACFPlayerCharacter* Player)
{
    const float Left=Canvas->ClipX-488;
    DrawRect(FLinearColor(.015f,.025f,.04f,.97f),Left,18,470,630);
    DrawRect(FLinearColor(.08f,.26f,.32f,1),Left+10,232+Player->GetBackpackRecipe()*56,450,52);
    DrawText(TEXT("BACKPACK & CRAFTING | C close"),FLinearColor::White,Left+14,30);
    DrawText(TEXT("LIVE WORLD - Esc opens pause menu"),FLinearColor::Yellow,Left+14,56);
    const TCHAR* Items[]={TEXT("Materials"),TEXT("Capture devices"),TEXT("Medicine"),TEXT("Food")};
    for(int32 Index=0;Index<4;++Index)
        DrawText(FString::Printf(TEXT("%s: %d / 999"),Items[Index],Player->Inventory->Count(static_cast<EFrontierItem>(Index))),FLinearColor::White,Left+14,88+Index*24);
    const auto& Gear=Player->Equipment->GetState();
    DrawText(FString::Printf(TEXT("Spear: %s | Vest: %s"),Gear.bSpearEquipped?TEXT("equipped"):Gear.bOwnSpear?TEXT("stored"):TEXT("not owned"),Gear.bVestEquipped?TEXT("equipped"):Gear.bOwnVest?TEXT("stored"):TEXT("not owned")),FLinearColor::Green,Left+14,188);
    DrawText(TEXT("RECIPES | Up / Down choose | Enter craft one"),FLinearColor::White,Left+14,212);
    const TCHAR* Recipes[]={TEXT("Capture device"),TEXT("Medicine"),TEXT("Food"),TEXT("Frontier Spear"),TEXT("Trail Vest")};
    for(int32 Index=0;Index<5;++Index)
    {
        const auto Color=Index==Player->GetBackpackRecipe()?FLinearColor::Yellow:FLinearColor::White;
        DrawText(FString::Printf(TEXT("%s%d. %s - %d materials"),Index==Player->GetBackpackRecipe()?TEXT("> "):TEXT("  "),Index+1,Recipes[Index],Player->GetBackpackRecipeCost(Index)),Color,Left+14,238+Index*56);
        DrawText(Player->GetBackpackRecipeStatus(Index),Color,Left+22,260+Index*56);
    }
    const TCHAR* Details[]={TEXT("One capture attempt. Injure a wild creature first."),TEXT("H heals you; P treats an aimed companion."),TEXT("J feeds a companion; U stores food at a food store."),TEXT("Unique weapon, crafted equipped. 6 toggles it."),TEXT("Unique armor, crafted equipped. 7 toggles it.")};
    DrawWrappedText(Details[Player->GetBackpackRecipe()],FLinearColor::Green,Left+14,536,440,2);
    DrawText(TEXT("1-5 also craft | 6 / 7 toggle owned gear"),FLinearColor::White,Left+14,586);
    DrawText(TEXT("No auto-save. F5 saves items and equipment."),FLinearColor::White,Left+14,612);
}
