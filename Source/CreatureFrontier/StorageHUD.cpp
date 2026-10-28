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
    const float Extra=(static_cast<int32>(EFrontierItem::Count)-6)*38;
    DrawRect(FLinearColor(.015f,.025f,.04f,.98f),Left,18,470,470+Extra);
    DrawText(TEXT("储物箱 | E 关闭 | 世界仍在运行"),FLinearColor::Yellow,Left+14,32);
    DrawText(TEXT("物品                  背包     箱内 / 999"),FLinearColor::White,Left+14,68);
    for(int32 Index=0;Index<static_cast<int32>(EFrontierItem::Count);++Index)
    {
        const auto Item=static_cast<EFrontierItem>(Index);
        if(Index==Player->Storage->GetSelection()) DrawRect(FLinearColor(.08f,.26f,.32f,1),Left+8,96+Index*38,454,36);
        DrawText(UCraftingCatalog::ItemName(Item),FLinearColor::White,Left+16,104+Index*38);
        DrawText(FString::Printf(TEXT("%d       %d"),Player->Inventory->Count(Item),Crate->StorageInventory->Count(Item)),FLinearColor::Yellow,Left+260,104+Index*38);
    }
    DrawText(TEXT("上下选择 | Enter 存入1个"),FLinearColor::White,Left+14,344+Extra);
    DrawText(TEXT("退格键 取出1个"),FLinearColor::White,Left+14,372+Extra);
    DrawText(TEXT("右方向键整组存入 | 左方向键整组取出"),FLinearColor::White,Left+14,400+Extra);
    DrawText(TEXT("整组转移仅在容量足够时进行。F5 保存。"),FLinearColor::Green,Left+14,428+Extra);
    DrawText(TEXT("拆除前请先清空箱子。"),FLinearColor::Green,Left+14,454+Extra);
}
