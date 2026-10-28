#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "WorkStation.h"
#include "InventoryComponent.h"

namespace
{
    constexpr int32 StarterObjectiveTargets[]={1,1,3,2,1};
    const TCHAR* StarterObjectiveTitles[]={TEXT("发现一种生物"),TEXT("结交第一位伙伴"),TEXT("与伙伴生产3份材料"),TEXT("与伙伴种出2份食物"),TEXT("捕捉或击败水晶高地首领")};
    const TCHAR* StarterObjectiveHints[]={TEXT("瞄准12米内的活体生物 | I 图鉴"),TEXT("J 向平静苔跳兽分享3食物，或削弱野生生物后右键捕捉"),TEXT("工作台附近按E | 专长匹配工作更快"),TEXT("B / V 建造农圃（18材料），再按E指派"),TEXT("水晶高地：右前方山谷 | Q 集火 / G 技能")};
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
    if(Index>=5) { Feedback=TEXT("启程旅程已完成，继续探索、建造和培养伙伴吧"); return; }
    if(GetObjectiveProgress(Index)<StarterObjectiveTargets[Index]) { Feedback=TEXT("目标尚未完成，请参考旅程提示"); return; }
    if(!Inventory->GrantItems(Reward(Index))) { Feedback=TEXT("背包空间不足，腾出空间后再按N领取奖励"); return; }
    ++Objectives.Claimed;
    Feedback=FString::Printf(TEXT("已领取旅程奖励（%d/5）| F5 保存"),Objectives.Claimed);
}

FString ACFPlayerCharacter::GetObjectiveStatus() const
{
    const int32 Index=Objectives.Claimed;
    if(Index>=5) return TEXT("启程旅程已完成 | 继续探索与建造");
    return FString::Printf(TEXT("旅程 %d/5 | %s（%d/%d）"),Index+1,StarterObjectiveTitles[Index],GetObjectiveProgress(Index),StarterObjectiveTargets[Index]);
}
FString ACFPlayerCharacter::GetObjectiveHint() const
{
    return Objectives.Claimed>=5?TEXT("F5 保存 | F9 读取存档"):GetObjectiveProgress(Objectives.Claimed)>=StarterObjectiveTargets[Objectives.Claimed]?TEXT("已完成，按N领取奖励并揭晓下个目标"):StarterObjectiveHints[Objectives.Claimed];
}
FString ACFPlayerCharacter::GetObjectiveRewardText() const
{
    const TCHAR* Texts[]={TEXT("奖励：2个捕捉器"),TEXT("奖励：12份材料"),TEXT("奖励：12份材料"),TEXT("奖励：2份药品"),TEXT("奖励：20份材料和3个捕捉器")};
    return Objectives.Claimed>=5?TEXT("本存档已领取全部五项奖励"):Texts[Objectives.Claimed];
}
