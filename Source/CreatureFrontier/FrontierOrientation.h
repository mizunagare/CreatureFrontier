#pragma once
#include "CoreMinimal.h"

namespace FrontierOrientation
{
// 与既有资源线索一致：世界X正向为东，Y正向为北；不把方向提示当成可通行路径。
inline int32 Sector(double Degrees)
{return (FMath::RoundToInt(FMath::UnwindDegrees(Degrees)/45.)+8)%8;}
inline const TCHAR* Heading(double Degrees)
{
    const TCHAR* Names[]={TEXT("东"),TEXT("东北"),TEXT("北"),TEXT("西北"),TEXT("西"),TEXT("西南"),TEXT("南"),TEXT("东南")};
    return FMath::IsFinite(Degrees)?Names[Sector(Degrees)]:TEXT("未知");
}
inline FString HomeHint(FVector Position,double ViewYaw,FVector Home)
{
    if(Position.ContainsNaN() || Home.ContainsNaN() || !FMath::IsFinite(ViewYaw))return TEXT("基地中心：方位暂不可用");
    const auto Delta=Home-Position;const int32 Metres=FMath::CeilToInt(Delta.Size2D()/100);
    // 高差单独说明，避免在楼上／洞内误把水平接近当作已经到家。
    if(Delta.Size2D()<=800 && FMath::Abs(Delta.Z)<=400)return FString::Printf(TEXT("基地中心：附近%d米（水平）"),Metres);
    if(Delta.Size2D()<100 && FMath::Abs(Delta.Z)>400)return FString::Printf(TEXT("基地中心：%s%d米（直线）"),Delta.Z>0?TEXT("上方"):TEXT("下方"),FMath::CeilToInt(FMath::Abs(Delta.Z)/100));
    const TCHAR* Relative[]={TEXT("前方"),TEXT("右前"),TEXT("右侧"),TEXT("右后"),TEXT("后方"),TEXT("左后"),TEXT("左侧"),TEXT("左前")};
    const double Bearing=FMath::RadiansToDegrees(FMath::Atan2(Delta.Y,Delta.X));
    return FString::Printf(TEXT("基地中心：%s%d米（直线）"),Relative[Sector(Bearing-ViewYaw)],FMath::CeilToInt(Delta.Size()/100));
}
}
