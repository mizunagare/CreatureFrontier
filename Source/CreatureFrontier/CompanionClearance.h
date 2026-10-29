#pragma once
#include "CoreMinimal.h"

class ACreatureCharacter;
struct FCollisionQueryParams;
struct FHitResult;

namespace FrontierFollowPath
{
// 短距让行的真实胶囊净空；仅在跨阶上限内支持先水平、后下降的落脚检查。
bool ClearYieldSegment(const ACreatureCharacter* Self,const FVector& Start,const FVector& End,const FCollisionQueryParams& Query,FHitResult& Hit,bool& StepDown);
}
