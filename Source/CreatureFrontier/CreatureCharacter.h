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
    virtual void OnConstruction(const FTransform& Transform) override;
    UFUNCTION(BlueprintPure, Category="Creature")
    FString GetCreatureName() const;
    UFUNCTION(BlueprintPure, Category="Work")
    float GetWorkEfficiency(const AWorkStation* Site) const;

    virtual void Tick(float DeltaSeconds) override;
    UPROPERTY()
    TObjectPtr<AWorkStation> WorkSite;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Creature")
    TObjectPtr<UStaticMeshComponent> CreatureVisual;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Creature")
    TObjectPtr<UCreatureDefinition> Definition;

    UPROPERTY(BlueprintReadOnly, Category="Encounter") FName EncounterId;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Encounter") bool bAlpha = false;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Combat") float AlphaCooldown = 3;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Combat") float AlphaWindup = 0;
    UPROPERTY(BlueprintReadOnly, Category="Habitat") bool bHasHabitat = false;
    UPROPERTY(BlueprintReadOnly, Category="Habitat") FVector HabitatHome = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Habitat") float RoamRadius = 250;
    UPROPERTY(BlueprintReadOnly, Category="Habitat") float LeashRadius = 1000;

    UPROPERTY(ReplicatedUsing=OnRep_Health, BlueprintReadOnly, Category="Creature")
    float Health = 100.0f;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Progression")
    int32 Level = 1;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Progression")
    int32 Experience = 0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Care")
    float Stamina = 100.0f;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Care") float Nutrition = 100;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Care") float AutoFeedCooldown = 0;
    UPROPERTY(BlueprintReadOnly, Category="Care") TWeakObjectPtr<AWorkStation> FeedingSite;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Combat")
    float SpecialCooldownRemaining = 0.0f;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Combat")
    float SpecialWindupRemaining = 0.0f;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Combat")
    float SlowRemaining = 0.0f;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Combat")
    float GuardRemaining = 0.0f;
    UPROPERTY() TObjectPtr<ACreatureCharacter> FocusTarget;
    bool OrderAttack(ACreatureCharacter* Target);
    bool RequestSpecial(ACreatureCharacter* Target);
    void ClearCombatOrders();
    bool IsSpecialReady() const;
    FString GetSpecialName() const;
    UFUNCTION(BlueprintPure, Category="Progression")
    float GetMaxHealth() const;
    UFUNCTION(BlueprintPure, Category="Progression")
    float GetAttackDamage() const;
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Progression")
    void AddExperience(int32 Amount);

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
    bool TickAutoFeeding();
    double FoodSearchExpires = 0;
    bool TickHabitatReturn();
    void TickRoaming(float DeltaSeconds);
    FVector RoamTarget = FVector::ZeroVector;
    bool bHasRoamTarget = false;
    float RoamPause = 1;
    bool TickAlphaAttack(float DeltaSeconds, APawn* Target);
    UPROPERTY() TObjectPtr<UStaticMeshComponent> AlphaWarning;
    bool HasClearSight(const AActor* Target) const;
    bool TickSpecial(float DeltaSeconds);
    void ResolveSpecial();
    UPROPERTY() TObjectPtr<UStaticMeshComponent> SkillIndicator;
    TWeakObjectPtr<ACreatureCharacter> SpecialTarget;
    TWeakObjectPtr<APawn> RetaliationTarget;
    float RetaliationRemaining = 0.0f;
    bool bSpecialRequested = false;
    double SpecialRequestExpires = 0.0;
    float SkillPulseRemaining = 0.0f;
    void RefreshAppearance();
    UPROPERTY() TObjectPtr<class USceneComponent> VisualPivot;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> AccentParts;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> EyeParts;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Pupils;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Feet;
    UPROPERTY() TObjectPtr<class UStaticMesh> SphereShape;
    UPROPERTY() TObjectPtr<class UStaticMesh> CubeShape;
    void NavigateTo(const FVector& Destination, float Acceptance);
    void StopNavigation();
    bool CanHit(const AActor* Target) const;
    double NextNavigationUpdate = 0.0;
    float AttackCooldown = 0.0f;
    float WorkProgress = 0.0f;
    TWeakObjectPtr<AWorkStation> ProgressSite;
};
