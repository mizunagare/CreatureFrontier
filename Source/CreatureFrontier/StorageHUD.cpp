#include "CFHUD.h"
#include "CFPlayerCharacter.h"
#include "StorageInteractionComponent.h"
#include "HomeModule.h"
#include "InventoryComponent.h"
#include "CraftingCatalog.h"
#include "Engine/Canvas.h"

void ACFHUD::DrawStorage(ACFPlayerCharacter* Player)
{
    auto* Crate=Player->Storage->GetCrate(); const auto* Catalog=UCraftingCatalog::Load(); if(!Crate || !Catalog) return;
    const float Left=Canvas->ClipX-488;
    DrawRect(FLinearColor(.015f,.025f,.04f,.98f),Left,18,470,470);
    DrawText(TEXT("STORAGE CRATE | E close | WORLD LIVE"),FLinearColor::Yellow,Left+14,32);
    DrawText(TEXT("ITEM                  PACK     CRATE / 999"),FLinearColor::White,Left+14,68);
    for(int32 Index=0;Index<static_cast<int32>(EFrontierItem::Count);++Index)
    {
        const auto Item=static_cast<EFrontierItem>(Index);
        if(Index==Player->Storage->GetSelection()) DrawRect(FLinearColor(.08f,.26f,.32f,1),Left+8,96+Index*38,454,36);
        DrawText(UCraftingCatalog::ItemName(Item),FLinearColor::White,Left+16,104+Index*38);
        DrawText(FString::Printf(TEXT("%d       %d"),Player->Inventory->Count(Item),Crate->StorageInventory->Count(Item)),FLinearColor::Yellow,Left+260,104+Index*38);
    }
    DrawText(TEXT("Up/Down select | Enter deposit 1"),FLinearColor::White,Left+14,344);
    DrawText(TEXT("Backspace withdraw 1"),FLinearColor::White,Left+14,372);
    DrawText(TEXT("Right deposit stack | Left withdraw stack"),FLinearColor::White,Left+14,400);
    DrawText(TEXT("Whole stack is all-or-nothing. F5 saves."),FLinearColor::Green,Left+14,428);
    DrawText(TEXT("Empty crate before dismantling."),FLinearColor::Green,Left+14,454);
}
