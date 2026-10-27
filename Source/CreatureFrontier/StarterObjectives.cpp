#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "WorkStation.h"
#include "InventoryComponent.h"

namespace
{
    constexpr int32 StarterObjectiveTargets[]={1,1,3,2,1};
    const TCHAR* StarterObjectiveTitles[]={TEXT("Discover a creature"),TEXT("Capture your first companion"),TEXT("Produce 3 materials with companions"),TEXT("Grow 2 food with companions"),TEXT("Capture or defeat the Crystal Rise alpha")};
    const TCHAR* StarterObjectiveHints[]={TEXT("Aim at a living creature within 12m | I: journal"),TEXT("Weaken a wild creature, then RMB to capture"),TEXT("E near a workbench | matching specialties work faster"),TEXT("B / V: build Farming (18 materials), then E to assign"),TEXT("Crystal Rise: right-front valley | Q focus / G skill")};
    TMap<EFrontierItem,int32> Reward(int32 Index)
    {
        switch(Index)
        {
            case 0:return {{EFrontierItem::CaptureDevice,2}};
            case 1:return {{EFrontierItem::Material,12}};
            case 2:return {{EFrontierItem::Material,12}};
            case 3:return {{EFrontierItem::Medicine,2}};
            case 4:return {{EFrontierItem::Material,20},{EFrontierItem::CaptureDevice,3}};
            default:return {};
        }
    }
}

int32 ACFPlayerCharacter::GetObjectiveProgress(int32 Index) const
{
    switch(Index)
    {
        case 0:return Journal.IsEmpty()?0:1;
        case 1:for(const auto& Entry:Journal) if(Entry.bCaptured) return 1; return 0;
        case 2:return Objectives.WorkMaterials;
        case 3:return Objectives.FarmFood;
        case 4:return Objectives.bAlphaResolved?1:0;
        default:return 0;
    }
}

void ACFPlayerCharacter::RecordCompanionProduction(const ACreatureCharacter* Pet,EFrontierItem Item)
{
    if(!HasAuthority() || !GetController() || !IsValid(Pet) || Pet->Health<=0 || Pet->Tamer!=GetController() || Pet->Activity!=ECreatureActivity::Work || !IsValid(Pet->WorkSite)) return;
    const bool Farm=Pet->WorkSite->RequiredSpecialty==EWorkSpecialty::Farming;
    if(Item==EFrontierItem::Food && Farm) Objectives.FarmFood=FMath::Min(2,Objectives.FarmFood+1);
    if(Item==EFrontierItem::Material && !Farm) Objectives.WorkMaterials=FMath::Min(3,Objectives.WorkMaterials+1);
}

void ACFPlayerCharacter::RecordAlphaResolution(const ACreatureCharacter* Creature)
{
    if(!HasAuthority() || !GetController() || !IsValid(Creature) || !Creature->bAlpha) return;
    if((Creature->Health<=0 && Creature->Disposition!=ECreatureDisposition::Tamed) || (Creature->Disposition==ECreatureDisposition::Tamed && Creature->Tamer==GetController())) Objectives.bAlphaResolved=true;
}

void ACFPlayerCharacter::ClaimObjectiveReward()
{
    if(!HasAuthority() || !GetController() || Health<=0) return;
    const int32 Index=Objectives.Claimed;
    if(Index>=5) { Feedback=TEXT("Starter journey complete - explore, build and train your companions"); return; }
    if(GetObjectiveProgress(Index)<StarterObjectiveTargets[Index]) { Feedback=TEXT("Objective not complete - follow the journey hint"); return; }
    if(!Inventory->GrantItems(Reward(Index))) { Feedback=TEXT("Reward needs inventory space - free space and press N again"); return; }
    ++Objectives.Claimed;
    Feedback=FString::Printf(TEXT("Journey reward claimed (%d/5) - F5 to save"),Objectives.Claimed);
}

FString ACFPlayerCharacter::GetObjectiveStatus() const
{
    const int32 Index=Objectives.Claimed;
    if(Index>=5) return TEXT("STARTER JOURNEY COMPLETE | continue exploring and building");
    return FString::Printf(TEXT("JOURNEY %d/5 | %s (%d/%d)"),Index+1,StarterObjectiveTitles[Index],GetObjectiveProgress(Index),StarterObjectiveTargets[Index]);
}
FString ACFPlayerCharacter::GetObjectiveHint() const
{
    return Objectives.Claimed>=5?TEXT("F5 saves | F9 restores the saved snapshot"):GetObjectiveProgress(Objectives.Claimed)>=StarterObjectiveTargets[Objectives.Claimed]?TEXT("READY - press N to claim reward and reveal the next objective"):StarterObjectiveHints[Objectives.Claimed];
}
FString ACFPlayerCharacter::GetObjectiveRewardText() const
{
    const TCHAR* Texts[]={TEXT("Reward: 2 capture devices"),TEXT("Reward: 12 materials"),TEXT("Reward: 12 materials"),TEXT("Reward: 2 medicine"),TEXT("Reward: 20 materials + 3 capture devices")};
    return Objectives.Claimed>=5?TEXT("All five rewards claimed in this save"):Texts[Objectives.Claimed];
}
