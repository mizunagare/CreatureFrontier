#pragma once
#include "CoreMinimal.h"
#include "ObjectiveTypes.generated.h"

USTRUCT()
struct FFrontierObjectiveProgress
{
    GENERATED_BODY()
    UPROPERTY() int32 Claimed = 0;
    UPROPERTY() int32 WorkMaterials = 0;
    UPROPERTY() int32 FarmFood = 0;
    UPROPERTY() bool bAlphaResolved = false;
};
