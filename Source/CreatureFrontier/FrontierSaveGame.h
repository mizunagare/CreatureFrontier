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
    UPROPERTY() float Nutrition = 100;
    UPROPERTY() float AutoFeedCooldown = 0;
    UPROPERTY() bool bResumeWorkAfterRest = false;
    UPROPERTY() float SpecialCooldown = 0.0f;
    UPROPERTY() bool bOwned = false;
    UPROPERTY() ECreatureDisposition Disposition = ECreatureDisposition::Wild;
    UPROPERTY() ECreatureActivity Activity = ECreatureActivity::Idle;
    UPROPERTY() FName WorkSiteName;
    UPROPERTY() FGuid WorkSiteId;
    UPROPERTY() FName EncounterId;
    UPROPERTY() bool bAlpha = false;
    UPROPERTY() float AlphaCooldown = 3;
    UPROPERTY() bool bHasHabitat = false;
    UPROPERTY() FVector HabitatHome = FVector::ZeroVector;
    UPROPERTY() float RoamRadius = 250;
    UPROPERTY() float LeashRadius = 1000;
};

USTRUCT()
struct FLootSaveRecord
{
    GENERATED_BODY()
    UPROPERTY() FTransform Transform;
    UPROPERTY() int32 Materials = 0;
    UPROPERTY() int32 Medicine = 0;
};

USTRUCT()
struct FFacilitySaveRecord
{
    GENERATED_BODY()
    UPROPERTY() FTransform Transform;
    UPROPERTY() FGuid Id;
    UPROPERTY() EWorkSpecialty Specialty = EWorkSpecialty::Gathering;
    UPROPERTY() int32 StoredFood = 0;
};

UCLASS()
class CREATUREFRONTIER_API UFrontierSaveGame : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY() int32 FormatVersion = 11;
    UPROPERTY() FString MapName;
    UPROPERTY() FTransform PlayerTransform;
    UPROPERTY() FRotator ViewRotation;
    UPROPERTY() float Health = 100.0f;
    UPROPERTY() int32 Resources = 0;
    UPROPERTY() TMap<EFrontierItem, int32> InventoryStacks;
    UPROPERTY() TArray<FCreatureSaveRecord> Creatures;
    UPROPERTY() TMap<FName, float> EncounterTimers;
    UPROPERTY() TArray<FLootSaveRecord> Loot;
    UPROPERTY() TArray<FFacilitySaveRecord> Facilities;
};
