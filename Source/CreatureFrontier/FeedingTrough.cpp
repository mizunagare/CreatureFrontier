#include "WorkStation.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "InventoryComponent.h"

bool AWorkStation::TransferFood(ACFPlayerCharacter* Hero, bool Deposit,bool SingleWithdrawal)
{
    if (!HasAuthority() || !IsFoodStore() || !IsValid(Hero) || Hero->Health <= 0 || GetDistanceTo(Hero) > 600) return false;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(TroughTransfer),false,Hero);
    if (GetWorld()->LineTraceSingleByChannel(Hit,Hero->GetPawnViewLocation(),GetActorLocation(),ECC_Visibility,Params) && Hit.GetActor() != this)
    { Hero->Feedback = TEXT("食物设施被障碍物遮挡"); return false; }
    const int32 Amount = Deposit ? FMath::Min3(5,30-StoredFood,Hero->Inventory->Count(EFrontierItem::Food)) :
        FMath::Min3(SingleWithdrawal?1:5,StoredFood,UInventoryComponent::StackLimit-Hero->Inventory->Count(EFrontierItem::Food));
    if (Amount <= 0) { Hero->Feedback = TEXT("没有可转移食物，或目标容量已满"); return false; }
    if (Deposit ? !Hero->Inventory->Consume(EFrontierItem::Food,Amount) : !Hero->Inventory->AddItem(EFrontierItem::Food,Amount)) return false;
    StoredFood += Deposit ? Amount : -Amount;
    Hero->Feedback = FString::Printf(TEXT("已%s%d份食物 | 储量 %d/30"),Deposit?TEXT("存入"):TEXT("取出"),Amount,StoredFood);
    RefreshLabel(); return true;
}

bool AWorkStation::AdjustFoodStock(int32 Delta)
{
    if(!HasAuthority() || !IsFoodStore() || Delta==0 || Delta < -StoredFood || Delta > 30-StoredFood) return false;
    StoredFood+=Delta; RefreshLabel(); return true;
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
    Hero->Feedback = Pet->GetCreatureName()+TEXT("已在食槽进食，即将继续原工作");
    RefreshLabel(); return true;
}
