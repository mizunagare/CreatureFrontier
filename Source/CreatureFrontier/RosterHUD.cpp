#include "CFHUD.h"
#include "FrontierChinese.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CompanionPersonalityComponent.h"
#include "CompanionMemoryComponent.h"
#include "WorkStation.h"
#include "Engine/Canvas.h"

void ACFHUD::DrawRosterPanel(ACFPlayerCharacter* Player)
{
    const auto Pets=Player->GetCompanionRoster();const int32 Highlight=Pets.IndexOfByKey(Player->GetRosterHighlight());
    const bool HasRescue=Pets.ContainsByPredicate([](const ACreatureCharacter* Pet){return !Pet->Memory->GetRescueSummary().IsEmpty();});
    const float U=Layout.Unit,RowHeight=(HasRescue?130:110)*U,Header=100*U;
    const int32 Rows=FMath::Clamp(FMath::FloorToInt((Canvas->ClipY-160*U)/RowHeight),1,8),Start=Highlight==INDEX_NONE?0:(Highlight/Rows)*Rows;
    const float Width=FMath::Min(470*U,Canvas->ClipX-2*Layout.Margin),Left=Canvas->ClipX-Width-Layout.Margin,Top=Layout.Margin;
    const auto Panel=FFrontierHUDLayout::Box(Left,Top,Width,Header+Rows*RowHeight);TrackPanel(Panel);
    DrawRect(FLinearColor(.015f,.025f,.04f,.97f),Left,Top,Width,Panel.GetSize().Y);
    if(Highlight!=INDEX_NONE)DrawRect(FLinearColor(.08f,.22f,.28f,.95f),Left+6*U,Top+85*U+(Highlight-Start)*RowHeight,Width-12*U,RowHeight-4*U);
    auto Line=[this,Left,Width,U](const FString& Text,FLinearColor Color,float Y)
    {DrawHUDText(Text,Color,FFrontierHUDLayout::Box(Left+12*U,Y,Width-24*U,Layout.Line),1);};
    Line(FString::Printf(TEXT("队伍：%d 位伙伴 | %d-%d"),Pets.Num(),Pets.IsEmpty()?0:Start+1,FMath::Min(Start+Rows,Pets.Num())),FLinearColor::White,Top+12*U);
    Line(TEXT("[ / ] 翻页 | 回车选择 | F4 命名 | Tab 关闭"),FLinearColor::Yellow,Top+34*U);
    Line(TEXT("世界仍在运行 | 选择距离12米，需可见"),FLinearColor::White,Top+56*U);
    if(Pets.IsEmpty())Line(TEXT("还没有伙伴，试着捕捉野生生物吧"),FLinearColor::Green,Top+92*U);
    for(int32 Index=Start;Index<FMath::Min(Start+Rows,Pets.Num());++Index)
    {
        ++Audit.RosterRows;const auto* Pet=Pets[Index];const float Y=Top+88*U+(Index-Start)*RowHeight;
        const auto Color=Pet->Health<=0?FLinearColor(1,.35f,.3f):FLinearColor::White;
        Line(FString::Printf(TEXT("%s %s%s"),Index==Highlight?TEXT(">>"):TEXT("  "),Player->IsCommandTarget(Pet)?TEXT("[指挥中] "):TEXT(""),*Pet->GetCreatureName()),Color,Y);
        Line(FString::Printf(TEXT("等级%d | %.1f米 | 生命 %.0f/%.0f | 体力 %.0f | 饱食 %.0f"),Pet->Level,Player->GetDistanceTo(Pet)/100,Pet->Health,Pet->GetMaxHealth(),Pet->Stamina,Pet->Nutrition),Color,Y+20*U);
        FString Status=FrontierChinese::Activity(Pet->Activity);
        Status+=Pet->bAvoidCombat?TEXT(" | 避战"):TEXT(" | 护主");
        if(Pet->Health<=0)Status=TEXT("已倒下：瞄准后按 P 治疗");else if(Pet->FeedingSite.IsValid())Status=TEXT("寻找食物");else if(Pet->bResumeWorkAfterRest)Status=TEXT("自动休息，等待复工");
        if(!Pet->Nickname.IsEmpty())Status=Pet->GetSpeciesName()+TEXT(" | ")+Status;
        if(Pet->WorkSite)Status+=TEXT(" | ")+FrontierChinese::Specialty(Pet->WorkSite->RequiredSpecialty);
        if(Pet->GetFieldResource())Status+=TEXT(" | 野外采集");if(Pet->CarriedFood>0)Status+=FString::Printf(TEXT(" | 携带食物 %d"),Pet->CarriedFood);
        Line(Status,FLinearColor::Green,Y+40*U);Line(Pet->Personality->GetSummary(),FLinearColor(.7f,.9f,1),Y+60*U);Line(Pet->Memory->GetSummary(),FLinearColor(.7f,.85f,1),Y+80*U);
        if(HasRescue)Line(Pet->Memory->GetRescueSummary(),FLinearColor(.7f,.85f,1),Y+100*U);
    }
}
