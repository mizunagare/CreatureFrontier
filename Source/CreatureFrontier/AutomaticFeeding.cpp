#include "CreatureCharacter.h"
#include "CFPlayerCharacter.h"
#include "WorkStation.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

bool ACreatureCharacter::TickAutoFeeding()
{
    if (Nutrition > 40 || AutoFeedCooldown > 0 || Activity == ECreatureActivity::Combat || FocusTarget)
    { FeedingSite.Reset(); return false; }
    for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
        if (It->Disposition != ECreatureDisposition::Tamed && It->Health > 0 && It->Activity != ECreatureActivity::ReturnHome && GetDistanceTo(*It) < 600)
        { FeedingSite.Reset(); return false; }
    if(CarriedFood>0)
    {
        --CarriedFood; Nutrition=FMath::Min(100.f,Nutrition+35); Stamina=FMath::Min(100.f,Stamina+15);
        AutoFeedCooldown=3; FeedingSite.Reset(); StopNavigation(); return true;
    }
    auto* Site = FeedingSite.Get();
    if (Site && (Site->StoredFood <= 0 || GetWorld()->GetTimeSeconds() > FoodSearchExpires))
    {
        if(FParse::Param(FCommandLine::Get(),TEXT("CFFeedingApproachTrace")))UE_LOG(LogTemp,Display,TEXT("CF_FEED_ABORT id=%s pos=%s nutrition=%.3f stock=%d expired=%d"),*PersistentId.ToString(),*GetActorLocation().ToString(),Nutrition,Site->StoredFood,GetWorld()->GetTimeSeconds()>FoodSearchExpires);
        FeedingSite.Reset(); AutoFeedCooldown = 5; return false;
    }
    if (!Site)
    {
        float Best = 800;
        for (TActorIterator<AWorkStation> It(GetWorld()); It; ++It)
            if (It->RequiredSpecialty == EWorkSpecialty::Feeding && It->StoredFood > 0 && GetDistanceTo(*It) < Best)
            { Best = GetDistanceTo(*It); Site = *It; }
        if (!Site) { AutoFeedCooldown = 2; return false; }
        FeedingSite = Site; FoodSearchExpires = GetWorld()->GetTimeSeconds()+8;
        WorkProgress = 0;
    }
    if (GetDistanceTo(Site) <= 220 && Site->ServeFood(this))
    {
        if(FParse::Param(FCommandLine::Get(),TEXT("CFFeedingApproachTrace")))UE_LOG(LogTemp,Display,TEXT("CF_FEED_SERVED id=%s pos=%s nutrition=%.3f stock=%d activity=%d"),*PersistentId.ToString(),*GetActorLocation().ToString(),Nutrition,Site->StoredFood,static_cast<int32>(Activity));
        StopNavigation(); FeedingSite.Reset(); return true;
    }
    FVector Direction = GetActorLocation()-Site->GetActorLocation(); Direction.Z = 0;
    if (!Direction.Normalize()) Direction = FVector(1,0,0);
    // 为路径抵达判定及工作台根节点的垂直偏移留出余量。
    FVector Approach = Site->GetActorLocation()+Direction*150; Approach.Z = GetActorLocation().Z;
    if(GetWorld()->GetTimeSeconds()>=NextNavigationUpdate && FParse::Param(FCommandLine::Get(),TEXT("CFFeedingApproachTrace")))
    {
        auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());FNavLocation Projected;
        const bool Found=Nav && Nav->ProjectPointToNavigation(Approach,Projected,FVector(80,80,120));
        const auto* Path=Found?UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),GetActorLocation(),Projected.Location,this):nullptr;
        UE_LOG(LogTemp,Display,TEXT("CF_FEED_APPROACH id=%s from=%s goal=%s projected=%s found=%d valid=%d partial=%d"),*PersistentId.ToString(),*GetActorLocation().ToString(),*Approach.ToString(),Found?*Projected.Location.ToString():TEXT("none"),Found,Path && Path->IsValid(),Path && Path->IsPartial());
    }
    NavigateTo(Approach,35);
    return true;
}
