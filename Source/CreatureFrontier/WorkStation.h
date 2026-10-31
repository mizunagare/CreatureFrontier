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
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<class UFarmCropComponent> Crop;
    void RefreshFarm() { RefreshLabel(); }
    bool TransferFood(class ACFPlayerCharacter* Hero, bool Deposit);
    bool ServeFood(class ACreatureCharacter* Pet);
    bool IsFoodStore() const { return RequiredSpecialty==EWorkSpecialty::Feeding || RequiredSpecialty==EWorkSpecialty::Farming; }
    bool AdjustFoodStock(int32 Delta);
    static constexpr float RequiredCraftWork=8;
    FName CraftRecipeId;
    float CraftWork=0;
    bool ToggleCraftOrder(class ACFPlayerCharacter* Hero,FName Recipe);
    bool WorkCraft(class ACreatureCharacter* Pet,class ACFPlayerCharacter* Hero,float Amount);
    FString CraftStatus() const;
    static bool ValidateCraft(const struct FFacilitySaveRecord& Record);
private:
    TWeakObjectPtr<class ACreatureCharacter> CraftWorker;
    void RefreshLabel();
    UPROPERTY() TObjectPtr<class UTextRenderComponent> Label;
    UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> FarmPlants;
};
