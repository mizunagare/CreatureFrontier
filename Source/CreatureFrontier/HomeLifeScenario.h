#pragma once
#include "CoreMinimal.h"
class UWorld;
class ACFPlayerCharacter;

// 延续原家园工作链，验证实际进屋取粮、照料、归队与生活状态存读。
void RunHomeLifeScenario(UWorld* World,ACFPlayerCharacter* Hero,FGuid PetId,FGuid FarmId,TFunction<void(bool)> Complete);
