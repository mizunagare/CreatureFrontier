#include "CreatureCharacter.h"
#include "CFPlayerCharacter.h"
#include "WorkStation.h"
#include "EngineUtils.h"

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
    { FeedingSite.Reset(); AutoFeedCooldown = 5; return false; }
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
    { StopNavigation(); FeedingSite.Reset(); return true; }
    FVector Direction = GetActorLocation()-Site->GetActorLocation(); Direction.Z = 0;
    if (!Direction.Normalize()) Direction = FVector(1,0,0);
    // Leave room for path acceptance and the vertical offset to the bench's root.
    FVector Approach = Site->GetActorLocation()+Direction*150; Approach.Z = GetActorLocation().Z;
    NavigateTo(Approach,35);
    return true;
}
