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
    // 小窗口减少每页配方数，不缩小汉字；方向键仍可遍历全部配方。
    const float Extra=FMath::Max(0,(Catalog->Items.Num()+1)/2-3)*27;
    const float Guidance=42;
    const int32 PageSize=FMath::Clamp(5-FMath::CeilToInt(FMath::Max(0.f,666.f+Extra+Guidance-Canvas->ClipY)/56.f),1,5);
    const int32 Start=(Player->GetBackpackRecipe()/PageSize)*PageSize;
    const float Compact=(5-PageSize)*56.f;
    DrawRect(FLinearColor(.015f,.025f,.04f,.97f),Left,18,470,630+Extra+Guidance-Compact);
    DrawRect(FLinearColor(.08f,.26f,.32f,1),Left+10,232+Extra+(Player->GetBackpackRecipe()-Start)*56,450,52);
    DrawText(TEXT("背包与制作 | C 关闭"),FLinearColor::White,Left+14,30);
    DrawText(TEXT("世界仍在运行 | 退出键打开暂停菜单"),FLinearColor::Yellow,Left+14,56);
    for(int32 Index=0;Index<Catalog->Items.Num();++Index)
        DrawText(FString::Printf(TEXT("%s: %d / 999"),*Catalog->Items[Index].DisplayName.ToString(),Player->Inventory->Count(Catalog->Items[Index].Item)),FLinearColor::White,Left+14+(Index%2)*220,88+(Index/2)*27);
    const auto& Gear=Player->Equipment->GetState();
    DrawText(FString::Printf(TEXT("长矛：%s | 护甲：%s"),Gear.bSpearEquipped?TEXT("已装备"):Gear.bOwnSpear?TEXT("已收纳"):TEXT("未拥有"),Gear.bVestEquipped?TEXT("已装备"):Gear.bOwnVest?TEXT("已收纳"):TEXT("未拥有")),FLinearColor::Green,Left+14,188+Extra);
    DrawText(FString::Printf(TEXT("配方 %d-%d / %d | 上下选择 | 回车制作"),Start+1,FMath::Min(Start+PageSize,Catalog->Recipes.Num()),Catalog->Recipes.Num()),FLinearColor::White,Left+14,212+Extra);
    for(int32 Index=Start;Index<FMath::Min(Start+PageSize,Catalog->Recipes.Num());++Index)
    {
        const auto Color=Index==Player->GetBackpackRecipe()?FLinearColor::Yellow:FLinearColor::White;
        DrawText(FString::Printf(TEXT("%s%d. %s | %s"),Index==Player->GetBackpackRecipe()?TEXT("> "):TEXT("  "),Index+1,*Catalog->Recipes[Index].DisplayName.ToString(),*UCraftingCatalog::FormatItems(Catalog->Recipes[Index].Inputs)),Color,Left+14,238+Extra+(Index-Start)*56);
        DrawWrappedText(Player->GetBackpackRecipeStatus(Index),Color,Left+22,260+Extra+(Index-Start)*56,430,1);
    }
    if(Catalog->Recipes.IsValidIndex(Player->GetBackpackRecipe())) DrawWrappedText(Catalog->Recipes[Player->GetBackpackRecipe()].Description.ToString(),FLinearColor::Green,Left+14,536+Extra-Compact,440,2);
    DrawWrappedText(Player->GetBackpackGatheringHint(Player->GetBackpackRecipe()),FLinearColor(.5f,.85f,1),Left+14,576+Extra-Compact,440,2);
    DrawText(TEXT("1-5 快捷制作 | 6 / 7 切换已有装备"),FLinearColor::White,Left+14,586+Extra+Guidance-Compact);
    DrawText(TEXT("不会自动保存，按 F5 保存物品和装备。"),FLinearColor::White,Left+14,612+Extra+Guidance-Compact);
}
