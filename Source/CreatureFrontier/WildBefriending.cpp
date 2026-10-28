#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "InventoryComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

bool ACFPlayerCharacter::BefriendWildCreature(ACreatureCharacter* Creature)
{
    if(!HasAuthority() || !GetController() || Health<=0 || UGameplayStatics::IsGamePaused(this))return false;
    if(!IsValid(Creature) || Creature->IsActorBeingDestroyed() || Creature->Tamer || !Creature->Definition || Creature->bAlpha ||
        (Creature->Disposition!=ECreatureDisposition::Wild && Creature->Disposition!=ECreatureDisposition::Friendly) ||
        (Creature->Activity!=ECreatureActivity::Idle && Creature->Activity!=ECreatureActivity::Wander) ||
        !FMath::IsNearlyEqual(Creature->Health,Creature->GetMaxHealth(),.01f))
    {Feedback=TEXT("只能向健康、平静且未归属的温顺野生生物分享食物；首领不适用");return false;}
    const int32 Cost=Creature->Definition->PeacefulFoodCost;
    if(Cost<1 || Cost>10){Feedback=TEXT("这个物种暂不接受食物结交");return false;}
    if(GetDistanceTo(Creature)>250){Feedback=TEXT("请轻轻靠近，在2.5米内分享食物");return false;}
    if(GetWorld()->GetTimeSeconds()<NextFeedTime){Feedback=TEXT("请等待喂食冷却（3秒）");return false;}
    FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(PeacefulBefriending),false,this);
    if(GetWorld()->LineTraceSingleByChannel(Hit,GetPawnViewLocation(),Creature->GetActorLocation(),ECC_Visibility,Query) && Hit.GetActor()!=Creature)
    {Feedback=TEXT("视线被阻挡，请走到生物面前");return false;}
    for(TActorIterator<ACreatureCharacter> It(GetWorld());It;++It)
    {
        if(*It==Creature || It->Health<=0 || It->IsActorBeingDestroyed())continue;
        if((It->Disposition==ECreatureDisposition::Hostile || It->Activity==ECreatureActivity::Combat) &&
            (GetDistanceTo(*It)<600 || Creature->GetDistanceTo(*It)<600))
        {Feedback=TEXT("附近仍有敌意或战斗，请先到安全的地方");return false;}
    }
    if(!Inventory->Consume(EFrontierItem::Food,Cost))
    {Feedback=FString::Printf(TEXT("和平结交需要%d份食物，未消耗任何物品"),Cost);return false;}
    // 所有拒绝路径均在扣除前。沿用既有驯服入口与稳定身份，不生成替身或奖励。
    Creature->Tame(GetController());Creature->AddBond(3);
    RecordJournal(Creature->Definition,true);
    NextFeedTime=GetWorld()->GetTimeSeconds()+3;
    Feedback=FString::Printf(TEXT("你与%s分享了%d份食物，它愿意与你同行！F 跟随 | E 指派工作"),*Creature->GetCreatureName(),Cost);
    return true;
}
