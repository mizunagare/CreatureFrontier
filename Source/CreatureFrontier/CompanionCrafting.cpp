#include "CreatureCharacter.h"
#include "WorkStation.h"
#include "CFPlayerCharacter.h"

bool ACreatureCharacter::TickCrafting(float DeltaSeconds,ACFPlayerCharacter* Hero)
{
    if(!IsValid(WorkSite) || WorkSite->RequiredSpecialty!=EWorkSpecialty::Crafting || WorkSite->CraftRecipeId.IsNone())return false;
    bGatheringActive=false;
    if(Stamina<=0){bResumeWorkAfterRest=true;SetActivity(ECreatureActivity::Rest);StopNavigation();return true;}
    if(!CanReachWorkSite(WorkSite)){ApproachWorkSite(WorkSite);return true;}
    StopNavigation();bGatheringActive=WorkSite->WorkCraft(this,Hero,DeltaSeconds*GetWorkEfficiency(WorkSite)*(Nutrition<20?.5f:1.f));
    if(bGatheringActive)Stamina=FMath::Max(0.f,Stamina-DeltaSeconds*1.5f);
    return true;
}
