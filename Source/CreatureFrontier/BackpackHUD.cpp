#include "CFHUD.h"
#include "CFPlayerCharacter.h"
#include "InventoryComponent.h"
#include "PlayerEquipmentComponent.h"
#include "Engine/Canvas.h"
#include "CraftingCatalog.h"

void ACFHUD::DrawBackpack(ACFPlayerCharacter* Player)
{
    const float Left=Canvas->ClipX-488;
    const auto* Catalog=UCraftingCatalog::Load(); if(!Catalog || !Catalog->Validate()) return;
    const int32 Start=(Player->GetBackpackRecipe()/5)*5;
    DrawRect(FLinearColor(.015f,.025f,.04f,.97f),Left,18,470,630);
    DrawRect(FLinearColor(.08f,.26f,.32f,1),Left+10,232+(Player->GetBackpackRecipe()-Start)*56,450,52);
    DrawText(TEXT("BACKPACK & CRAFTING | C close"),FLinearColor::White,Left+14,30);
    DrawText(TEXT("LIVE WORLD - Esc opens pause menu"),FLinearColor::Yellow,Left+14,56);
    for(int32 Index=0;Index<Catalog->Items.Num();++Index)
        DrawText(FString::Printf(TEXT("%s: %d / 999"),*Catalog->Items[Index].DisplayName.ToString(),Player->Inventory->Count(Catalog->Items[Index].Item)),FLinearColor::White,Left+14+(Index%2)*220,88+(Index/2)*27);
    const auto& Gear=Player->Equipment->GetState();
    DrawText(FString::Printf(TEXT("Spear: %s | Vest: %s"),Gear.bSpearEquipped?TEXT("equipped"):Gear.bOwnSpear?TEXT("stored"):TEXT("not owned"),Gear.bVestEquipped?TEXT("equipped"):Gear.bOwnVest?TEXT("stored"):TEXT("not owned")),FLinearColor::Green,Left+14,188);
    DrawText(FString::Printf(TEXT("RECIPES %d-%d / %d | Up/Down | Enter craft"),Start+1,FMath::Min(Start+5,Catalog->Recipes.Num()),Catalog->Recipes.Num()),FLinearColor::White,Left+14,212);
    for(int32 Index=Start;Index<FMath::Min(Start+5,Catalog->Recipes.Num());++Index)
    {
        const auto Color=Index==Player->GetBackpackRecipe()?FLinearColor::Yellow:FLinearColor::White;
        DrawText(FString::Printf(TEXT("%s%d. %s | %s"),Index==Player->GetBackpackRecipe()?TEXT("> "):TEXT("  "),Index+1,*Catalog->Recipes[Index].DisplayName.ToString(),*UCraftingCatalog::FormatItems(Catalog->Recipes[Index].Inputs)),Color,Left+14,238+(Index-Start)*56);
        DrawWrappedText(Player->GetBackpackRecipeStatus(Index),Color,Left+22,260+(Index-Start)*56,430,1);
    }
    if(Catalog->Recipes.IsValidIndex(Player->GetBackpackRecipe())) DrawWrappedText(Catalog->Recipes[Player->GetBackpackRecipe()].Description.ToString(),FLinearColor::Green,Left+14,536,440,2);
    DrawText(TEXT("1-5 also craft | 6 / 7 toggle owned gear"),FLinearColor::White,Left+14,586);
    DrawText(TEXT("No auto-save. F5 saves items and equipment."),FLinearColor::White,Left+14,612);
}
