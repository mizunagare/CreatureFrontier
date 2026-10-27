#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HomeScenario.generated.h"
class UWorld;
class ACFPlayerCharacter;

// Held movement input for the packaged physics test, independent of frame rate.
UCLASS()
class UHomeMovementProbe : public UActorComponent
{
    GENERATED_BODY()
public:
    UHomeMovementProbe();
    FVector Direction=FVector::ZeroVector;
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* Tick) override;
};
void RunHomeScenario(UWorld* World,ACFPlayerCharacter* Hero,int32 Mode=0);
