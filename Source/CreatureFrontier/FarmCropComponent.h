#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FarmCropComponent.generated.h"
class ACFPlayerCharacter;
class ACreatureCharacter;
struct FFacilitySaveRecord;

UCLASS()
class CREATUREFRONTIER_API UFarmCropComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    static constexpr float RequiredWork=6;
    UPROPERTY(BlueprintReadOnly) int32 Seeds=0;
    UPROPERTY(BlueprintReadOnly) bool bPlanted=false;
    UPROPERTY(BlueprintReadOnly) float GrowthWork=0;
    int32 Stage() const;
    FString Status() const;
    bool Interact(ACFPlayerCharacter* Hero,int32 Mode);
    bool Work(ACreatureCharacter* Pet,ACFPlayerCharacter* Hero,float Amount);
    bool IsOccupied() const { return Seeds>0 || bPlanted; }
    static bool Validate(const FFacilitySaveRecord& Record);
private:
    void Refresh();
    bool Sow();
    bool Harvest();
    double NextManualWork=0;
};
