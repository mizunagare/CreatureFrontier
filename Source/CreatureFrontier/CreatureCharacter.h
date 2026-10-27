#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CreatureTypes.h"
#include "CreatureCharacter.generated.h"

class UCreatureDefinition;
class UStaticMeshComponent;
class AWorkStation;

UCLASS(Blueprintable)
class CREATUREFRONTIER_API ACreatureCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    ACreatureCharacter();

    virtual void Tick(float DeltaSeconds) override;
    UPROPERTY()
    TObjectPtr<AWorkStation> WorkSite;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Creature")
    TObjectPtr<UStaticMeshComponent> CreatureVisual;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Creature")
    TObjectPtr<UCreatureDefinition> Definition;

    UPROPERTY(ReplicatedUsing=OnRep_Health, BlueprintReadOnly, Category="Creature")
    float Health = 100.0f;

    UPROPERTY(Replicated, BlueprintReadOnly, Category="Creature")
    ECreatureDisposition Disposition = ECreatureDisposition::Wild;

    UPROPERTY(ReplicatedUsing=OnRep_Activity, BlueprintReadOnly, Category="Creature")
    ECreatureActivity Activity = ECreatureActivity::Idle;

    UPROPERTY(Replicated, BlueprintReadOnly, Category="Creature")
    TObjectPtr<AController> Tamer;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI", meta=(ClampMin="0.0"))
    float AggroRange = 650.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI", meta=(ClampMin="0.0"))
    float AttackRange = 150.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI", meta=(ClampMin="0.1"))
    float AttackInterval = 1.2f;

    UFUNCTION(BlueprintCallable, Category="Creature")
    float GetHealthRatio() const;

    UFUNCTION(BlueprintCallable, Category="Creature", BlueprintAuthorityOnly)
    void SetActivity(ECreatureActivity NewActivity);

    UFUNCTION(BlueprintCallable, Category="Creature", BlueprintAuthorityOnly)
    void Tame(AController* NewTamer);

    UFUNCTION(BlueprintNativeEvent, Category="Creature")
    void OnTamed();

    virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent,
        AController* EventInstigator, AActor* DamageCauser) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
    virtual void BeginPlay() override;

    UFUNCTION()
    void OnRep_Health();

    UFUNCTION()
    void OnRep_Activity();

private:
    void NavigateTo(const FVector& Destination, float Acceptance);
    void StopNavigation();
    bool CanHit(const AActor* Target) const;
    double NextNavigationUpdate = 0.0;
    float AttackCooldown = 0.0f;
    float WorkProgress = 0.0f;
};
