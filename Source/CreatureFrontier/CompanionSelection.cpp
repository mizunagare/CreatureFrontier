#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"

TArray<ACreatureCharacter*> ACFPlayerCharacter::GetCompanionRoster() const
{
    TArray<ACreatureCharacter*> Result;
    if (!GetController()) return Result;
    for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
        if (It->Disposition == ECreatureDisposition::Tamed && It->Tamer == GetController()) Result.Add(*It);
    Result.Sort([](const ACreatureCharacter& A,const ACreatureCharacter& B) { return A.GetName() < B.GetName(); });
    return Result;
}

void ACFPlayerCharacter::ToggleRoster()
{
    bRosterOpen = !bRosterOpen;
    if (bRosterOpen && !GetCompanionRoster().Contains(RosterHighlight.Get())) StepRoster(1);
}

void ACFPlayerCharacter::StepRoster(int32 Direction)
{
    if (!bRosterOpen) return;
    const auto Pets = GetCompanionRoster();
    if (Pets.IsEmpty()) { RosterHighlight.Reset(); return; }
    const int32 Current = Pets.IndexOfByKey(RosterHighlight.Get());
    const int32 Next = Current == INDEX_NONE ? 0 : (Current+Direction+Pets.Num())%Pets.Num();
    RosterHighlight = Pets[Next];
}

void ACFPlayerCharacter::NextRosterCompanion() { StepRoster(1); }
void ACFPlayerCharacter::PreviousRosterCompanion() { StepRoster(-1); }
void ACFPlayerCharacter::SelectRosterCompanion()
{
    if (bRosterOpen) SelectCompanion(RosterHighlight.Get());
}

bool ACFPlayerCharacter::SelectCompanion(ACreatureCharacter* Pet)
{
    if (!HasAuthority() || !GetController() || Health <= 0 || !IsValid(Pet) || Pet->Disposition != ECreatureDisposition::Tamed || Pet->Tamer != GetController() || Pet->Health <= 0 || GetDistanceTo(Pet) > 1200)
    { Feedback = TEXT("Aim at your living companion within 12m to select it"); return false; }
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(CompanionSelection),false,this);
    if (GetWorld()->LineTraceSingleByChannel(Hit,FirstPersonCamera->GetComponentLocation(),Pet->GetActorLocation(),ECC_Visibility,Params) && Hit.GetActor() != Pet)
    { Feedback = TEXT("Companion selection is blocked by cover"); return false; }
    SelectedCompanion = Pet; bSingleCompanionMode = true;
    Feedback = Pet->GetCreatureName()+TEXT(" selected. F/E/T/Q/G affect only this companion; K returns to ALL");
    return true;
}

void ACFPlayerCharacter::SelectAimedCompanion() { SelectCompanion(TraceCreature()); }

void ACFPlayerCharacter::ClearCompanionSelection()
{
    SelectedCompanion.Reset(); bSingleCompanionMode = false;
    Feedback = TEXT("Commands target ALL companions. L selects an aimed companion");
}

bool ACFPlayerCharacter::IsCommandTarget(const ACreatureCharacter* Pet) const
{
    if (!GetController() || !IsValid(Pet) || Pet->Tamer != GetController()) return false;
    return !bSingleCompanionMode || (SelectedCompanion.Get() == Pet && Pet->Health > 0 && Pet->Disposition == ECreatureDisposition::Tamed);
}

FString ACFPlayerCharacter::GetCommandScopeText() const
{
    if (!bSingleCompanionMode) return TEXT("ALL companions | L select aimed | K all");
    const auto* Pet = SelectedCompanion.Get();
    if (!Pet || Pet->Tamer != GetController()) return TEXT("Selection lost - commands blocked | L select | K all");
    return Pet->Health > 0 ? TEXT("ONLY ")+Pet->GetCreatureName()+TEXT(" | L select | K all") :
        TEXT("Selected companion downed - commands blocked | P treat | K all");
}
