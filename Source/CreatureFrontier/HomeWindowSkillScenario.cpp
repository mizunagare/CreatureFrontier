#include "HomeDoorScenario.h"
#include "HomeBuildingComponent.h"
#include "HomeModule.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "InventoryComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "WildEncounter.h"
#include "EngineUtils.h"
#include "TimerManager.h"

void RunHomeWindowSkillScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    for(TActorIterator<AWildEncounter> It(World);It;++It)It->SetActorTickEnabled(false);
    for(TActorIterator<ACreatureCharacter> It(World);It;++It){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();}
    Hero->SetActorLocation(FVector(-450,-400,40));Hero->Inventory->Restore({{EFrontierItem::Material,100}});
    Hero->HomeBuilding->Selected=EHomeModuleKind::Floor;
    auto* Floor=Hero->HomeBuilding->BuildAt(FTransform(FVector(-400,-800,-40)));
    Hero->HomeBuilding->Selected=EHomeModuleKind::Window;
    auto* Window=Floor?Hero->HomeBuilding->BuildAt(FTransform(FVector(-400,-1000,-30)),Floor->PersistentId):nullptr;
    auto* Definition=LoadObject<UCreatureDefinition>(nullptr,TEXT("/Game/Creatures/DA_Cinderkit.DA_Cinderkit"));
    if(!Window || !Definition){UE_LOG(LogTemp,Error,TEXT("CF_WINDOW_SKILL FAIL fixture"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    auto Spawn=[World,Hero](FVector Location,UCreatureDefinition* Def,bool Owned)
    {
        auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),FTransform(Location),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Pet)return Pet;Pet->Definition=Def;Pet->FinishSpawning(FTransform(Location));Pet->GetCharacterMovement()->DisableMovement();
        if(Owned){Pet->Tame(Hero->GetController());Pet->SetAvoidCombat(true);}else{Pet->Health=1000;Pet->SetActorTickEnabled(false);}
        return Pet;
    };
    auto* Caster=Spawn(FVector(-400,-1140,40),Definition,true);
    auto* Target=Spawn(FVector(-400,-880,60),nullptr,false);
    auto* Open=Spawn(FVector(-300,-800,140),nullptr,false);
    auto* Covered=Spawn(FVector(-590,-880,140),nullptr,false);
    if(!Caster || !Target || !Open || !Covered || !Caster->RequestSpecial(Target))
    {UE_LOG(LogTemp,Error,TEXT("CF_WINDOW_SKILL FAIL request"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    const float Damage=Caster->GetAttackDamage()*Definition->SpecialDamageScale;
    // 冻结靶位/移动组件隔离视线边界，但施法伙伴始终由正常世界Tick推进蓄力与结算。
    // 高处位置与阶段间冷却重置仅是独立测试夹具，不代表自然作战或冷却平衡。
    const auto Timer=MakeShared<FTimerHandle>();
    World->GetTimerManager().SetTimer(*Timer,[World,Hero,Caster,Target,Open,Covered,Damage,Timer,Phase=0,At=World->GetTimeSeconds(),Charged=false]() mutable
    {
        auto Finish=[&](bool Pass)
        {
            UE_LOG(LogTemp,Display,TEXT("CF_WINDOW_SKILL %s phase=%d charged=%d main=%.2f open=%.2f covered=%.2f stamina=%.2f"),Pass?TEXT("PASS"):TEXT("FAIL"),Phase,Charged,Target->Health,Open->Health,Covered->Health,Caster->Stamina);
            FTimerHandle Handle=*Timer;World->GetTimerManager().ClearTimer(Handle);FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
        };
        const float Elapsed=World->GetTimeSeconds()-At;
        if(Phase==0 && Elapsed>=2)
        {
            const bool Blocked=Target->Health==1000 && Open->Health==1000 && Covered->Health==1000 && Caster->SpecialWindupRemaining==0 && Caster->SpecialCooldownRemaining==0 && Caster->Stamina==100;
            UE_LOG(LogTemp,Display,TEXT("CF_WINDOW_SKILL_BLOCKED %s"),Blocked?TEXT("PASS"):TEXT("FAIL"));
            if(!Blocked){Finish(false);return;}
            Caster->ClearCombatOrders();Caster->SetActorLocation(FVector(-400,-1140,140));Target->SetActorLocation(FVector(-400,-880,140));
            if(!Caster->RequestSpecial(Target)){Finish(false);return;}At=World->GetTimeSeconds();Phase=1;return;
        }
        if(Phase==1)
        {
            Charged |= Caster->SpecialWindupRemaining>0 && Caster->Stamina==80;
            if(Elapsed<1.2)return;
            const bool Hit=Charged && FMath::IsNearlyEqual(Target->Health,1000-Damage,.01) && FMath::IsNearlyEqual(Open->Health,1000-Damage*.5f,.01) && Covered->Health==1000;
            UE_LOG(LogTemp,Display,TEXT("CF_WINDOW_SKILL_SPLASH %s damage=%.2f"),Hit?TEXT("PASS"):TEXT("FAIL"),Damage);
            if(!Hit){Finish(false);return;}
            Caster->ClearCombatOrders();Caster->SpecialCooldownRemaining=0;Caster->Stamina=100;
            if(!Caster->RequestSpecial(Target)){Finish(false);return;}At=World->GetTimeSeconds();Phase=2;return;
        }
        if(Phase==2)
        {
            if(Caster->SpecialWindupRemaining<=0){if(Elapsed>2)Finish(false);return;}
            Target->SetActorLocation(FVector(-650,-880,140));At=World->GetTimeSeconds();Phase=3;return;
        }
        if(Phase==3 && Elapsed>.9)
        {
            const bool Safe=Caster->SpecialWindupRemaining==0 && Caster->Stamina==80 && FMath::IsNearlyEqual(Target->Health,1000-Damage,.01) && FMath::IsNearlyEqual(Open->Health,1000-Damage*.5f,.01) && Covered->Health==1000 && Hero->Inventory->Count(EFrontierItem::Material)==82;
            Caster->ClearCombatOrders();Finish(Safe);return;
        }
    },.05f,true);
}
