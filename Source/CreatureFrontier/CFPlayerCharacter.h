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
    virtual void BeginPlay() override;
    virtual float TakeDamage(float Amount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer) override;
    UPROPERTY(BlueprintReadOnly, Category="Player")
    float Health = 100.0f;
    UPROPERTY(BlueprintReadOnly, Category="Player")
    int32 Resources = 0;
    FString Feedback;
    ACreatureCharacter* TraceCreature() const;
    void CommandFollow();
    void CommandWork();
    void RestartLevel();
    bool SaveProgress(const FString& Slot = TEXT("FrontierSlot"));
    bool LoadProgress(const FString& Slot = TEXT("FrontierSlot"));
    void QuickSave();
    void QuickLoad();
    UFUNCTION()
    void CaptureResolved(ACreatureCharacter* Creature, bool bSucceeded);

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
    double NextAttackTime = 0.0;
};
