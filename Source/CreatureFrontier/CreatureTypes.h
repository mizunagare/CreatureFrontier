#pragma once

#include "CoreMinimal.h"
#include "CreatureTypes.generated.h"

UENUM(BlueprintType)
enum class ECreatureDisposition : uint8
{
    Wild,
    Friendly,
    Hostile,
    Tamed
};

UENUM(BlueprintType)
enum class ECreatureActivity : uint8
{
    Idle,
    Follow,
    Combat,
    Work,
    Rest,
    Wander,
    ReturnHome
};

UENUM(BlueprintType)
enum class ECreatureForm : uint8
{
    Leaf,
    Ember,
    Stone
};

UENUM(BlueprintType)
enum class EWorkSpecialty : uint8
{
    None,
    Gathering,
    Mining,
    Farming,
    Crafting,
    Transport
};

