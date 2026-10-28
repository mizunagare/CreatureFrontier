#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "CompanionPersonalityComponent.h"
#include "CompanionMemoryComponent.h"

FString ACreatureCharacter::GetBondLabel() const
{
    return BondPoints>=100?TEXT("Devoted"):BondPoints>=60?TEXT("Bonded"):BondPoints>=20?TEXT("Familiar"):TEXT("New companion");
}
void ACreatureCharacter::AddBond(int32 Amount)
{
    if(HasAuthority() && Health>0 && Disposition==ECreatureDisposition::Tamed && Tamer && Amount>0)
        BondPoints+=FMath::Min(Amount,100-BondPoints);
}
void ACreatureCharacter::RespondToPetting()
{
    if(HasAuthority() && Health>0 && Disposition==ECreatureDisposition::Tamed)
    { AddBond(2); PettingCooldown=30; AffectionPoseRemaining=2; }
}
void ACreatureCharacter::AcknowledgeName()
{
    if(HasAuthority() && Health>0 && Disposition==ECreatureDisposition::Tamed)
    { AffectionPoseRemaining=FMath::Max(AffectionPoseRemaining,.8f); bPlayfulResponse=false;bFavoriteResponse=false; }
}
void ACreatureCharacter::SetSocialResponse(bool Playful,bool Favorite)
{ bPlayfulResponse=Playful;bFavoriteResponse=Favorite; }
void ACreatureCharacter::RespondToMemory(bool Celebrate)
{if(HasAuthority() && Health>0 && Disposition==ECreatureDisposition::Tamed){AffectionPoseRemaining=2;SetSocialResponse(Celebrate,false);}}
bool ACFPlayerCharacter::PetCompanion(ACreatureCharacter* Pet)
{return InteractCompanion(Pet,false);}
bool ACFPlayerCharacter::PlayWithCompanion(ACreatureCharacter* Pet)
{return InteractCompanion(Pet,true);}
bool ACFPlayerCharacter::InteractCompanion(ACreatureCharacter* Pet,bool Playful)
{
    if(!HasAuthority() || Health<=0 || UGameplayStatics::IsGamePaused(this) || !IsValid(Pet) || Pet->IsActorBeingDestroyed() || Pet->GetWorld()!=GetWorld() || Pet->Health<=0 || Pet->Disposition!=ECreatureDisposition::Tamed || Pet->Tamer!=GetController() || GetDistanceTo(Pet)>250)
    { Feedback=TEXT("Aim at your living companion within 2.5m to pet it"); return false; }
    if((Pet->Activity!=ECreatureActivity::Follow && Pet->Activity!=ECreatureActivity::Rest) || Pet->FocusTarget || Pet->SpecialWindupRemaining>0)
    { Feedback=TEXT("Companion is busy - ask it to follow or rest first"); return false; }
    if(Pet->PettingCooldown>0) { Feedback=FString::Printf(TEXT("Give this companion %.0fs before petting again"),FMath::CeilToFloat(Pet->PettingCooldown)); return false; }
    const auto Style=Playful?ECompanionPreference::Playful:ECompanionPreference::Quiet;
    if(!Pet->Personality->CanRespond(Style))
    {Feedback=TEXT("Play needs a following companion with at least 20 stamina / satiety, not recovering for work");return false;}
    for(TActorIterator<ACreatureCharacter> It(GetWorld());It;++It)
        if(It->Disposition!=ECreatureDisposition::Tamed && It->Health>0 && It->Activity!=ECreatureActivity::ReturnHome && Pet->GetDistanceTo(*It)<600)
        { Feedback=TEXT("A wild creature is too close - find a safe place first"); return false; }
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(PettingSight),false,this);
    if(GetWorld()->LineTraceSingleByChannel(Hit,GetPawnViewLocation(),Pet->GetActorLocation(),ECC_Visibility,Params) && Hit.GetActor()!=Pet)
    { Feedback=TEXT("Companion is blocked by cover"); return false; }
    Feedback=Pet->Personality->Respond(Style);
    return true;
}
void ACFPlayerCharacter::PetAimedCompanion() { PetCompanion(TraceCreature()); }
void ACFPlayerCharacter::PlayAimedCompanion() { PlayWithCompanion(TraceCreature()); }
void ACFPlayerCharacter::RecallAimedJourney()
{
    auto* Pet=TraceCreature();
    if(!Pet || !Pet->Memory->Open(this))Feedback=TEXT("F7: share a pending crystal journey at the original base; own safe idle companion within 2.5m");
}
