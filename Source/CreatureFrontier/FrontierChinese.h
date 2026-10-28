#pragma once

#include "CreatureTypes.h"

// 仅用于显示，不改变枚举名或存档中的数值。
namespace FrontierChinese
{
inline FString Activity(ECreatureActivity Value)
{
    switch(Value)
    {
    case ECreatureActivity::Idle:return TEXT("待命");
    case ECreatureActivity::Follow:return TEXT("跟随");
    case ECreatureActivity::Combat:return TEXT("战斗");
    case ECreatureActivity::Work:return TEXT("工作");
    case ECreatureActivity::Rest:return TEXT("休息");
    case ECreatureActivity::Wander:return TEXT("巡游");
    case ECreatureActivity::ReturnHome:return TEXT("返回栖息地");
    default:return TEXT("未知状态");
    }
}
inline FString Specialty(EWorkSpecialty Value)
{
    switch(Value)
    {
    case EWorkSpecialty::None:return TEXT("无专长");
    case EWorkSpecialty::Gathering:return TEXT("采集");
    case EWorkSpecialty::Mining:return TEXT("采矿");
    case EWorkSpecialty::Farming:return TEXT("耕作");
    case EWorkSpecialty::Crafting:return TEXT("制作");
    case EWorkSpecialty::Transport:return TEXT("运输");
    case EWorkSpecialty::Feeding:return TEXT("喂食");
    default:return TEXT("未知专长");
    }
}
}
