#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CaptureComponent.generated.h"

class ACreatureCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCaptureResolved, ACreatureCharacter*, Creature, bool, bSucceeded);

UCLASS(ClassGroup=(Creature), meta=(BlueprintSpawnableComponent))
class CREATUREFRONTIER_API UCaptureComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UCaptureComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture", meta=(ClampMin="0.0"))
    float CapturePower = 1.0f;

    UPROPERTY(BlueprintAssignable, Category="Capture")
    FCaptureResolved OnCaptureResolved;

    UFUNCTION(BlueprintCallable, Category="Capture")
    float CalculateCaptureChance(const ACreatureCharacter* Creature) const;

    UFUNCTION(BlueprintCallable, Server, Reliable, Category="Capture")
    void TryCapture(ACreatureCharacter* Creature);
    void ToggleDevice();
    bool UsesResonance() const { return bUseResonance; }
    void ResetSelection() { bUseResonance=false; }
private:
    bool bUseResonance=false;
    double NextCaptureTime = 0.0;
};

