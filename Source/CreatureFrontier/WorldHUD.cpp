#include "CFHUD.h"
#include "FrontierChinese.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "CompanionPersonalityComponent.h"
#include "CompanionMemoryComponent.h"
#include "CompanionCargoComponent.h"
#include "CreatureAffinity.h"
#include "PlayerEquipmentComponent.h"
#include "PlayerPaceComponent.h"
#include "CaptureComponent.h"
#include "InventoryComponent.h"
#include "BaseBuildingComponent.h"
#include "WildEncounter.h"
#include "WorkStation.h"
#include "ResourceNode.h"
#include "ResearchComponent.h"
#include "StorageInteractionComponent.h"
#include "HomeModule.h"
#include "FarmCropComponent.h"
#include "Engine/Canvas.h"
#include "EngineUtils.h"

void ACFHUD::DrawWorldHUD(ACFPlayerCharacter* Player)
{
    const auto Green=FLinearColor(.5f,.95f,.65f),Blue=FLinearColor(.65f,.85f,1),Yellow=FLinearColor(1,.85f,.35f);
    int32 Companions=0,Working=0;for(TActorIterator<ACreatureCharacter> It(GetWorld());It;++It)if(It->Tamer==Player->GetController()){++Companions;if(It->Activity==ECreatureActivity::Work)++Working;}
    DrawHUDPanel(Layout.Vitals,{
        {FString::Printf(TEXT("生命 %.0f/100 | 材料 %d"),Player->Health,Player->Inventory->Count(EFrontierItem::Material))},
        {FString::Printf(TEXT("捕捉器 %d | 药品 %d | 食物 %d"),Player->Inventory->Count(EFrontierItem::CaptureDevice),Player->Inventory->Count(EFrontierItem::Medicine),Player->Inventory->Count(EFrontierItem::Food))},
        {FString::Printf(TEXT("伙伴 %d | 工作中 %d"),Companions,Working),Green},
        {(Player->Pace->IsSlowWalking()?FString(TEXT("慢行 | ")):FString())+Player->GetCommandScopeText(),Green,2},
        {FString::Printf(TEXT("%s | 攻击 %.0f | 距离 %.1f米 | 护甲 %.0f%%"),Player->Equipment->GetState().bSpearEquipped?TEXT("长矛"):TEXT("徒手"),Player->GetPlayerAttackDamage(),Player->GetPlayerMeleeRange()/100,Player->Equipment->GetDamageReduction()*100)},
        {Player->Health<=0?FString(TEXT("已倒下：R 基地复活 | F9 读档")):FString::Printf(TEXT("右键 %s | 共鸣器 %d | 0 切换"),Player->CaptureComponent->UsesResonance()?TEXT("共鸣捕捉"):TEXT("普通捕捉"),Player->Inventory->Count(EFrontierItem::ResonanceDevice)),Yellow},
        {TEXT("F1 帮助 | C 背包 | Tab 队伍 | I 图鉴 | Esc 暂停")}});
    TArray<FFrontierHUDRow> Feedback={{Player->Feedback,Yellow,Player->HasDeliverySelection()?2:3}};
    if(Player->HasDeliverySelection())Feedback.Add({TEXT("已选运输箱 | E 指派资源 | 对空按 V 清除"),Green,1});
    DrawHUDPanel(Layout.Feedback,Feedback);
    auto* Creature=Player->TraceCreature();FString Alert;
    if(Player->Health<=0)Alert=TEXT("已倒下\nR 基地复活 | F9 读取存档");
    else if(Creature && Creature->bAlpha && Creature->Disposition!=ECreatureDisposition::Tamed && Creature->AlphaWindup>0)Alert=FString::Printf(TEXT("首领震地即将释放 %.1f秒\n请闪避或寻找掩体！"),Creature->AlphaWindup);
    else if(Player->IsMeleePending())Alert=TEXT("长矛蓄力中\n保持瞄准并靠近目标");
    else if(Player->GetRespawnProtectionRemaining()>0)Alert=FString::Printf(TEXT("复活保护 %.1f秒\n主动攻击将结束保护"),Player->GetRespawnProtectionRemaining());
    else if(Creature && Creature->Activity==ECreatureActivity::ReturnHome)Alert=TEXT("正在返回栖息地\n暂不可造成伤害或捕捉");
    if(!Alert.IsEmpty()){Audit.bAlert=true;DrawHUDPanel(Layout.Alert,{{Alert,FLinearColor(1,.5f,.3f),4}});}
    if(Audit.bInterface)return;
    const float X=Canvas->ClipX*.5f,Y=Canvas->ClipY*.5f;DrawLine(X-6,Y,X+6,Y,FLinearColor::White);DrawLine(X,Y-6,X,Y+6,FLinearColor::White);
    DrawHUDPanel(Layout.Objective,{{Player->GetObjectiveStatus(),Green,2},{Player->GetObjectiveHint(),FLinearColor::White,2},{Player->GetObjectiveRewardText(),Yellow,2}});
    TArray<FFrontierHUDRow> Research={{Player->Research->GetHint(),Blue,2}};
    AWildEncounter* Nearest=nullptr;float Distance=1800;
    for(TActorIterator<AWildEncounter> It(GetWorld());It;++It)if(Player->GetDistanceTo(*It)<Distance){Distance=Player->GetDistanceTo(*It);Nearest=*It;}
    if(Nearest)Research.Add({Nearest->GetStatus(),Nearest->bBoss?Yellow:Green,2});DrawHUDPanel(Layout.Research,Research);
    TArray<FFrontierHUDRow> Rows;
    if(Player->Building->IsActive()){Audit.Context=EFrontierHUDContext::Building;Rows.Add({TEXT("建造模式"),Yellow});Rows.Add({Player->Building->GetHint(),Yellow,4});}
    else if(Player->Health<=0 && !Creature)Rows.Add({TEXT("倒下时无法交互，请先复活或读档"),Yellow,2});
    else if(Creature)
    {
        const bool Tamed=Creature->Disposition==ECreatureDisposition::Tamed;Audit.Context=Tamed?EFrontierHUDContext::Companion:EFrontierHUDContext::Wild;
        const FString Identity=Creature->Nickname.IsEmpty()?Creature->GetCreatureName():Creature->GetCreatureName()+TEXT(" / ")+Creature->GetSpeciesName();
        Rows.Add({Identity,Blue,2});Rows.Add({FString::Printf(TEXT("%s 等级%d | 生命 %.0f/%.0f"),Tamed?TEXT("伙伴"):TEXT("野生"),Creature->Level,Creature->Health,Creature->GetMaxHealth())});
        if(Player->Health<=0)Rows.Add({TEXT("倒下时无法交互，请先复活或读档"),Yellow,2});
        else if(Tamed && Creature->Health<=0)
        {
            Rows.Add({TEXT("伙伴已倒下，无法自行返家或执行指令"),Yellow,2});
            Rows.Add({Player->Inventory->Count(EFrontierItem::Medicine)>0?
                TEXT("P 使用1份药品救助，恢复40生命"):
                TEXT("缺少药品：回基地按2制作，再返回救助"),Yellow,2});
            Rows.Add({TEXT("救起后可按F跟随；R复活不会治疗伙伴"),Blue,2});
            if(Creature->CarriedFood>0 || Creature->Cargo->Count()>0)
                Rows.Add({FString::Printf(TEXT("携带食物 %d | 资源 %d | Z 取回（6米）"),Creature->CarriedFood,Creature->Cargo->Count()),Yellow});
        }
        else if(Tamed)
        {
            const FString Activity=FrontierChinese::Activity(Creature->Activity);
            Rows.Add({FString::Printf(TEXT("%s | 体力 %.0f | 饱食 %.0f | 经验 %d/%d"),*Activity,Creature->Stamina,Creature->Nutrition,Creature->Experience,Creature->Level*100),Green,2});
            Rows.Add({FString::Printf(TEXT("%s | %s %.1f秒"),*Creature->GetSpecialName(),Creature->SpecialWindupRemaining>0?TEXT("蓄力"):TEXT("冷却"),Creature->SpecialWindupRemaining>0?Creature->SpecialWindupRemaining:Creature->SpecialCooldownRemaining),Yellow,2});
            FString QuietReason,PlayReason;const bool Quiet=Player->CanInteractCompanion(Creature,false,QuietReason),Play=Player->CanInteractCompanion(Creature,true,PlayReason);
            Rows.Add({Quiet?(Play?FString(TEXT("Y 安静陪伴 / F6 玩耍（安全距离2.5米）")):FString(TEXT("Y 安静陪伴 | F6 暂不可用："))+PlayReason):FString(TEXT("陪伴暂不可用："))+QuietReason,Yellow,2});
            Rows.Add({TEXT("J 喂食 | P 治疗 | F 跟随 | T 休息"),Yellow});
            Rows.Add({TEXT("F4 命名 / F7 归家回忆（安全空闲时）"),Yellow});
            const auto Job=Creature->GetGatheringStatus();if(!Job.IsEmpty())Rows.Add({Job,Yellow,2});
            if(Creature->FeedingSite.IsValid())Rows.Add({TEXT("寻找食物中，保留工作指派"),Yellow});
            else if(Creature->bResumeWorkAfterRest)Rows.Add({TEXT("自动休息：体力80 / 饱食20 / 生命80%后复工"),Yellow,2});
            if(Creature->CarriedFood>0 || Creature->Cargo->Count()>0)Rows.Add({FString::Printf(TEXT("携带食物 %d | 资源 %d | Z 取回（6米）"),Creature->CarriedFood,Creature->Cargo->Count()),Yellow});
            Rows.Add({FString::Printf(TEXT("亲密 %d/100 | 照料冷却 %.0f秒"),Creature->BondPoints,FMath::CeilToFloat(Creature->PettingCooldown)),Green});
            Rows.Add({Creature->Personality->GetSummary(),Blue});Rows.Add({Creature->Memory->GetSummary(),Blue,2});
        }
        else
        {
            if(Creature->Definition && Creature->Definition->PeacefulFoodCost>0 && !Creature->bAlpha)
                Rows.Add({FString::Printf(TEXT("J 和平结交：分享%d食物 | 健康、平静、安全，2.5米内"),Creature->Definition->PeacefulFoodCost),Green,2});
            Rows.Add({FString::Printf(TEXT("捕捉概率 %.0f%% | 右键捕捉 | Q 集火 | G 技能"),Player->CaptureComponent->CalculateCaptureChance(Creature)*100),Yellow,2});
            if(Creature->Activity==ECreatureActivity::ReturnHome)Rows.Add({TEXT("返回栖息地中：暂不可伤害或捕捉"),Yellow,2});
            if(Creature->bAlpha)Rows.Add({FString::Printf(TEXT("首领震地 | %s %.1f秒"),Creature->AlphaWindup>0?TEXT("即将释放"):TEXT("冷却"),Creature->AlphaWindup>0?Creature->AlphaWindup:Creature->AlphaCooldown),Yellow,2});
        }
        if(Creature->SlowRemaining>0 || Creature->GuardRemaining>0)Rows.Add({FString::Printf(TEXT("减速 %.1f秒 | 守护 %.1f秒"),Creature->SlowRemaining,Creature->GuardRemaining),Blue});
        Rows.Add({Tamed?CreatureAffinity::Describe(Creature->Definition):Player->GetGroupMatchupText(Creature),Blue,2});
    }
    else if(auto* Node=Player->TraceResource()){Audit.Context=EFrontierHUDContext::Resource;Rows.Add({TEXT("资源"),Blue});Rows.Add({Node->GetStatus(),Yellow,4});}
    else if(Player->Storage->TraceCrate()){Audit.Context=EFrontierHUDContext::Storage;Rows.Add({TEXT("仓储"),Blue});Rows.Add({TEXT("E 打开（3米）| V 设为运输目标 | X 拆除前需清空"),Yellow,3});}
    else if(auto* Site=Player->TraceFacility())
    {
        Audit.Context=Site->RequiredSpecialty==EWorkSpecialty::Farming?EFrontierHUDContext::Garden:Site->IsFoodStore()?EFrontierHUDContext::FoodStore:EFrontierHUDContext::Facility;
        Rows.Add({Site->RequiredSpecialty==EWorkSpecialty::Farming?TEXT("农圃"):TEXT("工作设施"),Blue});
        if(Site->IsFoodStore())Rows.Add({FString::Printf(TEXT("食物 %d/30 | U 存入 | O 取出（6米）"),Site->StoredFood),Yellow,2});
        if(Site->RequiredSpecialty==EWorkSpecialty::Farming)Rows.Add({Site->Crop->Status(),Green,3});
        Rows.Add({TEXT("E 指派工作 | T 休息 | F 跟随/取消"),Yellow,2});
    }
    if(!Rows.IsEmpty())
    {
        const float Height=FMath::Min(Layout.Context.GetSize().Y,MeasureHUDRows(Rows,Layout.Context.GetSize().X-2*Layout.Padding));
        auto Rect=Layout.Context;Rect.Min.Y=Rect.Max.Y-Height;DrawHUDPanel(Rect,Rows);
    }
}
