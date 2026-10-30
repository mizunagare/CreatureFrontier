#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "WorkStation.h"
#include "InventoryComponent.h"
#include "FarmCropComponent.h"
#include "BaseBuildingComponent.h"
#include "FrontierOrientation.h"
#include "EngineUtils.h"

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
    FString FarmingHint(const ACFPlayerCharacter* Hero)
    {
        AWorkStation* Garden=nullptr;double Distance=DBL_MAX;
        // 优先说明伙伴正在使用的农圃，避免另一个更近的空圃把现有任务提示盖掉。
        for(TActorIterator<ACreatureCharacter> It(Hero->GetWorld());It;++It)
            if(It->Tamer==Hero->GetController() && It->Health>0 && IsValid(It->WorkSite) && It->WorkSite->RequiredSpecialty==EWorkSpecialty::Farming &&
                (It->Activity==ECreatureActivity::Work || It->bResumeWorkAfterRest))
                if(const double D=Hero->GetDistanceTo(It->WorkSite);D<Distance){Garden=It->WorkSite;Distance=D;}
        if(!Garden)for(TActorIterator<AWorkStation> It(Hero->GetWorld());It;++It)
            if(!It->IsActorBeingDestroyed() && It->RequiredSpecialty==EWorkSpecialty::Farming && Hero->GetDistanceTo(*It)<Distance){Garden=*It;Distance=Hero->GetDistanceTo(*It);}
        if(!Garden)
        {
            const int32 Missing=FMath::Max(0,UBaseBuildingComponent::GetCost(EWorkSpecialty::Farming)-Hero->Inventory->Count(EFrontierItem::Material));
            return Missing>0?FString::Printf(TEXT("农圃还缺%d材料；M采集，C查看配方\n备齐后B建造，V选择农圃"),Missing):TEXT("B建造、V选择农圃，需18材料\n建好后还需种子和伙伴派工");
        }
        if(Distance>800)
        {
            const auto Delta=Garden->GetActorLocation()-Hero->GetActorLocation();
            return FString::Printf(TEXT("农圃在%s%d米（直线），先靠近\n不是可通行路线，注意绕开障碍"),FrontierOrientation::Heading(FMath::RadiansToDegrees(FMath::Atan2(Delta.Y,Delta.X))),FMath::CeilToInt(Distance/100));
        }
        if(Garden->StoredFood>=30)return TEXT("农圃粮仓已满，靠近6米瞄准按O取粮\n腾出空间后伙伴才能继续收获");
        if(!Garden->Crop->bPlanted && Garden->Crop->Seeds==0)
        {
            if(Hero->Inventory->Count(EFrontierItem::Seeds)>0)return TEXT("靠近3米瞄农圃按F2存入种子\n再瞄农圃按E派伙伴，勿只手动收获");
            return TEXT("缺种：基地C选择准备种子，需食物和纤维\n缺食物可按3制作；种好后让伙伴收获");
        }
        for(TActorIterator<ACreatureCharacter> It(Hero->GetWorld());It;++It)
            if(It->Tamer==Hero->GetController() && It->Health>0 && It->WorkSite==Garden && (It->Activity==ECreatureActivity::Work || It->bResumeWorkAfterRest))
                return TEXT("伙伴已派工，留出农圃接近通道\n查看伙伴饱食和体力，等待伙伴收获");
        return TEXT("农圃已有种子或作物，瞄准按E派伙伴\n旅程只记录伙伴收获，不计手动收获");
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
    if(Objectives.Claimed>=5)return TEXT("F5 保存 | F9 读取存档");
    if(GetObjectiveProgress(Objectives.Claimed)>=StarterObjectiveTargets[Objectives.Claimed])return TEXT("已完成，按N领取奖励并揭晓下个目标");
    return Objectives.Claimed==3?FarmingHint(this):FString(StarterObjectiveHints[Objectives.Claimed]);
}
FString ACFPlayerCharacter::GetObjectiveRewardText() const
{
    const TCHAR* Texts[]={TEXT("奖励：2个捕捉器"),TEXT("奖励：12份材料"),TEXT("奖励：12份材料"),TEXT("奖励：2份药品"),TEXT("奖励：20份材料和3个捕捉器")};
    return Objectives.Claimed>=5?TEXT("本存档已领取全部五项奖励"):Texts[Objectives.Claimed];
}
