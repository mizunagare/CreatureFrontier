#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "WorkStation.h"
#include "InventoryComponent.h"
#include "Kismet/GameplayStatics.h"

void RunCompanionCraftRules(UWorld* World,ACFPlayerCharacter* Hero)
{
    // 独立规则夹具使用明确库存，不作为真实采集、移动或时间进度证据。
    Hero->SetActorLocation(FVector(1200,-1200,40));Hero->Health=100;
    auto* Site=World->SpawnActor<AWorkStation>(FVector(1400,-1000,0),FRotator::ZeroRotator);Site->bPlayerBuilt=true;Site->RequiredSpecialty=EWorkSpecialty::Crafting;Site->CraftRecipeId=TEXT("Medicine");
    auto MakeWorker=[&](FVector Position){auto* P=World->SpawnActor<ACreatureCharacter>(Position,FRotator::ZeroRotator);P->Disposition=ECreatureDisposition::Tamed;P->Tamer=Hero->GetController();P->WorkSite=Site;P->Activity=ECreatureActivity::Work;return P;};
    auto* Pet=MakeWorker(FVector(1250,-1000,40));auto* Peer=MakeWorker(FVector(1400,-1180,40));
    Hero->Inventory->Restore({{EFrontierItem::Material,4}});
    const bool Missing=!Site->WorkCraft(Pet,Hero,8) && Site->CraftWork==0 && Hero->Inventory->Count(EFrontierItem::Material)==4 && Pet->Experience==0;
    Hero->Inventory->Restore({{EFrontierItem::Material,5},{EFrontierItem::Medicine,999}});
    const bool Full=!Site->WorkCraft(Pet,Hero,8) && Site->CraftWork==0 && Hero->Inventory->Count(EFrontierItem::Medicine)==999 && Hero->Inventory->Count(EFrontierItem::Material)==5;
    Hero->Inventory->Restore({{EFrontierItem::Material,5}});
    const bool Partial=Site->WorkCraft(Pet,Hero,2) && Site->CraftWork==2 && Hero->Inventory->Count(EFrontierItem::Material)==5 && Hero->Inventory->Count(EFrontierItem::Medicine)==0;
    UGameplayStatics::SetGamePaused(World,true);const bool Paused=!Site->WorkCraft(Pet,Hero,1) && Site->CraftWork==2;UGameplayStatics::SetGamePaused(World,false);
    Pet->SetActivity(ECreatureActivity::Rest);const bool Rest=!Site->WorkCraft(Pet,Hero,1) && Site->CraftWork==2;Pet->SetActivity(ECreatureActivity::Work);
    const bool Exclusive=!Site->WorkCraft(Peer,Hero,2) && Site->CraftWork==2 && Peer->Experience==0;
    const bool Finished=Site->WorkCraft(Pet,Hero,6) && Site->CraftWork==0 && Site->CraftRecipeId.IsNone() && Hero->Inventory->Count(EFrontierItem::Material)==0 && Hero->Inventory->Count(EFrontierItem::Medicine)==1 && Pet->Experience==1 && Pet->Activity==ECreatureActivity::Rest;
    const bool Once=!Site->WorkCraft(Pet,Hero,8) && Hero->Inventory->Count(EFrontierItem::Medicine)==1 && Pet->Experience==1;
    const bool Pass=Missing && Full && Partial && Paused && Rest && Exclusive && Finished && Once;
    UE_LOG(LogTemp,Display,TEXT("CF_COMPANION_CRAFT_RULES %s missing=%d full=%d partial=%d paused=%d rest=%d exclusive=%d finished=%d once=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Missing,Full,Partial,Paused,Rest,Exclusive,Finished,Once);FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}
