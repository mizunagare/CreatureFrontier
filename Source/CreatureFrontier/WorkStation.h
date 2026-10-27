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
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Work")
    EWorkSpecialty RequiredSpecialty = EWorkSpecialty::Gathering;
    UPROPERTY(BlueprintReadOnly, Category="Building") bool bPlayerBuilt = false;
    UPROPERTY(BlueprintReadOnly, Category="Building") FGuid PersistentId;
private:
    void RefreshLabel();
    UPROPERTY() TObjectPtr<class UTextRenderComponent> Label;
    UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> FarmPlants;
};
