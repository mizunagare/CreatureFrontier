#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "EngineUtils.h"

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
        AffectionPoseRemaining=FMath::Max(AffectionPoseRemaining,.8f);
}
bool ACFPlayerCharacter::PetCompanion(ACreatureCharacter* Pet)
{
    if(!HasAuthority() || Health<=0 || IsPauseMenuOpen() || !IsValid(Pet) || Pet->Health<=0 || Pet->Disposition!=ECreatureDisposition::Tamed || Pet->Tamer!=GetController() || GetDistanceTo(Pet)>250)
    { Feedback=TEXT("Aim at your living companion within 2.5m to pet it"); return false; }
    if(Pet->Activity==ECreatureActivity::Work || Pet->Activity==ECreatureActivity::Combat || Pet->FocusTarget || Pet->SpecialWindupRemaining>0)
    { Feedback=TEXT("Companion is busy - ask it to follow or rest first"); return false; }
    if(Pet->PettingCooldown>0) { Feedback=FString::Printf(TEXT("Give this companion %.0fs before petting again"),FMath::CeilToFloat(Pet->PettingCooldown)); return false; }
    for(TActorIterator<ACreatureCharacter> It(GetWorld());It;++It)
        if(It->Disposition!=ECreatureDisposition::Tamed && It->Health>0 && It->Activity!=ECreatureActivity::ReturnHome && Pet->GetDistanceTo(*It)<600)
        { Feedback=TEXT("A wild creature is too close - find a safe place first"); return false; }
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(PettingSight),false,this);
    if(GetWorld()->LineTraceSingleByChannel(Hit,GetPawnViewLocation(),Pet->GetActorLocation(),ECC_Visibility,Params) && Hit.GetActor()!=Pet)
    { Feedback=TEXT("Companion is blocked by cover"); return false; }
    Pet->RespondToPetting();
    Feedback=FString::Printf(TEXT("%s enjoyed that | bond %d/100 (%s) | no combat stat bonus"),*Pet->GetCreatureName(),Pet->BondPoints,*Pet->GetBondLabel());
    return true;
}
void ACFPlayerCharacter::PetAimedCompanion() { PetCompanion(TraceCreature()); }
