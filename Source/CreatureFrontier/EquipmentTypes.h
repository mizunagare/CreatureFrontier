#pragma once
#include "CoreMinimal.h"
#include "EquipmentTypes.generated.h"

USTRUCT()
struct FPlayerEquipmentState
{
    GENERATED_BODY()
    UPROPERTY() bool bOwnSpear=false;
    UPROPERTY() bool bOwnVest=false;
    UPROPERTY() bool bSpearEquipped=false;
    UPROPERTY() bool bVestEquipped=false;
};
