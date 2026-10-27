#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CreatureTypes.h"
#include "WorkStation.generated.h"

UCLASS()
class CREATUREFRONTIER_API AWorkStation : public AActor
{
    GENERATED_BODY()
public:
    AWorkStation();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Work")
    EWorkSpecialty RequiredSpecialty = EWorkSpecialty::Gathering;
    UPROPERTY(BlueprintReadOnly, Category="Building") bool bPlayerBuilt = false;
    UPROPERTY(BlueprintReadOnly, Category="Building") FGuid PersistentId;
    UPROPERTY(BlueprintReadOnly, Category="Feeding") int32 StoredFood = 0;
    bool TransferFood(class ACFPlayerCharacter* Hero, bool Deposit);
    bool ServeFood(class ACreatureCharacter* Pet);
    bool IsFoodStore() const { return RequiredSpecialty==EWorkSpecialty::Feeding || RequiredSpecialty==EWorkSpecialty::Farming; }
    bool AdjustFoodStock(int32 Delta);
private:
    void RefreshLabel();
    UPROPERTY() TObjectPtr<class UTextRenderComponent> Label;
    UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> FarmPlants;
};
