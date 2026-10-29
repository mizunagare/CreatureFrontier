#include "CFPlayerCharacter.h"
#include "CraftingCatalog.h"
#include "ResearchComponent.h"
#include "InventoryComponent.h"
#include "PlayerEquipmentComponent.h"
#include "BaseBuildingComponent.h"
#include "Kismet/GameplayStatics.h"

FString ACFPlayerCharacter::GetRecipeStatus(const FFrontierRecipe& Recipe,bool* bCanCraft) const
{
    if(bCanCraft)*bCanCraft=false;
    if(Health<=0) return TEXT("已倒下，请复活后再制作");
    const auto& Gear=Equipment->GetState();
    if((Recipe.Reward==ERecipeReward::Spear && Gear.bOwnSpear) || (Recipe.Reward==ERecipeReward::Vest && Gear.bOwnVest)) return TEXT("已拥有，按6/7装备或卸下");
    if(Recipe.Reward==ERecipeReward::CrystalStudy && Research->Has(EFrontierResearch::CrystalStudy)) return TEXT("已完成水晶研究");
    if(!Research->Has(Recipe.RequiredResearch)) return TEXT("尚未解锁：")+Research->GetHint();
    if(Recipe.Reward==ERecipeReward::Item && Recipe.OutputAmount>UInventoryComponent::StackLimit-(Inventory->Count(Recipe.Output)-Recipe.Inputs.FindRef(Recipe.Output))) return TEXT("产物已满，每种物品最多999个");
    if(!IsNearWorkStation()) return TEXT("需要位于8米内的工作设施");
    if(Recipe.bBaseOnly && FVector::Dist2D(GetActorLocation(),UBaseBuildingComponent::BaseOrigin())>800) return TEXT("请回初始基地工作台加工、研究或制作");
    if(auto* Catalog=UCraftingCatalog::Load()) for(const auto& Item:Catalog->Items)
    {
        const int32 Missing=Recipe.Inputs.FindRef(Item.Item)-Inventory->Count(Item.Item);
        if(Missing>0) return FString::Printf(TEXT("还需要%d份%s"),Missing,*Item.DisplayName.ToString());
    }
    if(bCanCraft)*bCanCraft=true;
    return TEXT("可以制作，按回车制作一个");
}
bool ACFPlayerCharacter::CraftRecipe(FName RecipeId)
{
    if(!HasAuthority() || Health<=0 || UGameplayStatics::IsGamePaused(this)) return false;
    const auto* Catalog=UCraftingCatalog::Load(); const auto* Recipe=Catalog?Catalog->Find(RecipeId):nullptr;
    if(!Recipe || !Catalog->Validate()) { Feedback=TEXT("制作目录不可用或无效"); return false; }
    // 事务许可与界面文案分离，后续润色中文不会改变制作条件。
    bool bCanCraft=false;Feedback=GetRecipeStatus(*Recipe,&bCanCraft);if(!bCanCraft)return false;
    TMap<EFrontierItem,int32> Outputs;
    if(Recipe->Reward==ERecipeReward::Item) Outputs.Add(Recipe->Output,Recipe->OutputAmount);
    if(!Inventory->Transact(Recipe->Inputs,Outputs)) { Feedback=TEXT("制作事务失败，未消耗物品"); return false; }
    // 上面的预检查保证这些单线程唯一状态变更不会失败。
    if(Recipe->Reward==ERecipeReward::Spear) Equipment->GrantCrafted(true);
    if(Recipe->Reward==ERecipeReward::Vest) Equipment->GrantCrafted(false);
    if(Recipe->Reward==ERecipeReward::CrystalStudy) Research->CompleteCrystalStudy();
    Feedback=TEXT("制作完成：")+Recipe->DisplayName.ToString();
    return true;
}
