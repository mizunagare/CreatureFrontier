#include "HollowScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"

void RunWildSightScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    // 固定位置/手动Tick用于隔离视线规则，不作为洞窟实际移动的证据。
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)It->Destroy();
    auto* Definition=LoadObject<UCreatureDefinition>(nullptr,TEXT("/Game/Creatures/DA_Cinderkit.DA_Cinderkit"));
    Hero->GetCharacterMovement()->DisableMovement();Hero->SetActorLocation(FVector(300,-3300,40));
    const FTransform At(FVector(300,-3850,40));
    auto* Enemy=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),At);
    if(!Enemy || !Definition){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    Enemy->Definition=Definition;Enemy->FinishSpawning(At);Enemy->SetActorTickEnabled(false);Enemy->GetCharacterMovement()->DisableMovement();
    FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(WildSightFixture),false,Enemy);
    const bool Wall=World->LineTraceSingleByChannel(Hit,Enemy->GetActorLocation(),Hero->GetActorLocation(),ECC_Visibility,Query) && Hit.GetActor()!=Hero;
    Enemy->Tick(.1f);
    const bool Covered=Wall && Enemy->Activity!=ECreatureActivity::Combat;
    Hero->SetActorLocation(FVector(300,-4100,40));Enemy->Tick(.1f);
    const bool Seen=Enemy->Activity==ECreatureActivity::Combat;
    Hero->SetActorLocation(FVector(300,-3300,40));const float Health=Hero->Health;Enemy->Tick(.1f);
    const bool Remember=Enemy->Activity==ECreatureActivity::Combat && Hero->Health==Health;
    Enemy->Tick(3.1f);const bool Expired=Enemy->Activity!=ECreatureActivity::Combat;
    // 遮挡的玩家不能抢走可见伙伴的目标资格。
    const FTransform PetAt(FVector(180,-3850,40));
    auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),PetAt);
    if(!Pet){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    Pet->Definition=Definition;Pet->FinishSpawning(PetAt);Pet->Tame(Hero->GetController());Pet->SetActorTickEnabled(false);Pet->GetCharacterMovement()->DisableMovement();
    const float PetHealth=Pet->Health;Enemy->Tick(.1f);
    const bool VisiblePet=Enemy->Activity==ECreatureActivity::Combat && Pet->Health<PetHealth && Hero->Health==Health;
    Pet->Destroy();
    Hero->Health=0;Enemy->HandlePlayerDowned(Hero);Hero->Health=100;Enemy->Tick(.1f);
    const bool Reset=Enemy->Activity!=ECreatureActivity::Combat;
    UGameplayStatics::ApplyDamage(Enemy,1,Hero->GetController(),Hero,nullptr);Enemy->Tick(.1f);
    const bool Retaliation=Enemy->Activity==ECreatureActivity::Combat;
    const FTransform AllyAt(FVector(400,-3300,40));
    auto* Ally=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),AllyAt);
    if(!Ally){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    Ally->Definition=Definition;Ally->FinishSpawning(AllyAt);Ally->Tame(Hero->GetController());Ally->SetActorTickEnabled(false);Ally->GetCharacterMovement()->DisableMovement();Ally->Tick(.1f);
    const bool AllyCover=Ally->Activity==ECreatureActivity::Follow && !Ally->FocusTarget;
    const bool Ordered=Ally->OrderAttack(Enemy);Ally->Tick(.1f);
    const bool Explicit=Ordered && Ally->Activity==ECreatureActivity::Combat && Ally->FocusTarget==Enemy;
    Enemy->Tame(Hero->GetController());Enemy->Tick(.1f);
    const bool Tamed=Enemy->Disposition==ECreatureDisposition::Tamed && Enemy->Activity==ECreatureActivity::Follow;
    const bool Pass=Covered && Seen && Remember && Expired && VisiblePet && Reset && Retaliation && AllyCover && Explicit && Tamed;
    UE_LOG(LogTemp,Display,TEXT("CF_WILD_SIGHT %s covered=%d seen=%d memory=%d expired=%d visiblePet=%d downedReset=%d retaliation=%d allyCover=%d explicit=%d tamed=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Covered,Seen,Remember,Expired,VisiblePet,Reset,Retaliation,AllyCover,Explicit,Tamed);
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}
