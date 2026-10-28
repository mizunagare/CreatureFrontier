#include "BefriendingScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "InventoryComponent.h"
#include "InputRoutingQA.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "HAL/PlatformMisc.h"

namespace
{
bool DefenseSnapshot(UWorld* World,ACFPlayerCharacter* Hero)
{
    int32 Count=0;TSet<FGuid> Ids;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController())
    {
        if(!It->PersistentId.IsValid() || Ids.Contains(It->PersistentId) || It->Health!=It->GetMaxHealth() || It->Activity!=ECreatureActivity::Follow)return false;
        Ids.Add(It->PersistentId);++Count;
    }
    return Count==2 && Hero->Inventory->Count(EFrontierItem::Food)==7 && Hero->Inventory->Count(EFrontierItem::CaptureDevice)==5;
}
}
void RunCompanionDefenseScenario(UWorld* World,ACFPlayerCharacter* Hero,int Mode)
{
    if(Mode==2)
    {const bool Pass=Hero->LoadProgress(TEXT("DefenseCross")) && DefenseSnapshot(World,Hero);UE_LOG(LogTemp,Display,TEXT("CF_DEFENSE_READ %s"),Pass?TEXT("PASS"):TEXT("FAIL"));FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return;}
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)It->Destroy();
    auto* Leaf=LoadObject<UCreatureDefinition>(nullptr,TEXT("/Game/Creatures/DA_Mosskip.DA_Mosskip"));
    auto* Stone=LoadObject<UCreatureDefinition>(nullptr,TEXT("/Game/Creatures/DA_Craglet.DA_Craglet"));
    auto* Ember=LoadObject<UCreatureDefinition>(nullptr,TEXT("/Game/Creatures/DA_Cinderkit.DA_Cinderkit"));
    if(!Leaf || !Stone || !Ember){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    Hero->SetActorLocation(FVector(-450,-1100,40));Hero->GetCharacterMovement()->DisableMovement();
    Hero->Inventory->Restore({{EFrontierItem::Food,10},{EFrontierItem::CaptureDevice,5}});
    auto Spawn=[World](UCreatureDefinition* Definition,const FVector& Location)
    {const FTransform At(Location);auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),At);if(Pet){Pet->Definition=Definition;Pet->FinishSpawning(At);}return Pet;};
    auto* Wild=Spawn(Leaf,Hero->GetActorLocation()+FVector(180,0,0));
    auto* Ally=Spawn(Stone,Hero->GetActorLocation()+FVector(-180,0,0));
    if(!Wild || !Ally){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    Ally->Tame(Hero->GetController());const FGuid AllyId=Ally->PersistentId,WildId=Wild->PersistentId;
    const float HeroHP=Hero->Health;
    // 先让真实角色Tick与同行运行，不能靠冻结伙伴掩盖误攻。
    FTimerHandle Check;World->GetTimerManager().SetTimer(Check,[World,Hero,Wild,Ally,AllyId,WildId,HeroHP,Spawn,Ember,Mode]()
    {
        const bool Calm=Wild->Health==Wild->GetMaxHealth() && Ally->Health==Ally->GetMaxHealth() && Hero->Health==HeroHP && Ally->Activity==ECreatureActivity::Follow && Wild->Disposition==ECreatureDisposition::Wild;
        if(!Calm){UE_LOG(LogTemp,Error,TEXT("CF_DEFENSE FAIL calm=0 wildHP=%.1f allyActivity=%d"),Wild->Health,int32(Ally->Activity));FPlatformMisc::RequestExitWithStatus(false,1);return;}
        const auto View=(Wild->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
        const bool Joined=CFInputQA::Press(Hero,EKeys::J) && DefenseSnapshot(World,Hero) && Wild->PersistentId==WildId;
        // 后半段固定位置隔离选敌与实际伤害，不作为连续战斗/导航证据。
        for(auto* Pet:{Wild,Ally}){Pet->SetActorTickEnabled(false);Pet->GetCharacterMovement()->DisableMovement();}
        auto* Threat=Spawn(Ember,Ally->GetActorLocation()+FVector(0,-125,0));
        if(!Threat){FPlatformMisc::RequestExitWithStatus(false,1);return;}
        Threat->SetActorTickEnabled(false);Threat->GetCharacterMovement()->DisableMovement();const float HP=Threat->Health;
        Ally->Tick(2.f);const bool Neutral=Ally->Activity==ECreatureActivity::Follow && Threat->Health==HP;
        const bool Provoked=UGameplayStatics::ApplyDamage(Threat,1,Hero->GetController(),Hero,nullptr)>0;Threat->Tick(.1f);const float HurtHP=Threat->Health;
        Ally->Tick(2.f);const bool Defended=Provoked && Threat->Disposition==ECreatureDisposition::Hostile && Ally->Activity==ECreatureActivity::Combat && Threat->Health<HurtHP;
        Threat->Destroy();Ally->Tick(2.f);
        auto* Ordered=Spawn(Ember,Ally->GetActorLocation()+FVector(0,-125,0));
        if(!Ordered){FPlatformMisc::RequestExitWithStatus(false,1);return;}
        Ordered->SetActorTickEnabled(false);Ordered->GetCharacterMovement()->DisableMovement();const float OrderedHP=Ordered->Health;
        const bool Command=Ally->OrderAttack(Ordered);Ally->Tick(2.f);
        const bool Explicit=Command && Ordered->Health<OrderedHP;
        Ordered->Destroy();Ally->Tick(2.f);Wild->Tick(.1f);
        const bool Saved=Hero->SaveProgress(TEXT("DefenseCross")) && Hero->LoadProgress(TEXT("DefenseCross")) && DefenseSnapshot(World,Hero);
        bool SameAlly=false,SameFriend=false;for(TActorIterator<ACreatureCharacter> It(World);It;++It){SameAlly|=It->PersistentId==AllyId && It->Tamer==Hero->GetController();SameFriend|=It->PersistentId==WildId && It->Tamer==Hero->GetController();}
        const bool Pass=Calm && Joined && Neutral && Defended && Explicit && Saved && SameAlly && SameFriend;
        UE_LOG(LogTemp,Display,TEXT("CF_DEFENSE%s %s calm=%d joined=%d neutral=%d defended=%d explicit=%d saved=%d identity=%d"),Mode==1?TEXT("_WRITE"):TEXT(""),Pass?TEXT("PASS"):TEXT("FAIL"),Calm,Joined,Neutral,Defended,Explicit,Saved,SameAlly && SameFriend);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    },1.5f,false);
}
