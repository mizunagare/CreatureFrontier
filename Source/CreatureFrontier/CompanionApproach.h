#pragma once
#include "CoreMinimal.h"
class ACreatureCharacter;
class AActor;
// 箱储运输和施工共用同层站位、视线、胶囊净空及完整路径规则。
bool FindCompanionApproach(ACreatureCharacter* Pet,AActor* Target,FVector& Out);
