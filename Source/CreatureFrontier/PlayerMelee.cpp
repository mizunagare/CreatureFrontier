#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "PlayerEquipmentComponent.h"
#include "BaseBuildingComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

float ACFPlayerCharacter::GetPlayerMeleeRange() const { return Equipment->GetState().bSpearEquipped?320.f:180.f; }

void ACFPlayerCharacter::CancelMeleeAttack()
{
    GetWorldTimerManager().ClearTimer(MeleeTimer); bMeleePending=false;
    if(Equipment) Equipment->StopAttackVisual();
}

void ACFPlayerCharacter::Attack()
{
    if(!HasAuthority() || !GetController()) return;
    if(Building->IsActive()) { Building->Confirm(); return; }
    if(Health<=0 || bMeleePending || GetWorld()->GetTimeSeconds()<NextAttackTime) return;
    RespawnProtectionUntil=0;
    bPendingSpear=Equipment->GetState().bSpearEquipped;
    PendingMeleeDamage=GetPlayerAttackDamage(); PendingMeleeRange=GetPlayerMeleeRange(); bMeleePending=true;
    NextAttackTime=GetWorld()->GetTimeSeconds()+(bPendingSpear?.55:.4);
    Equipment->PlayAttack();
    if(bPendingSpear)
    {
        Feedback=TEXT("Spear windup - keep the target within 3.2m");
        GetWorldTimerManager().SetTimer(MeleeTimer,this,&ACFPlayerCharacter::ResolveMeleeStrike,.12f,false);
    }
    else ResolveMeleeStrike();
}

void ACFPlayerCharacter::ResolveMeleeStrike()
{
    if(!bMeleePending) return;
    bMeleePending=false; GetWorldTimerManager().ClearTimer(MeleeTimer);
    if(!HasAuthority() || !GetController() || Health<=0 || Building->IsActive() || Equipment->GetState().bSpearEquipped!=bPendingSpear) return;
    const FVector Start=FirstPersonCamera->GetComponentLocation(), End=Start+FirstPersonCamera->GetForwardVector()*PendingMeleeRange;
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(PlayerMelee),false,this);
    if(!GetWorld()->SweepSingleByChannel(Hit,Start,End,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(12),Params))
    { Feedback=TEXT("Melee missed - move closer and aim at the creature"); return; }
    auto* Target=Cast<ACreatureCharacter>(Hit.GetActor());
    if(!Target) { Feedback=TEXT("Melee blocked by cover"); return; }
    if(Target->Disposition==ECreatureDisposition::Tamed || Target->Health<=0)
    { Feedback=TEXT("Melee stopped - companions and downed creatures are not targets"); return; }
    FHitResult Sight;
    if(GetWorld()->LineTraceSingleByChannel(Sight,Start,Hit.ImpactPoint,ECC_Visibility,Params) && Sight.GetActor()!=Target)
    { Feedback=TEXT("Melee blocked by cover"); return; }
    const float Applied=UGameplayStatics::ApplyDamage(Target,PendingMeleeDamage,GetController(),this,UDamageType::StaticClass());
    Feedback=Applied>0?TEXT("Melee hit! RMB captures; weaken the target without defeating it"):TEXT("Creature retreating - wait for it to return home");
}
