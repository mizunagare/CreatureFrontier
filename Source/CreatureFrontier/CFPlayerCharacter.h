#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CFPlayerCharacter.generated.h"

class UCameraComponent;
class UCaptureComponent;
class ACreatureCharacter;

UCLASS()
class CREATUREFRONTIER_API ACFPlayerCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    ACFPlayerCharacter();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Camera")
    TObjectPtr<UCameraComponent> FirstPersonCamera;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Creature")
    TObjectPtr<UCaptureComponent> CaptureComponent;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat", meta=(ClampMin="0.0"))
    float AttackDamage = 25.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat", meta=(ClampMin="100.0"))
    float InteractionRange = 1200.0f;

    UFUNCTION(BlueprintCallable, Category="Combat")
    void Attack();

    UFUNCTION(BlueprintCallable, Category="Creature")
    void CaptureTarget();

protected:
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:
    void MoveForward(float Value);
    void MoveRight(float Value);
    ACreatureCharacter* TraceCreature() const;
};
