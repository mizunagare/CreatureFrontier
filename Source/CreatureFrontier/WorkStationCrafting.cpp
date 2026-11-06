#include "WorkStation.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CraftingCatalog.h"
#include "InventoryComponent.h"
#include "ResearchComponent.h"
#include "BaseBuildingComponent.h"
#include "FrontierSaveGame.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

bool AWorkStation::ValidateCraft(const FFacilitySaveRecord& Record)
{
    if(Record.CraftStockRevision>1 || !UInventoryComponent::Validate(Record.CraftStock) ||
        ((Record.CraftStockRevision==0 || Record.Specialty!=EWorkSpecialty::Crafting) && Record.CraftStock.Num()!=0))return false;
    if(Record.CraftRevision<0 || Record.CraftRevision>2 || !FMath::IsFinite(Record.CraftWork) || Record.CraftWork<0 || Record.CraftWork>RequiredCraftWork)return false;
    if(Record.bCraftUsesLocalStock!=(Record.CraftRevision==2) || (Record.bCraftUsesLocalStock && (Record.CraftStockRevision!=1 || Record.CraftRecipe.IsNone())))return false;
    if(Record.CraftRecipe.IsNone())return Record.CraftWork==0;
    if(Record.CraftRevision<1 || Record.Specialty!=EWorkSpecialty::Crafting)return false;
    const auto* Catalog=UCraftingCatalog::Load();const auto* Recipe=Catalog?Catalog->Find(Record.CraftRecipe):nullptr;
    return Recipe && Catalog->Validate() && Recipe->Reward==ERecipeReward::Item && (!Recipe->bBaseOnly || FVector::Dist2D(Record.Transform.GetLocation(),UBaseBuildingComponent::BaseOrigin())<=800);
}

int32 AWorkStation::CraftFoodDemand(ACFPlayerCharacter* Hero) const
{
    // 只补当前合法委托的缺口，不把设施当作无限囤粮仓库；旧背包委托不参与。
    if(IsActorBeingDestroyed() || !bPlayerBuilt || !PersistentId.IsValid() || RequiredSpecialty!=EWorkSpecialty::Crafting || !bCraftUsesLocalStock || CraftRecipeId.IsNone() || !IsValid(Hero) || Hero->GetWorld()!=GetWorld() || Hero->Health<=0 || !IsValid(CraftInventory) || !UInventoryComponent::Validate(CraftInventory->GetStacks()))return 0;
    const auto* Catalog=UCraftingCatalog::Load();const auto* Recipe=Catalog?Catalog->Find(CraftRecipeId):nullptr;
    if(!Recipe || !Catalog->Validate() || Recipe->Reward!=ERecipeReward::Item || !Hero->Research->Has(Recipe->RequiredResearch) || (Recipe->bBaseOnly && FVector::Dist2D(GetActorLocation(),UBaseBuildingComponent::BaseOrigin())>800))return 0;
    return FMath::Max(0,Recipe->Inputs.FindRef(EFrontierItem::Food)-CraftInventory->Count(EFrontierItem::Food));
}

bool AWorkStation::ToggleCraftOrder(ACFPlayerCharacter* Hero,FName Id)
{
    if(!HasAuthority() || IsActorBeingDestroyed() || !bPlayerBuilt || RequiredSpecialty!=EWorkSpecialty::Crafting || !IsValid(Hero) || Hero->GetWorld()!=GetWorld() || Hero->Health<=0 || !Hero->IsBackpackOpen() || UGameplayStatics::IsGamePaused(this) || Hero->GetDistanceTo(this)>300 || Hero->TraceFacility()!=this)return false;
    if(!CraftRecipeId.IsNone())
    {
        CraftRecipeId=NAME_None;CraftWork=0;bCraftUsesLocalStock=false;CraftWorker.Reset();
        for(TActorIterator<ACreatureCharacter> It(GetWorld());It;++It)if(It->WorkSite==this && It->Tamer==Hero->GetController() && (It->Activity==ECreatureActivity::Work || It->bResumeWorkAfterRest)){It->bResumeWorkAfterRest=false;It->SetActivity(ECreatureActivity::Rest);}
        Hero->Feedback=TEXT("已取消加工委托，未消耗原料；设施物资保留");return true;
    }
    const auto* Catalog=UCraftingCatalog::Load();const auto* Recipe=Catalog?Catalog->Find(Id):nullptr;
    if(!Recipe || !Catalog->Validate() || Recipe->Reward!=ERecipeReward::Item){Hero->Feedback=TEXT("伙伴仅能加工物品，研究和装备请自行制作");return false;}
    if(!Hero->Research->Has(Recipe->RequiredResearch)){Hero->Feedback=TEXT("尚未解锁此加工配方");return false;}
    if(Recipe->bBaseOnly && FVector::Dist2D(GetActorLocation(),UBaseBuildingComponent::BaseOrigin())>800){Hero->Feedback=TEXT("此配方需要初始基地内的制作设施");return false;}
    CraftRecipeId=Id;CraftWork=0;bCraftUsesLocalStock=true;CraftWorker.Reset();Hero->Feedback=TEXT("已委托：")+Recipe->DisplayName.ToString()+TEXT(" | 从设施库存取料，产物留在设施；关闭C后E派工");return true;
}

