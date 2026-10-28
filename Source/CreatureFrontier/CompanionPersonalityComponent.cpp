#include "CompanionPersonalityComponent.h"
#include "CreatureCharacter.h"
#include "Kismet/GameplayStatics.h"

ECompanionPreference UCompanionPersonalityComponent::PreferenceForId(const FGuid& Id)
{
    // 首次分配及旧档迁移使用显式映射，不依赖版本变化。
    return ((Id.A^Id.B^Id.C^Id.D)&1u)?ECompanionPreference::Playful:ECompanionPreference::Quiet;
}
bool UCompanionPersonalityComponent::ValidateSnapshot(ECompanionPreference Preference,bool Discovered,bool OwnedTamed)
{
    return OwnedTamed?(Preference==ECompanionPreference::Quiet || Preference==ECompanionPreference::Playful):
        Preference==ECompanionPreference::None && !Discovered;
}
void UCompanionPersonalityComponent::EnsurePreference()
{
    const auto* Pet=Cast<ACreatureCharacter>(GetOwner());
    if(Pet && Pet->HasAuthority() && Pet->PersistentId.IsValid() && Pet->Disposition==ECreatureDisposition::Tamed && Pet->Tamer && Preferred==ECompanionPreference::None)
        Preferred=PreferenceForId(Pet->PersistentId);
}
void UCompanionPersonalityComponent::RestoreSnapshot(ECompanionPreference Preference,bool Discovered)
{
    if(GetOwner()->HasAuthority()) {Preferred=Preference;bDiscovered=Discovered;}
}
FString UCompanionPersonalityComponent::GetSummary() const
{
    return !bDiscovered?TEXT("偏好尚未发现 | 试试 Y / F6"):Preferred==ECompanionPreference::Quiet?
        TEXT("喜欢安静陪伴（Y）"):TEXT("喜欢活泼玩耍（F6）");
}
bool UCompanionPersonalityComponent::CanRespond(ECompanionPreference Style) const
{
    const auto* Pet=Cast<ACreatureCharacter>(GetOwner());
    if(!Pet || !Pet->HasAuthority() || UGameplayStatics::IsGamePaused(this) || Pet->Health<=0 || Pet->Disposition!=ECreatureDisposition::Tamed || !Pet->Tamer || Pet->PettingCooldown>0) return false;
    if(Style!=ECompanionPreference::Quiet && Style!=ECompanionPreference::Playful) return false;
    return Style!=ECompanionPreference::Playful || (Pet->Activity==ECreatureActivity::Follow && !Pet->bResumeWorkAfterRest && Pet->Stamina>=20 && Pet->Nutrition>=20);
}
FString UCompanionPersonalityComponent::Respond(ECompanionPreference Style)
{
    if(!CanRespond(Style)) return FString();
    auto* Pet=CastChecked<ACreatureCharacter>(GetOwner());EnsurePreference();
    const bool Favorite=Style==Preferred,NewDiscovery=Favorite && !bDiscovered;
    if(Favorite) bDiscovered=true;
    if(Style==ECompanionPreference::Playful) Pet->Stamina-=5;
    Pet->RespondToPetting();Pet->SetSocialResponse(Style==ECompanionPreference::Playful,Favorite);
    return FString::Printf(TEXT("%s %s%s | %s | 亲密 %d/100 | 共享冷却30秒"),*Pet->GetCreatureName(),
        Favorite?TEXT("露出了欣喜的神情！"):TEXT("享受着你的陪伴"),NewDiscovery?TEXT(" 发现了新的偏好。"):TEXT(""),*GetSummary(),Pet->BondPoints);
}
