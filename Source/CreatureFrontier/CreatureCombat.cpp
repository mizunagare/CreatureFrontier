#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "CFPlayerCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"

bool ACreatureCharacter::HasClearSight(const AActor* Target) const
{
    if (!IsValid(Target)) return false;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CreatureSkillSight), false, this);
    return !GetWorld()->LineTraceSingleByChannel(Hit, GetActorLocation(), Target->GetActorLocation(), ECC_Visibility, Params)
        || Hit.GetActor() == Target;
}

bool ACreatureCharacter::OrderAttack(ACreatureCharacter* Target)
{
    const bool AutoRest = Activity == ECreatureActivity::Rest && bResumeWorkAfterRest && Stamina >= 3;
    if (!HasAuthority() || Health <= 0 || Disposition != ECreatureDisposition::Tamed ||
        Activity == ECreatureActivity::Work || (Activity == ECreatureActivity::Rest && !AutoRest) || !IsValid(Target) ||
        Target->Health <= 0 || Target->Disposition == ECreatureDisposition::Tamed || Target->Activity == ECreatureActivity::ReturnHome) return false;
    FocusTarget = Target;
    if (AutoRest) WorkSite = nullptr;
    SetActivity(ECreatureActivity::Combat);
    return true;
}

bool ACreatureCharacter::IsSpecialReady() const
{
    return Health > 0 && Nutrition > 0 && Disposition == ECreatureDisposition::Tamed && Stamina >= 20 &&
        SpecialCooldownRemaining <= 0 && SpecialWindupRemaining <= 0 &&
        Activity != ECreatureActivity::Work && Activity != ECreatureActivity::Rest;
}

bool ACreatureCharacter::RequestSpecial(ACreatureCharacter* Target)
{
    if (!HasAuthority() || !IsSpecialReady() || !OrderAttack(Target)) return false;
    bSpecialRequested = true;
    SpecialRequestExpires = GetWorld()->GetTimeSeconds() + 6.0;
    return true;
}

void ACreatureCharacter::ClearCombatOrders()
{
    FocusTarget = nullptr;
    SpecialTarget.Reset();
    bSpecialRequested = false;
    SpecialWindupRemaining = 0;
}

FString ACreatureCharacter::GetSpecialName() const
{
    return Definition && !Definition->SpecialName.IsEmpty() ? Definition->SpecialName.ToString() : TEXT("Charged Strike");
}

bool ACreatureCharacter::TickSpecial(float DeltaSeconds)
{
    if (SpecialWindupRemaining > 0)
    {
        StopNavigation();
        SpecialWindupRemaining = FMath::Max(0.0f, SpecialWindupRemaining - DeltaSeconds);
        if (SpecialWindupRemaining <= 0) ResolveSpecial();
        return true;
    }
    if (!bSpecialRequested) return false;
    if (!IsValid(FocusTarget) || FocusTarget->Health <= 0 || FocusTarget->Disposition == ECreatureDisposition::Tamed ||
        GetWorld()->GetTimeSeconds() > SpecialRequestExpires || !IsSpecialReady())
    {
        bSpecialRequested = false;
        return false;
    }
    const float Range = Definition ? Definition->SpecialRange : 600.0f;
    if (GetDistanceTo(FocusTarget) > Range || !HasClearSight(FocusTarget))
    {
        NavigateTo(FocusTarget->GetActorLocation(), Range * 0.5f);
        return true;
    }
    StopNavigation();
    Stamina -= 20.0f;
    SpecialTarget = FocusTarget;
    SpecialWindupRemaining = 0.65f;
    SpecialCooldownRemaining = Definition ? Definition->SpecialCooldownSeconds : 8.0f;
    bSpecialRequested = false;
    if (auto* Hero = Tamer ? Cast<ACFPlayerCharacter>(Tamer->GetPawn()) : nullptr)
        Hero->Feedback = GetCreatureName() + TEXT(": ") + GetSpecialName();
    return true;
}

void ACreatureCharacter::ResolveSpecial()
{
    auto* Target = SpecialTarget.Get();
    SpecialTarget.Reset();
    const float Range = Definition ? Definition->SpecialRange : 600.0f;
    if (!Target || Target->Health <= 0 || Target->Disposition == ECreatureDisposition::Tamed || Target->Activity == ECreatureActivity::ReturnHome ||
        GetDistanceTo(Target) > Range || !HasClearSight(Target)) return;
    const float Damage = GetAttackDamage() * (Definition ? Definition->SpecialDamageScale : 2.0f);
    UGameplayStatics::ApplyDamage(Target, Damage, Tamer, this, UDamageType::StaticClass());
    SkillPulseRemaining = 0.3f;
    const ECreatureForm Form = Definition ? Definition->Form : ECreatureForm::Leaf;
    if (Form == ECreatureForm::Leaf) Target->SlowRemaining = FMath::Max(Target->SlowRemaining, 4.0f);
    if (Form == ECreatureForm::Stone) GuardRemaining = 5.0f;
    if (Form == ECreatureForm::Ember)
    {
        for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
            if (*It != Target && It->Disposition != ECreatureDisposition::Tamed && It->Health > 0 &&
                It->GetDistanceTo(Target) <= 220.0f && HasClearSight(*It))
                UGameplayStatics::ApplyDamage(*It, Damage * 0.5f, Tamer, this, UDamageType::StaticClass());
    }
}
