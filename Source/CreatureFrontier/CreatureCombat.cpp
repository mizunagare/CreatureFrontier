#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "CFPlayerCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "CompanionSkillTrail.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

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
    if(FocusTarget!=Target && bCombatRetreatActive)
    {StopNavigation();NextNavigationUpdate=0;NextCombatRetreatSearch=0;}
    FocusTarget = Target;
    if (AutoRest) WorkSite = nullptr;
    SetActivity(ECreatureActivity::Combat);
    return true;
}

APawn* ACreatureCharacter::FindVisibleWildThreat(APawn* Player) const
{
    APawn* Result=nullptr;float Closest=TNumericLimits<float>::Max();
    auto Consider=[&](APawn* Candidate)
    {
        if(!IsValid(Candidate))return;
        const float Distance=FVector::Dist2D(GetActorLocation(),Candidate->GetActorLocation());
        if(Distance<=AggroRange && Distance<Closest && HasClearSight(Candidate)){Result=Candidate;Closest=Distance;}
    };
    Consider(Player);
    for(TActorIterator<ACreatureCharacter> It(GetWorld());It;++It)
        if(It->Disposition==ECreatureDisposition::Tamed && It->Health>0)Consider(*It);
    return Result;
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
    if(FParse::Param(FCommandLine::Get(),TEXT("CFCombatTrace")) && (SpecialWindupRemaining>0 || bSpecialRequested))
        UE_LOG(LogTemp,Display,TEXT("CF_SKILL_TRACE_CANCEL actor=%s target=%s requested=%d windup=%.3f"),*PersistentId.ToString(),*GetNameSafe(FocusTarget),bSpecialRequested,SpecialWindupRemaining);
    if(bCombatRetreatActive)StopNavigation();
    CombatRetreatRoute.Reset();
    SeenThreat.Reset();SeenThreatRemaining=0;LastSeenThreatLocation=FVector::ZeroVector;
    FocusTarget = nullptr;
    SpecialTarget.Reset();
    bSpecialRequested = false;
    SpecialWindupRemaining = 0;
}

bool ACreatureCharacter::SetAvoidCombat(bool Avoid)
{
    if(!HasAuthority() || Health<=0 || Disposition!=ECreatureDisposition::Tamed || !Tamer)return false;
    bAvoidCombat=Avoid;
    if(Avoid)
    {
        ClearCombatOrders();
        if(Activity==ECreatureActivity::Combat){StopNavigation();SetActivity(ECreatureActivity::Follow);}
    }
    return true;
}

void ACreatureCharacter::HandlePlayerDowned(const ACFPlayerCharacter* Player)
{
    if(!HasAuthority() || !Player || Player->Health>0) return;
    if(Disposition==ECreatureDisposition::Tamed)
    {
        if(Tamer!=Player->GetController()) return;
        ClearCombatOrders(); StopNavigation();
        if(Health>0 && Activity==ECreatureActivity::Combat) { WorkSite=nullptr; SetActivity(ECreatureActivity::Follow); }
        return;
    }
    // 当前原型只有一名玩家：清除反击意图，但不治疗或移动野生生物。
    RetaliationTarget.Reset(); RetaliationRemaining=0; AlphaWindup=0; ClearCombatOrders(); StopNavigation();
    if(Activity==ECreatureActivity::Combat) { Disposition=ECreatureDisposition::Wild; SetActivity(ECreatureActivity::Idle); }
}

FString ACreatureCharacter::GetSpecialName() const
{
    return Definition && !Definition->SpecialName.IsEmpty() ? Definition->SpecialName.ToString() : TEXT("蓄力打击");
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
    if(FParse::Param(FCommandLine::Get(),TEXT("CFCombatTrace")))
        UE_LOG(LogTemp,Display,TEXT("CF_SKILL_TRACE_BEGIN actor=%s target=%s distance=%.2f range=%.2f damage=%.2f scale=%.2f stamina=%.2f"),*PersistentId.ToString(),*FocusTarget->PersistentId.ToString(),GetDistanceTo(FocusTarget),Range,GetAttackDamage(),Definition?Definition->SpecialDamageScale:2.f,Stamina);
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
        GetDistanceTo(Target) > Range || !HasClearSight(Target))
    {
        if(FParse::Param(FCommandLine::Get(),TEXT("CFCombatTrace")))
        {
            FHitResult Hit;FCollisionQueryParams Params(SCENE_QUERY_STAT(SkillOutcomeTrace),false,this);
            if(Target)GetWorld()->LineTraceSingleByChannel(Hit,GetActorLocation(),Target->GetActorLocation(),ECC_Visibility,Params);
            const TCHAR* Reason=!Target?TEXT("target-invalid"):Target->Health<=0?TEXT("target-downed"):Target->Disposition==ECreatureDisposition::Tamed?TEXT("target-tamed"):Target->Activity==ECreatureActivity::ReturnHome?TEXT("target-returning"):GetDistanceTo(Target)>Range?TEXT("range"):TEXT("sight");
            UE_LOG(LogTemp,Display,TEXT("CF_SKILL_TRACE_RESOLVE actor=%s target=%s outcome=miss reason=%s distance=%.2f range=%.2f blocker=%s impact=%s"),*PersistentId.ToString(),Target?*Target->PersistentId.ToString():TEXT("none"),Reason,Target?GetDistanceTo(Target):-1.f,Range,*GetNameSafe(Hit.GetActor()),*Hit.ImpactPoint.ToString());
        }
        return;
    }
    const float Damage = GetAttackDamage() * (Definition ? Definition->SpecialDamageScale : 2.0f);
    const float Before=Target->Health;
    const float Applied=UGameplayStatics::ApplyDamage(Target, Damage, Tamer, this, UDamageType::StaticClass());
    if(FParse::Param(FCommandLine::Get(),TEXT("CFCombatTrace")))
        UE_LOG(LogTemp,Display,TEXT("CF_SKILL_TRACE_RESOLVE actor=%s target=%s outcome=hit raw=%.2f applied=%.2f before=%.2f after=%.2f distance=%.2f range=%.2f"),*PersistentId.ToString(),*Target->PersistentId.ToString(),Damage,Applied,Before,Target->Health,GetDistanceTo(Target),Range);
    // 在已通过距离/视线检查的真实结算点生成，不把表现当作第二次攻击。
    if(auto* Trail=GetWorld()->SpawnActor<ACompanionSkillTrail>())
    {
        Trail->SetOwner(this);
        Trail->Initialize(GetActorLocation()+FVector(0,0,15),Target->GetActorLocation()+FVector(0,0,15),Definition);
    }
    SkillPulseRemaining = 0.3f;
    BeginStrikePose(Target);
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
