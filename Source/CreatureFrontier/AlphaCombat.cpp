#include "CreatureCharacter.h"
#include "CFPlayerCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

bool ACreatureCharacter::TickAlphaAttack(float DeltaSeconds, APawn* Target)
{
    if (!bAlpha || Disposition == ECreatureDisposition::Tamed) { AlphaWindup = 0; return false; }
    if (AlphaWindup > 0)
    {
        StopNavigation();
        AlphaWindup = FMath::Max(0.0f, AlphaWindup - DeltaSeconds);
        if (AlphaWindup <= 0)
        {
            BeginStrikePose();
            for (TActorIterator<APawn> It(GetWorld()); It; ++It)
            {
                auto* Pet = Cast<ACreatureCharacter>(*It);
                auto* Hero = Cast<ACFPlayerCharacter>(*It);
                const bool ValidVictim = (Hero && Hero->Health > 0) ||
                    (Pet && Pet->Health > 0 && Pet->Disposition == ECreatureDisposition::Tamed);
                const FVector Delta = It->GetActorLocation() - GetActorLocation();
                float Applied=0;
                if (ValidVictim && Delta.Size2D() <= 380 && FMath::Abs(Delta.Z) <= 65 && HasClearSight(*It))
                    Applied=UGameplayStatics::ApplyDamage(*It, GetAttackDamage() * 1.5f, GetController(), this, UDamageType::StaticClass());
                // 只在显式诊断时区分高度躲避与圈外／遮挡，不改变原伤害判定。
                if(Hero && FParse::Param(FCommandLine::Get(),TEXT("CFCombatTrace")))
                    UE_LOG(LogTemp,Display,TEXT("CF_ALPHA_PLAYER_TRACE boss=%s distance=%.2f height=%.2f inRange=%d above=%d sight=%d applied=%.2f"),*PersistentId.ToString(),Delta.Size2D(),Delta.Z,Delta.Size2D()<=380,FMath::Abs(Delta.Z)>65,HasClearSight(Hero),Applied);
            }
        }
        return true;
    }
    if (AlphaCooldown > 0 || !IsValid(Target) || GetDistanceTo(Target) > 500 || !HasClearSight(Target)) return false;
    StopNavigation();
    SetActivity(ECreatureActivity::Combat);
    Disposition = ECreatureDisposition::Hostile;
    AlphaWindup = 1.2f;
    AlphaCooldown = 10;
    AttackCooldown = FMath::Max(AttackCooldown, AttackInterval);
    if (auto* Hero = Cast<ACFPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
        Hero->Feedback = TEXT("首领：水晶震地！离开橙色圆圈或跳跃！");
    return true;
}
