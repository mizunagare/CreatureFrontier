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
    const int32 ItemCount=static_cast<int32>(EFrontierItem::Count);
    const float RowHeight=FMath::Clamp((Canvas->ClipY-260.f)/ItemCount,26.f,38.f);
    const float FooterY=106.f+ItemCount*RowHeight;
    // 表头与数值共用列坐标，不能用空格推算中文字体宽度。
    const float BackpackX=Left+240.f, CrateX=Left+346.f;
    DrawRect(FLinearColor(.015f,.025f,.04f,.98f),Left,18,470,FooterY+108.f);
    DrawText(TEXT("储物箱 | E 关闭 | 世界仍在运行"),FLinearColor::Yellow,Left+14,32);
    DrawText(TEXT("物品"),FLinearColor::White,Left+16,68);
    DrawText(TEXT("背包"),FLinearColor::White,BackpackX,68);
    DrawText(TEXT("箱内 / 999"),FLinearColor::White,CrateX,68);
    for(int32 Index=0;Index<ItemCount;++Index)
    {
        const auto Item=static_cast<EFrontierItem>(Index);
        if(Index==Player->Storage->GetSelection()) DrawRect(FLinearColor(.08f,.26f,.32f,1),Left+8,96+Index*RowHeight,454,RowHeight-2);
        DrawText(UCraftingCatalog::ItemName(Item),FLinearColor::White,Left+16,104+Index*RowHeight);
        DrawText(FString::FromInt(Player->Inventory->Count(Item)),FLinearColor::Yellow,BackpackX,104+Index*RowHeight);
        DrawText(FString::FromInt(Crate->StorageInventory->Count(Item)),FLinearColor::Yellow,CrateX,104+Index*RowHeight);
    }
    DrawText(TEXT("上下选择 | Enter 存入1个"),FLinearColor::White,Left+14,FooterY);
    DrawText(TEXT("退格键 取出1个"),FLinearColor::White,Left+14,FooterY+26);
    DrawText(TEXT("右方向键整组存入 | 左方向键整组取出"),FLinearColor::White,Left+14,FooterY+52);
    DrawText(TEXT("整组转移仅在容量足够时进行。F5 保存。"),FLinearColor::Green,Left+14,FooterY+78);
    DrawText(TEXT("拆除前请先清空箱子。"),FLinearColor::Green,Left+14,FooterY+104);
}
