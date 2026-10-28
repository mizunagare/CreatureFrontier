#include "CFHUD.h"
#include "CFPlayerCharacter.h"
#include "PlayerEquipmentComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "CanvasTypes.h"

void ACFHUD::DrawWrappedText(const FString& Text,FLinearColor Color,float X,float Y,float Width,int32 MaxLines)
{
    // 使用引擎的字符断行，中文没有空格也能正确换行。
    FTextSizingParameters Params(0,0,Width,0,GEngine->GetSmallFont());
    TArray<FWrappedStringElement> Lines;
    Canvas->WrapString(Params,0,FStringView(Text),Lines);
    for(int32 Row=0;Row<FMath::Min(MaxLines,Lines.Num());++Row)
        DrawText(Lines[Row].Value,Color,X,Y+Row*20);
}

void ACFHUD::DrawPauseMenu(ACFPlayerCharacter* Player)
{
    const float Width=FMath::Min(640.f,Canvas->ClipX-48),Left=(Canvas->ClipX-Width)/2,Top=(Canvas->ClipY-460)/2;
    DrawRect(FLinearColor(.005f,.012f,.02f,.88f),0,0,Canvas->ClipX,Canvas->ClipY);
    DrawRect(FLinearColor(.025f,.055f,.08f,.98f),Left,Top,Width,460);
    DrawRect(FLinearColor(.08f,.26f,.32f,1),Left+14,Top+112+Player->GetPauseSelection()*40,Width-28,34);
    DrawText(TEXT("已暂停 · 幻兽边境"),FLinearColor::White,Left+24,Top+24,nullptr,1.2f);
    DrawText(TEXT("世界模拟已暂停"),FLinearColor::Green,Left+24,Top+54);
    DrawText(TEXT("F5 保存 | F9 确认读档"),FLinearColor::White,Left+24,Top+80);
    const TCHAR* Options[]={TEXT("继续游戏"),TEXT("保存当前进度"),TEXT("读取存档进度"),TEXT("在基地复活（仅倒下时）"),TEXT("退出到桌面（不会自动保存）")};
    for(int32 Index=0;Index<5;++Index)
        DrawText(FString(Index==Player->GetPauseSelection()?TEXT(">> "):TEXT("   "))+Options[Index],Index==Player->GetPauseSelection()?FLinearColor::Yellow:FLinearColor::White,Left+24,Top+120+Index*40);
    DrawWrappedText(Player->Feedback,Player->IsPauseConfirmationPending()?FLinearColor::Yellow:FLinearColor::Green,Left+24,Top+332,Width-48,3);
    DrawText(TEXT("上下方向键选择 | Enter 确认 | Esc 继续/取消"),FLinearColor::White,Left+24,Top+414);
    DrawText(TEXT("退出不会自动保存，离开前可按 F5 保存。"),FLinearColor::White,Left+24,Top+436);
}

void ACFHUD::DrawControlHelp(ACFPlayerCharacter* Player)
{
    const TArray<FString> Lines={
        TEXT("WASD 移动 | 左Shift 慢行 | 鼠标转向/空格跳跃"),
        TEXT("M 采集资源（3米）| 玩家与伙伴共享有限储量"),
        TEXT("左键近战 | 右键捕捉/取消长矛蓄力"),
        TEXT("0 切换普通/共鸣捕捉 | 对水晶按 C 研究"),
        TEXT("伙伴默认防御 | Q 主动集火 | G 技能"),
        TEXT("L 选择单个伙伴 | K 指挥全队"),
        TEXT("F 跟随/取消 | E 指派资源或附近设施（8米）"),
        TEXT("E 开关木门/打开箱子（3米）| 箱内方向键操作"),
        TEXT("瞄准箱子按 V 设运输目标 | 对空按 V 清除"),
        TEXT("G 播种/照料/收获（3米）| F2/F3 存取种子"),
        TEXT("F4 给自己的空闲伙伴命名（2.5米，输入时暂停）"),
        TEXT("F7 归家回忆（水晶/脱险）：安全空闲，2.5米内"),
        TEXT("T 休息 | P 治疗瞄准的伙伴"),
        TEXT("J 喂食伙伴/向温顺野生生物分享食物 | H 自疗"),
        TEXT("Y 安静陪伴 / F6 玩耍（安全2.5米，玩耍耗体力/饱食20）"),
        TEXT("B 建造 | V 切换设施 | X 拆除"),
        TEXT("建造时：8 切换设施/房屋 | 9 旋转墙体"),
        TEXT("C 背包/配方 | 1 制作捕捉器（3材料）"),
        TEXT("2 制作药品（5材料）| 3 制作食物（2材料）"),
        FString::Printf(TEXT("4 制作长矛（%d）| 5 制作护甲（%d）"),Player->Equipment->GetSpearCost(),Player->Equipment->GetVestCost()),
        TEXT("6 / 7 装备或卸下长矛/护甲"),
        TEXT("U 存入食物 | O 从农圃/食槽取出"),
        TEXT("Z 取回瞄准伙伴携带的物品"),
        TEXT("Tab 队伍 | [ / ] 翻页 | Enter 选择"),
        TEXT("I 图鉴 | PageUp / PageDown 翻页"),
        TEXT("N 领取已完成旅程的奖励"),
        TEXT("倒下时按 R 回基地复活，保留世界"),
        TEXT("F5 保存 | F9 确认读档 | Esc 暂停"),
        TEXT("F1 关闭帮助，此面板不会暂停世界")};
    const float Left=Canvas->ClipX-488,Step=FMath::Clamp((Canvas->ClipY-130)/Lines.Num(),15.f,24.f);
    DrawRect(FLinearColor(.015f,.025f,.04f,.96f),Left,18,470,86+Step*Lines.Num());
    DrawText(TEXT("操作帮助 | F1 关闭"),FLinearColor::White,Left+14,30);
    DrawText(TEXT("世界仍在运行，按 Esc 打开暂停菜单"),FLinearColor::Yellow,Left+14,56);
    for(int32 Index=0;Index<Lines.Num();++Index) DrawText(Lines[Index],FLinearColor::White,Left+14,90+Index*Step);
}
