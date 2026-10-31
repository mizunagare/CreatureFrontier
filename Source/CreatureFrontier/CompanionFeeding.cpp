#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "InventoryComponent.h"
#include "WorkStation.h"
#include "Camera/CameraComponent.h"
#include "CompanionCargoComponent.h"
#include "Kismet/GameplayStatics.h"

void ACFPlayerCharacter::CraftFood()
{
    CraftRecipe(TEXT("Food"));
}

bool ACFPlayerCharacter::FeedCompanion(ACreatureCharacter* Pet)
{
    if (!HasAuthority() || Health <= 0 || UGameplayStatics::IsGamePaused(this) || !IsValid(Pet) || Pet->Tamer != GetController() || Pet->Health <= 0 || GetDistanceTo(Pet) > 600)
    { Feedback = TEXT("请在6米内喂食存活的伙伴；倒下的伙伴需用药品救治"); return false; }
    if (GetWorld()->GetTimeSeconds() < NextFeedTime) { Feedback = TEXT("请等待喂食冷却（3秒）"); return false; }
    if (Pet->Nutrition > 80) { Feedback = TEXT("伙伴不饿（饱食需不高于80）"); return false; }
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(FeedingSight),false,this);
    if (GetWorld()->LineTraceSingleByChannel(Hit,GetPawnViewLocation(),Pet->GetActorLocation(),ECC_Visibility,Params) && Hit.GetActor() != Pet)
    { Feedback = TEXT("伙伴被障碍物遮挡"); return false; }
    if (!Inventory->Consume(EFrontierItem::Food,1)) { Feedback = TEXT("没有食物，请种植或在基地按3制作"); return false; }
    Pet->Nutrition = FMath::Min(100.0f,Pet->Nutrition+35);
    Pet->Stamina = FMath::Min(100.0f,Pet->Stamina+15);
    Pet->AddBond(3);
    NextFeedTime = GetWorld()->GetTimeSeconds()+3;
    Feedback = TEXT("已喂食伙伴：饱食+35，体力+15。E 继续工作 | F 跟随");
    return true;
}

void ACFPlayerCharacter::FeedAimedCompanion()
{
    auto* Creature=TraceCreature();
    if(Creature && Creature->Disposition!=ECreatureDisposition::Tamed)BefriendWildCreature(Creature);
    else FeedCompanion(Creature);
}

AWorkStation* ACFPlayerCharacter::TraceFacility() const
{
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(FacilityAim),false,this);
    const FVector Start = FirstPersonCamera->GetComponentLocation();
    GetWorld()->LineTraceSingleByChannel(Hit,Start,Start+FirstPersonCamera->GetForwardVector()*600,ECC_Visibility,Params);
    return Cast<AWorkStation>(Hit.GetActor());
}

void ACFPlayerCharacter::DepositTroughFood()
{
    auto* Site = TraceFacility();
    if (!Site || !Site->IsFoodStore()) Feedback = TEXT("请瞄准6米内的农圃或食槽");
    else Site->TransferFood(this,true);
}

void ACFPlayerCharacter::WithdrawTroughFood()
{
    auto* Site = TraceFacility();
    if (!Site || !Site->IsFoodStore()) Feedback = TEXT("请瞄准6米内的农圃或食槽");
    else Site->TransferFood(this,false);
}

void ACFPlayerCharacter::WithdrawSingleFood()
{
    auto* Site=TraceFacility();
    if(!Site || !Site->IsFoodStore())Feedback=TEXT("请瞄准6米内的农圃或食槽");
    else Site->TransferFood(this,false,true);
}

bool ACFPlayerCharacter::WithdrawCompanionCargo(ACreatureCharacter* Pet)
{
    if(!HasAuthority() || Health<=0 || !IsValid(Pet) || Pet->Tamer!=GetController() || Pet->Disposition!=ECreatureDisposition::Tamed || GetDistanceTo(Pet)>600) return false;
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(CargoRecovery),false,this);
    if(GetWorld()->LineTraceSingleByChannel(Hit,GetPawnViewLocation(),Pet->GetActorLocation(),ECC_Visibility,Params) && Hit.GetActor()!=Pet) return false;
    if(Pet->Cargo->Count()>0) return Pet->Cargo->Recover(this);
    const int32 Amount=FMath::Min(Pet->CarriedFood,999-Inventory->Count(EFrontierItem::Food));
    if(Amount<=0 || !Inventory->AddItem(EFrontierItem::Food,Amount)) return false;
    Pet->CarriedFood-=Amount; Feedback=FString::Printf(TEXT("已从伙伴处取回%d份食物"),Amount); return true;
}
void ACFPlayerCharacter::WithdrawAimedCargo()
{ if(!WithdrawCompanionCargo(TraceCreature())) Feedback=TEXT("请瞄准6米内有携带物的自有伙伴，并确保背包有空间"); }
