#pragma once
#include "CoreMinimal.h"
class UWorld;
class ACFPlayerCharacter;
// 复用楼梯组合，检查二层设施生产与原工作任务恢复。
void RunUpperFarmScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read);
