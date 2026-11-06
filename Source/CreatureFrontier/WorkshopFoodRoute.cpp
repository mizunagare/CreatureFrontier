#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "WorkStation.h"
#include "InventoryComponent.h"
#include "Containers/Ticker.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"

void RunWorkshopFoodRoute(UWorld* World,ACFPlayerCharacter* Hero)
{
    // 明确投入测试粮与委托；不冒称玩家正常来源。观察期间不瞬移伙伴、冻结AI或关闭碰撞。
    const bool Blocked=FParse::Param(FCommandLine::Get(),TEXT("CFWorkshopFoodBlocked"));
    const bool Reserve=FParse::Param(FCommandLine::Get(),TEXT("CFWorkshopFoodReserve"));
    const bool Cancel=FParse::Param(FCommandLine::Get(),TEXT("CFWorkshopFoodCancel"));
    const bool Removed=FParse::Param(FCommandLine::Get(),TEXT("CFWorkshopFoodRemoved"));
    auto MakeSite=[&](FVector Location,EWorkSpecialty Kind){auto* S=World->SpawnActor<AWorkStation>(Location,FRotator::ZeroRotator);S->RequiredSpecialty=Kind;S->bPlayerBuilt=true;S->PersistentId=FGuid::NewGuid();S->RefreshFarm();return S;};
    auto* Post=MakeSite(FVector(-1100,-1000,0),EWorkSpecialty::Transport);
    auto* Farm=MakeSite(FVector(-1400,-1000,0),EWorkSpecialty::Farming);
    auto* Trough=MakeSite(FVector(-1100,-500,0),EWorkSpecialty::Feeding);
    auto* Workshop=MakeSite(FVector(-650,-650,Blocked?700:0),EWorkSpecialty::Crafting);
    Workshop->CraftRecipeId=TEXT("PlantingSeeds");Workshop->bCraftUsesLocalStock=true;
    // 取消夹具仅初始化玩家站位；事件发生时调用真实委托取消入口，不移动运输伙伴。
    if(Cancel)Hero->SetActorLocation(FVector(-650,-850,40));
    Farm->StoredFood=3;Trough->StoredFood=Reserve?0:2;
    auto* Pet=World->SpawnActor<ACreatureCharacter>(FVector(-1100,-1300,40),FRotator::ZeroRotator);
    Pet->Tamer=Hero->GetController();Pet->Disposition=ECreatureDisposition::Tamed;Pet->Activity=ECreatureActivity::Work;Pet->WorkSite=Post;Pet->Nutrition=100;
    struct FState{double Start=0,LastTime=0,Travel=0;FVector Last;bool Picked=false,Changed=false,ChangeValid=true;};
    auto S=MakeShared<FState>();S->Start=S->LastTime=World->GetTimeSeconds();S->Last=Pet->GetActorLocation();
    const int32 Total=Farm->StoredFood+Trough->StoredFood;
    const bool Demand=Workshop->CraftFoodDemand(Hero)==1;
    UE_LOG(LogTemp,Display,TEXT("CF_WORKSHOP_FOOD_FIXTURE demand=%d blocked=%d reserve=%d total=%d"),Demand,Blocked,Reserve,Total);
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([World,Hero,Pet,Post,Farm,Trough,Workshop,S,Total,Demand,Blocked,Reserve,Cancel,Removed](float)
    {
        const double Now=World->GetTimeSeconds();const FVector Position=Pet->GetActorLocation();
        const double Step=FVector::Dist(S->Last,Position);
        const bool Continuous=Step<=Pet->GetCharacterMovement()->GetMaxSpeed()*(Now-S->LastTime)+10;
        S->Last=Position;S->LastTime=Now;S->Travel+=Step;S->Picked |= Pet->CarriedFood>0;
        if((Cancel || Removed) && !S->Changed && Pet->CarriedFood>0 && Pet->GetDistanceTo(Farm)>230 && Pet->GetDistanceTo(Workshop)>300)
        {
            const int32 Cargo=Pet->CarriedFood;
            S->ChangeValid=Workshop->CraftInventory->Count(EFrontierItem::Food)==0;
            if(Cancel)
            {
                Hero->FirstPersonCamera->SetWorldRotation((Workshop->GetActorLocation()+FVector(0,0,30)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());
                Hero->ToggleBackpack();
                S->ChangeValid &= Workshop->ToggleCraftOrder(Hero,TEXT("PlantingSeeds")) && Workshop->CraftRecipeId.IsNone() && !Workshop->bCraftUsesLocalStock;
                Hero->ToggleBackpack();
            }
            else S->ChangeValid &= Workshop->Destroy(); // 显式故障注入，非完整玩家拆除流程。
            S->Changed=true;S->ChangeValid &= Pet->CarriedFood==Cargo;
            UE_LOG(LogTemp,Display,TEXT("CF_WORKSHOP_FOOD_CHANGED %s cancel=%d removed=%d cargo=%d distant=1"),S->ChangeValid?TEXT("PASS"):TEXT("FAIL"),Cancel,Removed,Cargo);
        }
        const int32 Stock=IsValid(Workshop)?Workshop->CraftInventory->Count(EFrontierItem::Food):0;
        const bool Fallback=Blocked || Reserve || Cancel || Removed;
        const bool Intact=Demand && S->ChangeValid && Continuous && Pet->GetActorEnableCollision() && Pet->WorkSite==Post && Pet->Activity==ECreatureActivity::Work && Pet->CarriedSeeds==0 && Pet->CarriedFood>=0 && Pet->CarriedFood<=3 && Stock<=1 && Farm->StoredFood+Trough->StoredFood+Pet->CarriedFood+Stock==Total && (!Fallback || Stock==0);
        const bool Done=(!(Cancel || Removed) || S->Changed) && S->Picked && Farm->StoredFood==0 && Pet->CarriedFood==0 && Stock==(Fallback?0:1) && Trough->StoredFood==Total-Stock && S->Travel>400 && Pet->Experience==1;
        if(!Intact || Done || Now-S->Start>35)
        {
            const bool Pass=Intact && Done;
            UE_LOG(LogTemp,Display,TEXT("CF_WORKSHOP_FOOD_ROUTE %s invariant=%d picked=%d stock=%d trough=%d cargo=%d travel=%.2f xp=%d elapsed=%.2f"),Pass?TEXT("PASS"):TEXT("FAIL"),Intact,S->Picked,Stock,Trough->StoredFood,Pet->CarriedFood,S->Travel,Pet->Experience,Now-S->Start);
            FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
        }
        return true;
    }));
}