FString AWorkStation::CraftStatus() const
{
    if(CraftRecipeId.IsNone())return TEXT("C选配方，Shift+回车委托；C面板内E存取设施物资");
    const auto* Catalog=UCraftingCatalog::Load();const auto* Recipe=Catalog?Catalog->Find(CraftRecipeId):nullptr;
    return FString::Printf(TEXT("委托 %s %.1f/8\n%s"),Recipe?*Recipe->DisplayName.ToString():TEXT("配方不可用"),CraftWork,bCraftUsesLocalStock?TEXT("设施缺料或满格时等待，产物留在设施"):TEXT("旧委托：完工扣背包原料，产物入背包"));
}

bool AWorkStation::WorkCraft(ACreatureCharacter* Pet,ACFPlayerCharacter* Hero,float Amount)
{
    if(!HasAuthority() || IsActorBeingDestroyed() || !bPlayerBuilt || RequiredSpecialty!=EWorkSpecialty::Crafting || CraftRecipeId.IsNone() || !IsValid(Pet) || !IsValid(Hero) || Pet->GetWorld()!=GetWorld() || Hero->GetWorld()!=GetWorld() || Hero->Health<=0 || Pet->Health<=0 || Pet->Disposition!=ECreatureDisposition::Tamed || Pet->Tamer!=Hero->GetController() || Pet->Activity!=ECreatureActivity::Work || Pet->WorkSite!=this || !FMath::IsFinite(Amount) || Amount<=0 || Pet->GetDistanceTo(this)>220 || UGameplayStatics::IsGamePaused(this))return false;
    FHitResult Hit;FCollisionQueryParams Params(SCENE_QUERY_STAT(CraftWorkerSight),false,Pet);
    if(GetWorld()->LineTraceSingleByChannel(Hit,Pet->GetActorLocation(),GetActorLocation()+FVector(0,0,30),ECC_Visibility,Params) && Hit.GetActor()!=this)return false;
    auto* Other=CraftWorker.Get();if(IsValid(Other) && Other!=Pet && Other->Health>0 && Other->Activity==ECreatureActivity::Work && Other->WorkSite==this)return false;
    const auto* Catalog=UCraftingCatalog::Load();const auto* Recipe=Catalog?Catalog->Find(CraftRecipeId):nullptr;
    if(!Recipe || !Catalog->Validate() || Recipe->Reward!=ERecipeReward::Item || !Hero->Research->Has(Recipe->RequiredResearch) || (Recipe->bBaseOnly && FVector::Dist2D(GetActorLocation(),UBaseBuildingComponent::BaseOrigin())>800))return false;
    auto* Stock=bCraftUsesLocalStock?CraftInventory.Get():Hero->Inventory.Get();
    if(!IsValid(Stock) || !UInventoryComponent::Validate(Stock->GetStacks()))return false;
    for(const auto& Cost:Recipe->Inputs)if(Stock->Count(Cost.Key)<Cost.Value)return false;
    if(Recipe->OutputAmount>UInventoryComponent::StackLimit-(Stock->Count(Recipe->Output)-Recipe->Inputs.FindRef(Recipe->Output)))return false;
    CraftWorker=Pet;CraftWork=FMath::Min(RequiredCraftWork,CraftWork+Amount);
    if(CraftWork>=RequiredCraftWork && Stock->Transact(Recipe->Inputs,{{Recipe->Output,Recipe->OutputAmount}}))
    {
        Hero->Feedback=TEXT("伙伴加工完成：")+Recipe->DisplayName.ToString()+(bCraftUsesLocalStock?TEXT("，产物已存入制作设施"):TEXT("，产物已放入背包"));
        Hero->RecordCompanionProduction(Pet,Recipe->Output);Pet->AddExperience(1);CraftRecipeId=NAME_None;CraftWork=0;bCraftUsesLocalStock=false;CraftWorker.Reset();Pet->bResumeWorkAfterRest=false;Pet->SetActivity(ECreatureActivity::Rest);
    }
    return true;
}
