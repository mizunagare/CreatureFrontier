#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "CreatureTypes.h"
#include "InventoryComponent.h"
#include "FrontierSaveGame.generated.h"

class ACreatureCharacter;
class UCreatureDefinition;

USTRUCT()
struct FCreatureSaveRecord
{
    GENERATED_BODY()
    UPROPERTY() TSoftClassPtr<ACreatureCharacter> CreatureClass;
    UPROPERTY() TSoftObjectPtr<UCreatureDefinition> Definition;
    UPROPERTY() FTransform Transform;
    UPROPERTY() float Health = 100.0f;
    UPROPERTY() int32 Level = 1;
    UPROPERTY() int32 Experience = 0;
    UPROPERTY() float Stamina = 100.0f;
    UPROPERTY() float SpecialCooldown = 0.0f;
    UPROPERTY() bool bOwned = false;
    UPROPERTY() ECreatureDisposition Disposition = ECreatureDisposition::Wild;
    UPROPERTY() ECreatureActivity Activity = ECreatureActivity::Idle;
    UPROPERTY() FName WorkSiteName;
};

UCLASS()
class CREATUREFRONTIER_API UFrontierSaveGame : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY() int32 FormatVersion = 4;
    UPROPERTY() FString MapName;
    UPROPERTY() FTransform PlayerTransform;
    UPROPERTY() FRotator ViewRotation;
    UPROPERTY() float Health = 100.0f;
    UPROPERTY() int32 Resources = 0;
    UPROPERTY() TMap<EFrontierItem, int32> InventoryStacks;
    UPROPERTY() TArray<FCreatureSaveRecord> Creatures;
};
