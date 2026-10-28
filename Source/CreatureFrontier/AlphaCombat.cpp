#include "CreatureCharacter.h"
#include "CFPlayerCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"

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
                if (ValidVictim && Delta.Size2D() <= 380 && FMath::Abs(Delta.Z) <= 65 && HasClearSight(*It))
                    UGameplayStatics::ApplyDamage(*It, GetAttackDamage() * 1.5f, GetController(), this, UDamageType::StaticClass());
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
