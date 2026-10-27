#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CreatureTypes.h"
#include "FrontierItems.h"
#include "ResourceDefinition.generated.h"

// Resource budgets and work costs are content, not duplicated player/AI rules.
UCLASS(BlueprintType)
class CREATUREFRONTIER_API UResourceDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FText DisplayName;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) EWorkSpecialty Specialty=EWorkSpecialty::Gathering;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) int32 Capacity=12;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float WorkSeconds=3;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float HarvestInterval=.8f;
    // Zero means permanently depleted in this world snapshot. No offline growth.
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float RegrowSeconds=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FLinearColor Color=FLinearColor::Green;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) EFrontierItem OutputItem=EFrontierItem::Material;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) EFrontierResearch Discovery=EFrontierResearch::None;
    bool IsValidDefinition() const
    {
        return (Specialty==EWorkSpecialty::Gathering || Specialty==EWorkSpecialty::Mining || Specialty==EWorkSpecialty::Crafting) &&
            IsGatheredResource(OutputItem) &&
            (Discovery==EFrontierResearch::None || (Discovery==EFrontierResearch::CrystalSample && OutputItem==EFrontierItem::Crystal)) &&
            Capacity>0 && Capacity<=999 && FMath::IsFinite(WorkSeconds) && WorkSeconds>=1 && WorkSeconds<=60 &&
            FMath::IsFinite(HarvestInterval) && HarvestInterval>=.1f && HarvestInterval<=10 &&
            FMath::IsFinite(RegrowSeconds) && RegrowSeconds>=0 && RegrowSeconds<=3600;
    }
};
