#include "CFHUD.h"
#include "FrontierChinese.h"
#include "CFPlayerCharacter.h"
#include "CreatureDefinition.h"
#include "CreatureAffinity.h"
#include "Engine/Canvas.h"

void ACFHUD::DrawJournalPanel(ACFPlayerCharacter* Player)
{
    const auto& Entries=Player->GetJournal();int32 Captured=0;for(const auto& Entry:Entries)if(Entry.bCaptured)++Captured;
    const float Left=Canvas->ClipX-488;
    DrawRect(FLinearColor(.015f,.025f,.04f,.96f),Left,18,470,440);
    DrawText(FString::Printf(TEXT("野外图鉴 | 已发现%d种 | 已结交%d种"),Entries.Num(),Captured),FLinearColor::Green,Left+14,30);
    DrawText(TEXT("I 关闭 | PageUp / PageDown 翻页 | 不暂停"),FLinearColor::Yellow,Left+14,54);
    if(Entries.IsEmpty()){DrawText(TEXT("瞄准12米内的活体生物即可发现"),FLinearColor::White,Left+14,104);return;}
    const int32 Index=Player->GetJournalPage()%Entries.Num();const auto& Entry=Entries[Index];const auto* Definition=Entry.Definition.Get();if(!Definition)return;
    DrawText(FString::Printf(TEXT("%d / %d  %s"),Index+1,Entries.Num(),*Definition->DisplayName.ToString()),FLinearColor::White,Left+14,100);
    DrawText(Entry.bCaptured?TEXT("已结交，研究记录永久保留"):TEXT("已发现，尚未结交"),Entry.bCaptured?FLinearColor::Green:FLinearColor::Yellow,Left+14,126);
    if(Entry.bCaptured)
    {
        DrawText(FString::Printf(TEXT("基础生命%d | 攻击%d | 速度%.1f米/秒"),Definition->BaseMaxHealth,Definition->BaseAttack,Definition->MoveSpeed/100),FLinearColor::White,Left+14,166);
        FString Jobs;for(auto Job:Definition->WorkSpecialties){const FString Name=FrontierChinese::Specialty(Job);Jobs+=(Jobs.IsEmpty()?TEXT(""):TEXT(" / "))+Name;}
        DrawText(TEXT("工作专长：")+Jobs,FLinearColor::White,Left+14,192);DrawText(TEXT("技能：")+Definition->SpecialName.ToString(),FLinearColor::White,Left+14,218);
        DrawText(FString::Printf(TEXT("范围%.1f米 | 冷却%.0f秒 | 消耗20体力"),Definition->SpecialRange/100,Definition->SpecialCooldownSeconds),FLinearColor::White,Left+14,244);
    }
    else DrawText(TEXT("结交或捕捉后解锁基础属性、专长和技能记录"),FLinearColor::White,Left+14,166);
    DrawText(CreatureAffinity::Describe(Definition),FLinearColor(.7f,.9f,1),Left+14,270);
    DrawWrappedText(Definition->Description.ToString(),FLinearColor::White,Left+14,290,430,6);
    DrawText(TEXT("F5 保存研究记录 | F9 恢复存档"),FLinearColor::Yellow,Left+14,426);
}
