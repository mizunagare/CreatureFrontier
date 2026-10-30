#include "CFPlayerCharacter.h"
#include "InventoryComponent.h"
#include "PlayerEquipmentComponent.h"
#include "BaseBuildingComponent.h"
#include "CraftingCatalog.h"
#include "ResourceNode.h"
#include "ResourceDefinition.h"
#include "ResearchComponent.h"
#include "EngineUtils.h"

void ACFPlayerCharacter::ToggleBackpack()
{
    if(bPauseMenu || Health<=0) return;
    bBackpackOpen=!bBackpackOpen;
    if(bBackpackOpen)
    {
        CancelMeleeAttack(); Building->Cancel();
        bRosterOpen=false; bJournalOpen=false; bHelpVisible=false;
        BackpackRecipe=0;
        Feedback=TEXT("背包 | 上下选择配方 | 回车制作一个 | C 关闭 | 世界仍运行");
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

AResourceNode* ACFPlayerCharacter::GetBackpackGatheringSource(int32 Index) const
{
    const auto* Catalog=UCraftingCatalog::Load();if(!Catalog || !Catalog->Recipes.IsValidIndex(Index))return nullptr;
    const auto& Recipe=Catalog->Recipes[Index];const auto& Gear=Equipment->GetState();
    if((Recipe.Reward==ERecipeReward::Spear && Gear.bOwnSpear) || (Recipe.Reward==ERecipeReward::Vest && Gear.bOwnVest) ||
        (Recipe.Reward==ERecipeReward::CrystalStudy && Research->Has(EFrontierResearch::CrystalStudy)))return nullptr;
    for(const auto& Item:Catalog->Items)if(Inventory->Count(Item.Item)<Recipe.Inputs.FindRef(Item.Item))
    {
        AResourceNode* Best=nullptr;AResourceNode* Regrowing=nullptr;double Distance=DBL_MAX,RegrowDistance=DBL_MAX;
        for(TActorIterator<AResourceNode> It(GetWorld());It;++It)
            if(!It->IsActorBeingDestroyed() && It->Definition && It->Definition->IsValidDefinition() && It->Definition->OutputItem==Item.Item)
            {
                const double Current=FVector::DistSquared(GetActorLocation(),It->GetActorLocation());
                if(It->Remaining>0 && Current<Distance){Best=*It;Distance=Current;}
                else if(It->Remaining==0 && It->Definition->RegrowSeconds>0 && It->RegrowRemaining>0 && Current<RegrowDistance){Regrowing=*It;RegrowDistance=Current;}
            }
        return Best?Best:Regrowing;
    }
    return nullptr;
}

FString ACFPlayerCharacter::GetBackpackGatheringHint(int32 Index) const
{
    const auto* Catalog=UCraftingCatalog::Load();if(!Catalog || !Catalog->Recipes.IsValidIndex(Index))return TEXT("配方线索不可用");
    const auto& Recipe=Catalog->Recipes[Index];const auto& Gear=Equipment->GetState();
    if((Recipe.Reward==ERecipeReward::Spear && Gear.bOwnSpear) || (Recipe.Reward==ERecipeReward::Vest && Gear.bOwnVest))return TEXT("已拥有此装备，无需重复采料；按6/7切换装备");
    if(Recipe.Reward==ERecipeReward::CrystalStudy && Research->Has(EFrontierResearch::CrystalStudy))return TEXT("研究已完成，可制作共鸣工具和水晶灯");
    TArray<FString> Missing;EFrontierItem First=EFrontierItem::Material;
    for(const auto& Item:Catalog->Items)
    {
        const int32 Count=Recipe.Inputs.FindRef(Item.Item)-Inventory->Count(Item.Item);
        if(Count>0){if(Missing.IsEmpty())First=Item.Item;Missing.Add(Item.DisplayName.ToString()+FString::FromInt(Count));}
    }
    if(Missing.IsEmpty())return TEXT("配料齐备，请确认上方设施范围与研究许可");
    const FString Budget=TEXT("缺料：")+FString::Join(Missing,TEXT("、"))+TEXT("\n");
    if(const auto* Source=GetBackpackGatheringSource(Index))
    {
        const FVector Delta=Source->GetActorLocation()-GetActorLocation();const double Distance=Delta.Size();
        if(Distance<=300)return Budget+Source->Definition->DisplayName.ToString()+(Source->Remaining>0?TEXT("在3米内，瞄准后按住M采集"):*FString::Printf(TEXT("等待再生，约%d秒"),FMath::CeilToInt(Source->RegrowRemaining)));
        const TCHAR* Directions[]={TEXT("东"),TEXT("东北"),TEXT("北"),TEXT("西北"),TEXT("西"),TEXT("西南"),TEXT("南"),TEXT("东南")};
        const int32 Sector=(FMath::RoundToInt(FMath::Atan2(Delta.Y,Delta.X)/(PI/4))+8)%8;
        const TCHAR* Direction=Delta.Size2D()<100 && FMath::Abs(Delta.Z)>300?(Delta.Z>0?TEXT("上方"):TEXT("下方")):Directions[Sector];
        const FString Regen=Source->Remaining>0?FString():FString::Printf(TEXT("，再生约%d秒"),FMath::CeilToInt(Source->RegrowRemaining));
        return Budget+FString::Printf(TEXT("采集线索：%s，%s%d米（直线）%s"),*Source->Definition->DisplayName.ToString(),Direction,FMath::CeilToInt(Distance/100),*Regen);
    }
    if(First==EFrontierItem::Food)return Budget+TEXT("食物可种植收获，或在设施附近按3制作");
    return Budget+TEXT("当前无可采来源，请探索其他来源或加工途径");
}
