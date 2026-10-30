#pragma once
#include "CoreMinimal.h"

class ACreatureCharacter;
class UNavigationPath;
struct FCollisionQueryParams;
struct FHitResult;

namespace FrontierFollowPath
{
// 已有跟随路径的人物通道检查，农业接近点也使用同一判据。
bool ClearPath(const ACreatureCharacter* Self,const UNavigationPath* Path);
// 短距让行的真实胶囊净空；仅在跨阶上限内支持先水平、后下降的落脚检查。
bool ClearYieldSegment(const ACreatureCharacter* Self,const FVector& Start,const FVector& End,const FCollisionQueryParams& Query,FHitResult& Hit,bool& StepDown);
}
