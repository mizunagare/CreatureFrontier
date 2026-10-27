#include "CreatureCharacter.h"
#include "NavigationSystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

bool ACreatureCharacter::TickHabitatReturn()
{
    if (!bHasHabitat) return false;
    const float Distance = FVector::Dist2D(GetActorLocation(),HabitatHome);
    if (Activity != ECreatureActivity::ReturnHome && Distance <= LeashRadius) return false;
    AlphaWindup = 0;
    ClearCombatOrders();
    RetaliationTarget.Reset(); RetaliationRemaining = 0;
    SlowRemaining = 0; GuardRemaining = 0;
    Disposition = ECreatureDisposition::Wild;
    SetActivity(ECreatureActivity::ReturnHome);
    if (Distance > 100) NavigateTo(HabitatHome,55);
    else
    {
        StopNavigation();
        Health = GetMaxHealth();
        SetActivity(ECreatureActivity::Idle);
        RoamPause = 3;
        bHasRoamTarget = false;
    }
    return true;
}

void ACreatureCharacter::TickRoaming(float DeltaSeconds)
{
    Disposition = ECreatureDisposition::Wild;
    if (!bHasHabitat) { StopNavigation(); SetActivity(ECreatureActivity::Idle); return; }
    if (RoamPause > 0)
    {
        RoamPause = FMath::Max(0.0f,RoamPause-DeltaSeconds);
        StopNavigation(); SetActivity(ECreatureActivity::Idle); return;
    }
    if (Activity != ECreatureActivity::Wander || !bHasRoamTarget)
    {
        auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
        FNavLocation Point;
        bool Found = false;
        for (int32 Attempt=0; Nav && Attempt<5; ++Attempt)
            if (Nav->GetRandomReachablePointInRadius(HabitatHome,RoamRadius,Point) && FVector::Dist2D(GetActorLocation(),Point.Location) > 110)
            { Found = true; break; }
        if (!Found)
        {
            if(FParse::Param(FCommandLine::Get(),TEXT("CFRoamTrace")))
                UE_LOG(LogTemp,Display,TEXT("CF_ROAM_SELECT failed time=%.3f location=%s"),GetWorld()->GetTimeSeconds(),*GetActorLocation().ToString());
            StopNavigation(); SetActivity(ECreatureActivity::Idle); RoamPause = 2; return;
        }
        RoamTarget = Point.Location; bHasRoamTarget = true;
        if(FParse::Param(FCommandLine::Get(),TEXT("CFRoamTrace")))
            UE_LOG(LogTemp,Display,TEXT("CF_ROAM_SELECT time=%.3f distance=%.3f location=%s target=%s"),GetWorld()->GetTimeSeconds(),FVector::Dist2D(GetActorLocation(),RoamTarget),*GetActorLocation().ToString(),*RoamTarget.ToString());
        SetActivity(ECreatureActivity::Wander);
    }
    if (FVector::Dist2D(GetActorLocation(),RoamTarget) <= 85)
    {
        StopNavigation(); SetActivity(ECreatureActivity::Idle);
        RoamPause = FMath::FRandRange(2.0f,5.0f); bHasRoamTarget = false;
        if(FParse::Param(FCommandLine::Get(),TEXT("CFRoamTrace")))
            UE_LOG(LogTemp,Display,TEXT("CF_ROAM_ARRIVE time=%.3f distance=%.3f homeDistance=%.3f pause=%.3f"),GetWorld()->GetTimeSeconds(),FVector::Dist2D(GetActorLocation(),RoamTarget),FVector::Dist2D(GetActorLocation(),HabitatHome),RoamPause);
    }
    else NavigateTo(RoamTarget,55);
}
