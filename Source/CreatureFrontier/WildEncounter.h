#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WildEncounter.generated.h"

class ACreatureCharacter;
class UCreatureDefinition;

// 持久化的种群槽位，而非不检查数量就不断生成角色的定时器。
UCLASS()
class CREATUREFRONTIER_API AWildEncounter : public AActor
{
    GENERATED_BODY()
public:
    AWildEncounter();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    UPROPERTY(EditAnywhere, Category="Encounter") FName EncounterId;
    UPROPERTY(EditAnywhere, Category="Encounter") TObjectPtr<UCreatureDefinition> Definition;
    UPROPERTY(EditAnywhere, Category="Encounter", meta=(ClampMin="1", ClampMax="20")) int32 CreatureLevel = 2;
    UPROPERTY(EditAnywhere, Category="Encounter", meta=(ClampMin="1", ClampMax="600")) float RespawnSeconds = 60;
    UPROPERTY(EditAnywhere, Category="Encounter") FString RegionName = TEXT("野生栖息地");
    UPROPERTY(EditAnywhere, Category="Encounter") bool bBoss = false;
    // 该栖息地真实住民造成的同行倒地可记录为脱险经历来源。
    UPROPERTY(EditAnywhere, Category="Encounter") bool bRecordsRescueJourney = false;
    UPROPERTY(VisibleAnywhere, Category="Encounter") float Remaining = 0;
    ACreatureCharacter* GetResident() const;
    float GetSavedRemaining() const;
    void RestorePopulation(float SavedRemaining);
    FString GetStatus() const;
private:
    TWeakObjectPtr<ACreatureCharacter> Resident;
    TWeakObjectPtr<ACreatureCharacter> Corpse;
    bool bWaiting = true;
    bool IsSpawnAreaClear() const;
};
