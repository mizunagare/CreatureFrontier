#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "InventoryComponent.h"

void ACFPlayerCharacter::CraftFood()
{
    if (!HasAuthority() || Health <= 0) return;
    if (!IsNearWorkStation()) { Feedback = TEXT("Food crafting needs a nearby work station (8m)"); return; }
    Feedback = Inventory->Craft(EFrontierItem::Food) ? TEXT("Crafted food: -2 materials") : TEXT("Need 2 materials or food stack is full");
}

bool ACFPlayerCharacter::FeedCompanion(ACreatureCharacter* Pet)
{
    if (!HasAuthority() || Health <= 0 || !IsValid(Pet) || Pet->Tamer != GetController() || Pet->Health <= 0 || GetDistanceTo(Pet) > 600)
    { Feedback = TEXT("Feed a living companion within 6m; use medicine to revive"); return false; }
    if (GetWorld()->GetTimeSeconds() < NextFeedTime) { Feedback = TEXT("Wait before feeding again (3s)"); return false; }
    if (Pet->Nutrition > 80) { Feedback = TEXT("Companion is not hungry (satiety must be 80 or below)"); return false; }
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(FeedingSight),false,this);
    if (GetWorld()->LineTraceSingleByChannel(Hit,GetPawnViewLocation(),Pet->GetActorLocation(),ECC_Visibility,Params) && Hit.GetActor() != Pet)
    { Feedback = TEXT("Companion is blocked by cover"); return false; }
    if (!Inventory->Consume(EFrontierItem::Food,1)) { Feedback = TEXT("No food - farm or craft with 3 at base"); return false; }
    Pet->Nutrition = FMath::Min(100.0f,Pet->Nutrition+35);
    Pet->Stamina = FMath::Min(100.0f,Pet->Stamina+15);
    NextFeedTime = GetWorld()->GetTimeSeconds()+3;
    Feedback = TEXT("Fed companion: +35 satiety, +15 stamina. E: resume work | F: follow");
    return true;
}

void ACFPlayerCharacter::FeedAimedCompanion() { FeedCompanion(TraceCreature()); }
