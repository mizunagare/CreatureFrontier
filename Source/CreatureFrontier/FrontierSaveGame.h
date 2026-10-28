#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "CreatureTypes.h"
#include "InventoryComponent.h"
#include "CreatureJournalTypes.h"
#include "ObjectiveTypes.h"
#include "EquipmentTypes.h"
#include "HomeLayout.h"
#include "CompanionPersonalityComponent.h"
#include "CompanionMemoryComponent.h"
#include "FrontierSaveGame.generated.h"

class ACreatureCharacter;
class UCreatureDefinition;
class UResourceDefinition;

USTRUCT()
struct FResourceSaveRecord
{
    GENERATED_BODY()
    UPROPERTY() FGuid Id;
    UPROPERTY() TSoftObjectPtr<UResourceDefinition> Definition;
    UPROPERTY() FTransform Transform;
    UPROPERTY() int32 Remaining=0;
    UPROPERTY() float HarvestCooldown=0;
    UPROPERTY() float RegrowRemaining=0;
};

USTRUCT()
struct FCreatureSaveRecord
{
    GENERATED_BODY()
    UPROPERTY() FGuid Id;
    UPROPERTY() FString Nickname;
    UPROPERTY() ECompanionPreference Preference=ECompanionPreference::None;
    UPROPERTY() bool bPreferenceDiscovered=false;
    // Store the raw byte: enum-name deserialization may turn unknown names into
    // the valid None default before our snapshot validator can reject them.
    UPROPERTY() uint8 CrystalJourney=0;
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
    UPROPERTY() int32 CarriedFood=0;
    UPROPERTY() int32 BondPoints=0;
    UPROPERTY() float PettingCooldown=0;
    UPROPERTY() ECreatureDisposition Disposition = ECreatureDisposition::Wild;
    UPROPERTY() ECreatureActivity Activity = ECreatureActivity::Idle;
    UPROPERTY() FName WorkSiteName;
    UPROPERTY() FGuid WorkSiteId;
    UPROPERTY() FGuid FieldResourceId;
    UPROPERTY() TMap<EFrontierItem,int32> CargoItems;
    UPROPERTY() FGuid DeliveryCrateId;
    UPROPERTY() bool bReturningCargo=false;
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
    UPROPERTY() int32 Seeds=0;
    UPROPERTY() bool bPlanted=false;
    UPROPERTY() float GrowthWork=0;
};

UCLASS()
class CREATUREFRONTIER_API UFrontierSaveGame : public USaveGame
{
    GENERATED_BODY()
public:
    // Legacy saves may omit this property when it equals the class default.
    // Freeze the pre-cargo fallback; writers explicitly set the current version
    // so future default changes cannot reinterpret an untagged legacy save.
    UPROPERTY() int32 FormatVersion = 21;
    UPROPERTY() FResearchProgress Research;
    UPROPERTY() TArray<FHomeModuleRecord> HomeModules;
    UPROPERTY() TArray<FResourceSaveRecord> ResourceNodes;
    UPROPERTY() FPlayerEquipmentState Equipment;
    UPROPERTY() FFrontierObjectiveProgress Objectives;
    UPROPERTY() TArray<FCreatureJournalEntry> Journal;
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
