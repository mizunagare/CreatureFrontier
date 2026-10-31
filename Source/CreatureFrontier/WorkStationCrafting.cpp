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
    if(Record.CraftRevision<0 || Record.CraftRevision>1 || !FMath::IsFinite(Record.CraftWork) || Record.CraftWork<0 || Record.CraftWork>RequiredCraftWork)return false;
    if(Record.CraftRecipe.IsNone())return Record.CraftWork==0;
    if(Record.CraftRevision!=1 || Record.Specialty!=EWorkSpecialty::Crafting)return false;
    const auto* Catalog=UCraftingCatalog::Load();const auto* Recipe=Catalog?Catalog->Find(Record.CraftRecipe):nullptr;
    return Recipe && Catalog->Validate() && Recipe->Reward==ERecipeReward::Item && (!Recipe->bBaseOnly || FVector::Dist2D(Record.Transform.GetLocation(),UBaseBuildingComponent::BaseOrigin())<=800);
}

bool AWorkStation::ToggleCraftOrder(ACFPlayerCharacter* Hero,FName Id)
{
    if(!HasAuthority() || IsActorBeingDestroyed() || !bPlayerBuilt || RequiredSpecialty!=EWorkSpecialty::Crafting || !IsValid(Hero) || Hero->GetWorld()!=GetWorld() || Hero->Health<=0 || !Hero->IsBackpackOpen() || UGameplayStatics::IsGamePaused(this) || Hero->GetDistanceTo(this)>300 || Hero->TraceFacility()!=this)return false;
    if(!CraftRecipeId.IsNone())
    {
        CraftRecipeId=NAME_None;CraftWork=0;CraftWorker.Reset();
        for(TActorIterator<ACreatureCharacter> It(GetWorld());It;++It)if(It->WorkSite==this && It->Tamer==Hero->GetController() && (It->Activity==ECreatureActivity::Work || It->bResumeWorkAfterRest)){It->bResumeWorkAfterRest=false;It->SetActivity(ECreatureActivity::Rest);}
        Hero->Feedback=TEXT("已取消加工委托，未消耗原料");return true;
    }
    const auto* Catalog=UCraftingCatalog::Load();const auto* Recipe=Catalog?Catalog->Find(Id):nullptr;
    if(!Recipe || !Catalog->Validate() || Recipe->Reward!=ERecipeReward::Item){Hero->Feedback=TEXT("伙伴仅能加工物品，研究和装备请自行制作");return false;}
    bool Allowed=false;Hero->Feedback=Hero->GetRecipeStatus(*Recipe,&Allowed);if(!Allowed)return false;
    if(Recipe->bBaseOnly && FVector::Dist2D(GetActorLocation(),UBaseBuildingComponent::BaseOrigin())>800){Hero->Feedback=TEXT("此配方需要初始基地内的制作设施");return false;}
    CraftRecipeId=Id;CraftWork=0;CraftWorker.Reset();Hero->Feedback=TEXT("已委托：")+Recipe->DisplayName.ToString()+TEXT(" | E 指派伙伴，完工时扣料");return true;
}

FString AWorkStation::CraftStatus() const
{
    if(CraftRecipeId.IsNone())return TEXT("C 选配方，瞄准设施 Shift+回车委托");
    const auto* Catalog=UCraftingCatalog::Load();const auto* Recipe=Catalog?Catalog->Find(CraftRecipeId):nullptr;
    return FString::Printf(TEXT("委托 %s %.1f/8\n缺料或满格时等待，完工扣料"),Recipe?*Recipe->DisplayName.ToString():TEXT("配方不可用"),CraftWork);
}

bool AWorkStation::WorkCraft(ACreatureCharacter* Pet,ACFPlayerCharacter* Hero,float Amount)
{
    if(!HasAuthority() || IsActorBeingDestroyed() || !bPlayerBuilt || RequiredSpecialty!=EWorkSpecialty::Crafting || CraftRecipeId.IsNone() || !IsValid(Pet) || !IsValid(Hero) || Pet->GetWorld()!=GetWorld() || Hero->GetWorld()!=GetWorld() || Hero->Health<=0 || Pet->Health<=0 || Pet->Disposition!=ECreatureDisposition::Tamed || Pet->Tamer!=Hero->GetController() || Pet->Activity!=ECreatureActivity::Work || Pet->WorkSite!=this || !FMath::IsFinite(Amount) || Amount<=0 || Pet->GetDistanceTo(this)>220 || UGameplayStatics::IsGamePaused(this))return false;
    FHitResult Hit;FCollisionQueryParams Params(SCENE_QUERY_STAT(CraftWorkerSight),false,Pet);
    if(GetWorld()->LineTraceSingleByChannel(Hit,Pet->GetActorLocation(),GetActorLocation()+FVector(0,0,30),ECC_Visibility,Params) && Hit.GetActor()!=this)return false;
    auto* Other=CraftWorker.Get();if(IsValid(Other) && Other!=Pet && Other->Health>0 && Other->Activity==ECreatureActivity::Work && Other->WorkSite==this)return false;
    const auto* Catalog=UCraftingCatalog::Load();const auto* Recipe=Catalog?Catalog->Find(CraftRecipeId):nullptr;
    if(!Recipe || !Catalog->Validate() || Recipe->Reward!=ERecipeReward::Item || !Hero->Research->Has(Recipe->RequiredResearch) || (Recipe->bBaseOnly && FVector::Dist2D(GetActorLocation(),UBaseBuildingComponent::BaseOrigin())>800))return false;
    for(const auto& Cost:Recipe->Inputs)if(Hero->Inventory->Count(Cost.Key)<Cost.Value)return false;
    if(Recipe->OutputAmount>UInventoryComponent::StackLimit-(Hero->Inventory->Count(Recipe->Output)-Recipe->Inputs.FindRef(Recipe->Output)))return false;
    CraftWorker=Pet;CraftWork=FMath::Min(RequiredCraftWork,CraftWork+Amount);
    if(CraftWork>=RequiredCraftWork && Hero->Inventory->Transact(Recipe->Inputs,{{Recipe->Output,Recipe->OutputAmount}}))
    {
        Hero->RecordCompanionProduction(Pet,Recipe->Output);Pet->AddExperience(1);CraftRecipeId=NAME_None;CraftWork=0;CraftWorker.Reset();Pet->bResumeWorkAfterRest=false;Pet->SetActivity(ECreatureActivity::Rest);Hero->Feedback=TEXT("伙伴加工完成：")+Recipe->DisplayName.ToString();
    }
    return true;
}
