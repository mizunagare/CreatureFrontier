#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CompanionConstructionComponent.generated.h"
class AHomeModule;
class ACFPlayerCharacter;
UCLASS()
class CREATUREFRONTIER_API UCompanionConstructionComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    bool Assign(AHomeModule* Site,AHomeModule* Crate,ACFPlayerCharacter* Hero);
    void Cancel();
    AHomeModule* GetSite() const;
    bool IsWorking() const { return bWorking; }
    uint32 GetApproachSearchCount() const { return ApproachSearchCount; }
    bool TickWork(float DeltaSeconds,ACFPlayerCharacter* Hero);
    void Restore(AHomeModule* Site);
private:
    bool Reach(AActor* Target) const;
    void Approach(AActor* Target);
    TWeakObjectPtr<AHomeModule> Goal;
    TWeakObjectPtr<AActor> ApproachTarget;
    FVector ApproachPoint=FVector::ZeroVector;
    double NextSearch=0;
    bool bHasApproach=false;
    bool bWorking=false;
    uint32 ApproachSearchCount=0;
};
