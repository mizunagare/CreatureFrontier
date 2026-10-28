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
    if (bRosterOpen) { bJournalOpen = false; bHelpVisible=false; bBackpackOpen=false; }
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
    { Feedback = TEXT("请瞄准12米内自己存活的伙伴进行选择"); return false; }
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(CompanionSelection),false,this);
    if (GetWorld()->LineTraceSingleByChannel(Hit,FirstPersonCamera->GetComponentLocation(),Pet->GetActorLocation(),ECC_Visibility,Params) && Hit.GetActor() != Pet)
    { Feedback = TEXT("障碍物阻挡了伙伴选择"); return false; }
    SelectedCompanion = Pet; bSingleCompanionMode = true;
    Feedback = Pet->GetCreatureName()+TEXT("已选择。F/E/T/Q和G技能仅指挥它；瞄准农圃按G照料；K恢复全队指挥");
    return true;
}

void ACFPlayerCharacter::SelectAimedCompanion() { SelectCompanion(TraceCreature()); }

void ACFPlayerCharacter::ClearCompanionSelection()
{
    SelectedCompanion.Reset(); bSingleCompanionMode = false;
    Feedback = TEXT("当前指挥全部伙伴，L 选择瞄准的单个伙伴");
}

void ACFPlayerCharacter::ToggleCompanionCombatStance()
{
    if(!HasAuthority() || Health<=0 || !GetController())return;
    TArray<ACreatureCharacter*> Targets;
    bool AnyGuard=false;
    for(auto* Pet:GetCompanionRoster())
        if(IsCommandTarget(Pet) && Pet->Health>0 && GetDistanceTo(Pet)<=1800)
        {Targets.Add(Pet);AnyGuard |= !Pet->bAvoidCombat;}
    int32 Count=0;
    for(auto* Pet:Targets)if(Pet->SetAvoidCombat(AnyGuard))++Count;
    Feedback=Count?FString::Printf(TEXT("%d位伙伴：%s | Q/G仍可明确出击 | F8切换"),Count,AnyGuard?TEXT("避战，不自动护主"):TEXT("自动护主")):
        TEXT("18米内没有当前受指挥的存活伙伴");
}

bool ACFPlayerCharacter::IsCommandTarget(const ACreatureCharacter* Pet) const
{
    if (!GetController() || !IsValid(Pet) || Pet->Tamer != GetController()) return false;
    return !bSingleCompanionMode || (SelectedCompanion.Get() == Pet && Pet->Health > 0 && Pet->Disposition == ECreatureDisposition::Tamed);
}

FString ACFPlayerCharacter::GetCommandScopeText() const
{
    if (!bSingleCompanionMode) return TEXT("全队指挥 | L 选择瞄准伙伴 | K 全队");
    const auto* Pet = SelectedCompanion.Get();
    if (!Pet || Pet->Tamer != GetController()) return TEXT("选择已失效，命令暂停 | L 重选 | K 全队");
    return Pet->Health > 0 ? TEXT("仅指挥 ")+Pet->GetCreatureName()+TEXT(" | L 选择 | K 全队") :
        TEXT("所选伙伴已倒下，命令暂停 | P 治疗 | K 全队");
}
