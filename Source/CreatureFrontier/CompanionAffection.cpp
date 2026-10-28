#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "CompanionPersonalityComponent.h"
#include "CompanionMemoryComponent.h"

FString ACreatureCharacter::GetBondLabel() const
{
    return BondPoints>=100?TEXT("亲密无间"):BondPoints>=60?TEXT("心意相通"):BondPoints>=20?TEXT("逐渐熟悉"):TEXT("新伙伴");
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
bool ACFPlayerCharacter::CanInteractCompanion(ACreatureCharacter* Pet,bool Playful,FString& OutReason) const
{
    OutReason.Reset();
    if(!HasAuthority() || Health<=0 || UGameplayStatics::IsGamePaused(this) || !IsValid(Pet) || Pet->IsActorBeingDestroyed() || Pet->GetWorld()!=GetWorld() || Pet->Health<=0 || Pet->Disposition!=ECreatureDisposition::Tamed || Pet->Tamer!=GetController() || GetDistanceTo(Pet)>250)
    { OutReason=TEXT("请瞄准2.5米内自己存活的伙伴进行抚摸"); return false; }
    if((Pet->Activity!=ECreatureActivity::Follow && Pet->Activity!=ECreatureActivity::Rest) || Pet->FocusTarget || Pet->SpecialWindupRemaining>0)
    { OutReason=TEXT("伙伴正忙，请先让它跟随或休息"); return false; }
    if(Pet->PettingCooldown>0) { OutReason=FString::Printf(TEXT("再等%.0f秒后才能抚摸这位伙伴"),FMath::CeilToFloat(Pet->PettingCooldown)); return false; }
    const auto Style=Playful?ECompanionPreference::Playful:ECompanionPreference::Quiet;
    if(!Pet->Personality->CanRespond(Style))
    {OutReason=TEXT("玩耍需要跟随中的伙伴、至少20体力和饱食，且不在休息待复工");return false;}
    for(TActorIterator<ACreatureCharacter> It(GetWorld());It;++It)
        if(It->Disposition!=ECreatureDisposition::Tamed && It->Health>0 && It->Activity!=ECreatureActivity::ReturnHome && Pet->GetDistanceTo(*It)<600)
        { OutReason=TEXT("野生生物离得太近，请先到安全地点"); return false; }
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(PettingSight),false,this);
    if(GetWorld()->LineTraceSingleByChannel(Hit,GetPawnViewLocation(),Pet->GetActorLocation(),ECC_Visibility,Params) && Hit.GetActor()!=Pet)
    { OutReason=TEXT("伙伴被障碍物遮挡"); return false; }
    return true;
}
bool ACFPlayerCharacter::InteractCompanion(ACreatureCharacter* Pet,bool Playful)
{
    FString Reason;if(!CanInteractCompanion(Pet,Playful,Reason)){Feedback=Reason;return false;}
    const auto Style=Playful?ECompanionPreference::Playful:ECompanionPreference::Quiet;
    Feedback=Pet->Personality->Respond(Style);
    return true;
}
void ACFPlayerCharacter::PetAimedCompanion() { PetCompanion(TraceCreature()); }
void ACFPlayerCharacter::PlayAimedCompanion() { PlayWithCompanion(TraceCreature()); }
void ACFPlayerCharacter::RecallAimedJourney()
{
    auto* Pet=TraceCreature();
    if(!Pet || !Pet->Memory->Open(this))Feedback=TEXT("F7：在初始基地瞄准2.5米内自己的安全空闲伙伴，分享待回应的水晶之旅");
}
