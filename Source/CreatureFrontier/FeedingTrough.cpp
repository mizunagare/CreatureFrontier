#include "WorkStation.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "InventoryComponent.h"

bool AWorkStation::TransferFood(ACFPlayerCharacter* Hero, bool Deposit)
{
    if (!HasAuthority() || RequiredSpecialty != EWorkSpecialty::Feeding || !IsValid(Hero) || Hero->Health <= 0 || GetDistanceTo(Hero) > 600) return false;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(TroughTransfer),false,Hero);
    if (GetWorld()->LineTraceSingleByChannel(Hit,Hero->GetPawnViewLocation(),GetActorLocation(),ECC_Visibility,Params) && Hit.GetActor() != this)
    { Hero->Feedback = TEXT("Trough is blocked by cover"); return false; }
    const int32 Amount = Deposit ? FMath::Min3(5,30-StoredFood,Hero->Inventory->Count(EFrontierItem::Food)) :
        FMath::Min3(5,StoredFood,UInventoryComponent::StackLimit-Hero->Inventory->Count(EFrontierItem::Food));
    if (Amount <= 0) { Hero->Feedback = TEXT("No transferable food or destination is full"); return false; }
    if (Deposit ? !Hero->Inventory->Consume(EFrontierItem::Food,Amount) : !Hero->Inventory->AddItem(EFrontierItem::Food,Amount)) return false;
    StoredFood += Deposit ? Amount : -Amount;
    Hero->Feedback = FString::Printf(TEXT("Trough %d/30 food | U deposit | O withdraw"),StoredFood);
    RefreshLabel(); return true;
}

bool AWorkStation::ServeFood(ACreatureCharacter* Pet)
{
    if (!HasAuthority() || RequiredSpecialty != EWorkSpecialty::Feeding || StoredFood <= 0 || !IsValid(Pet) || Pet->Health <= 0 ||
        Pet->Disposition != ECreatureDisposition::Tamed || Pet->Nutrition > 80 || Pet->AutoFeedCooldown > 0 || Pet->Activity == ECreatureActivity::Combat || GetDistanceTo(Pet) > 220) return false;
    auto* Hero = Pet->Tamer ? Cast<ACFPlayerCharacter>(Pet->Tamer->GetPawn()) : nullptr;
    if (!Hero || Hero->Health <= 0) return false;
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(TroughMeal),false,this);
    if (GetWorld()->LineTraceSingleByChannel(Hit,GetActorLocation(),Pet->GetActorLocation(),ECC_Visibility,Params) && Hit.GetActor() != Pet) return false;
    --StoredFood; Pet->Nutrition = FMath::Min(100.0f,Pet->Nutrition+35); Pet->Stamina = FMath::Min(100.0f,Pet->Stamina+15);
    Pet->AutoFeedCooldown = 3;
    Hero->Feedback = Pet->GetCreatureName()+TEXT(" ate at the trough and will resume its assignment");
    RefreshLabel(); return true;
}
