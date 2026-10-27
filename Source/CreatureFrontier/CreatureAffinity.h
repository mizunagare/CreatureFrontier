#pragma once
#include "CoreMinimal.h"
class UCreatureDefinition;
namespace CreatureAffinity
{
    const TCHAR* FamilyName(const UCreatureDefinition* Definition);
    float Multiplier(const UCreatureDefinition* Attacker,const UCreatureDefinition* Defender);
    FString Describe(const UCreatureDefinition* Definition);
}
