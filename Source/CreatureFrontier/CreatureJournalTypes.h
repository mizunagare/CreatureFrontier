#pragma once
#include "CoreMinimal.h"
#include "CreatureJournalTypes.generated.h"

class UCreatureDefinition;

USTRUCT()
struct FCreatureJournalEntry
{
    GENERATED_BODY()
    UPROPERTY() TSoftObjectPtr<UCreatureDefinition> Definition;
    UPROPERTY() bool bCaptured = false;
};
