#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "WorkStation.h"
#include "InventoryComponent.h"
#include "FarmCropComponent.h"
#include "BaseBuildingComponent.h"
#include "FrontierOrientation.h"
#include "EngineUtils.h"
#include "PlayerEquipmentComponent.h"
#include "WildEncounter.h"
#include "CraftingCatalog.h"
#include "HomeModule.h"
#include "HomeModuleDefinition.h"

namespace
{
    FString FarmingHint(const ACFPlayerCharacter* Hero);
    FString BuiltHomeLifeHint(const ACFPlayerCharacter* Hero)
    {
        bool Ready=false;
        for(TActorIterator<AHomeModule> Floor(Hero->GetWorld());Floor;++Floor)if(!Floor->bPreview && !Floor->bStaged && !Floor->bUnderConstruction && Floor->GetKind()==EHomeModuleKind::Floor)
        {
            int32 Sides=0;bool Door=false,Roof=false,Box=false;
            for(TActorIterator<AHomeModule> It(Hero->GetWorld());It;++It)if(!It->bPreview && !It->bStaged && !It->bUnderConstruction && It->FloorId==Floor->PersistentId)
            {const auto Kind=It->GetKind();Sides+=Kind==EHomeModuleKind::Wall || Kind==EHomeModuleKind::Window || Kind==EHomeModuleKind::Door || Kind==EHomeModuleKind::Doorway;Door|=Kind==EHomeModuleKind::Door;Roof|=Kind==EHomeModuleKind::Roof;Box|=Kind==EHomeModuleKind::Storage;}
            Ready|=Sides>=4 && Door && Roof && Box;
        }
        if(!Ready)return FString();
        bool Hungry=false,Tired=false;
        for(TActorIterator<ACreatureCharacter> It(Hero->GetWorld());It;++It)if(It->Tamer==Hero->GetController() && It->Health>0)
        {Hungry|=It->Nutrition<=80;Tired|=It->Stamina<80;}
        if(Hungry && Hero->Inventory->Count(EFrontierItem::Food)>0)return TEXT("建房后照料伙伴：瞄准J喂食（消耗1食物）\n开门留出通路，设施附近T休息");
        if(Tired)return TEXT("伙伴体力不足：开门留出通路\n设施附近T休息，恢复后F一起返屋");
        if(Hero->Inventory->Count(EFrontierItem::Seeds)==0)
            for(TActorIterator<AWorkStation> It(Hero->GetWorld());It;++It)if(It->bPlayerBuilt && It->RequiredSpecialty==EWorkSpecialty::Farming && !It->Crop->bPlanted && It->Crop->Seeds==0 && It->StoredFood==0)
            {const auto* Catalog=UCraftingCatalog::Load();const auto* Recipe=Catalog?Catalog->Find(TEXT("PlantingSeeds")):nullptr;if(Recipe)return FString::Printf(TEXT("补种：基地C选择准备种子（产%d份）\n需要%s，M采集后返家制作"),Recipe->OutputAmount,*UCraftingCatalog::FormatItems(Recipe->Inputs));}
        // 初始食物消耗后接回原农圃，不把所有建成家园都永久提示继续施工或探索。
        if(Hero->Inventory->Count(EFrontierItem::Food)<=1)
        {
            for(TActorIterator<AWorkStation> It(Hero->GetWorld());It;++It)if(It->bPlayerBuilt && It->RequiredSpecialty==EWorkSpecialty::Farming && It->StoredFood>0)
                return TEXT("农圃已有收获：靠近6米瞄准按O取粮\n补回伙伴照料与下一次探索的食物");
            return FarmingHint(Hero);
        }
        return TEXT("家园已可进出：E开关门，勿挡活动区\n照料伙伴后继续探索；F5保存");
    }
    // 恢复提示只读现有状态，不改变治疗许可、任务或奖励进度。
    FString RecoveryHint(const ACFPlayerCharacter* Hero)
    {
        if(Hero->Health<=0)return FString();
        bool PartnerHurt=false,PartnerResting=false;
        for(TActorIterator<ACreatureCharacter> It(Hero->GetWorld());It;++It)
            if(It->Tamer==Hero->GetController() && It->Health>0)
            {
                PartnerHurt|=It->Health<It->GetMaxHealth();
                PartnerResting|=It->Activity==ECreatureActivity::Rest && (It->Health<It->GetMaxHealth() || It->Stamina<99);
            }
        if(Hero->Health<100)
        {
            if(Hero->Inventory->Count(EFrontierItem::Medicine)>0)
                return TEXT("受伤先恢复：H治疗自己（消耗1药）\n伙伴可瞄准P用药，或设施附近T休息");
            const auto* Catalog=UCraftingCatalog::Load();const auto* Recipe=Catalog?Catalog->Find(TEXT("Medicine")):nullptr;
            if(Recipe)return FString::Printf(TEXT("先恢复：设施附近T让伙伴休息\n缺药：M采料，设施附近2制药（%d材料）"),Recipe->Inputs.FindRef(EFrontierItem::Material));
            return TEXT("先恢复：设施附近T让伙伴休息\n缺药：C查看配方，采料后返家制药");
        }
        if(PartnerResting)return TEXT("伙伴在设施休息中，恢复生命和体力\n等恢复后F跟随，再准备下一次探索");
        if(PartnerHurt)return TEXT("伙伴受伤：设施附近T休息恢复\n有药可瞄准P治疗，恢复后F跟随");
        return FString();
    }
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
    if(Objectives.Claimed>=5)
    {
        // 领奖结束不等于生活循环结束；复用原恢复许可和真实模块成本。
        const FString Recovery=RecoveryHint(this);if(!Recovery.IsEmpty())return Recovery;
        const FString HomeLife=BuiltHomeLifeHint(this);if(!HomeLife.IsEmpty())return HomeLife;
        bool Floor=false,HomeStorage=false;
        for(TActorIterator<AHomeModule> It(GetWorld());It;++It)if(!It->bPreview && !It->bStaged && !It->bUnderConstruction)
        {Floor|=It->GetKind()==EHomeModuleKind::Floor;HomeStorage|=It->GetKind()==EHomeModuleKind::Storage;}
        const auto* Definition=UHomeModuleDefinition::LoadDefault(Floor?EHomeModuleKind::Storage:EHomeModuleKind::Floor);
        if(!Definition)return TEXT("F5 保存 | F9 读取存档");
        if(!Floor)return FString::Printf(TEXT("B后按8切房屋，V选择木地板\n需%d材料；左键建造，右键取消"),Definition->MaterialCost);
        if(!HomeStorage)return FString::Printf(TEXT("在地板上放储物箱（%d材料）\nB后按8切房屋，V选择，左键建造"),Definition->MaterialCost);
        return TEXT("右Shift+左键放施工架\nE开箱存料；V选箱，瞄工地E派工");
    }
    if(GetObjectiveProgress(Objectives.Claimed)>=StarterObjectiveTargets[Objectives.Claimed])return TEXT("已完成，按N领取奖励并揭晓下个目标");
    if(Objectives.Claimed==4)
    {
        const FString Recovery=RecoveryHint(this);if(!Recovery.IsEmpty())return Recovery;
        // 仅提供准备建议，不把装备变成领奖或战斗的新许可条件。
        const auto& Gear=Equipment->GetState();
        if(!Gear.bOwnSpear)return FString::Printf(TEXT("先备武器：设施附近按4制长矛（%d材料）\n缺料可M采集，或在采集台E派伙伴"),Equipment->GetSpearCost());
        if(!Gear.bSpearEquipped)return TEXT("按6装备已有长矛，无需重复制作\n护甲、药品和食物也需准备");
        if(!Gear.bOwnVest)return FString::Printf(TEXT("先备护甲：设施附近按5制作（%d材料）\n缺料可M采集，或在采集台E派伙伴"),Equipment->GetVestCost());
        if(!Gear.bVestEquipped)return TEXT("按7装备已有护甲，无需重复制作\n装备只减伤，不保证能战胜首领");
        for(TActorIterator<AWildEncounter> It(GetWorld());It;++It)if(It->bBoss && It->EncounterId==TEXT("CrystalRise"))
        {
            const auto Delta=It->GetActorLocation()-GetActorLocation();
            return FString::Printf(TEXT("水晶高地在%s%d米（直线）\n危险首领，先备补给；Q集火/G技能"),FrontierOrientation::Heading(FMath::RadiansToDegrees(FMath::Atan2(Delta.Y,Delta.X))),FMath::CeilToInt(Delta.Size()/100));
        }
        return TEXT("危险首领，先备药品和食物再探索\n观察水晶高地区域；Q集火/G技能");
    }
    return Objectives.Claimed==3?FarmingHint(this):FString(StarterObjectiveHints[Objectives.Claimed]);
}
FString ACFPlayerCharacter::GetObjectiveRewardText() const
{
    const TCHAR* Texts[]={TEXT("奖励：2个捕捉器"),TEXT("奖励：12份材料"),TEXT("奖励：12份材料"),TEXT("奖励：2份药品"),TEXT("奖励：20份材料和3个捕捉器")};
    return Objectives.Claimed>=5?TEXT("本存档已领取全部五项奖励"):Texts[Objectives.Claimed];
}
