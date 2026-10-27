#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CreatureTypes.h"
#include "CreatureDefinition.generated.h"

UCLASS(BlueprintType)
class CREATUREFRONTIER_API UCreatureDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Identity")
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Identity", meta=(MultiLine=true))
    FText Description;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stats", meta=(ClampMin="1"))
    int32 BaseMaxHealth = 100;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stats", meta=(ClampMin="0"))
    int32 BaseAttack = 10;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Capture", meta=(ClampMin="0.01", ClampMax="1.0"))
    float BaseCaptureRate = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Work")
    TArray<EWorkSpecialty> WorkSpecialties;
};

