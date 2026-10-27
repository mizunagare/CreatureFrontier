#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EquipmentTypes.h"
#include "PlayerEquipmentComponent.generated.h"
class UStaticMeshComponent;

UCLASS(ClassGroup=(Frontier),meta=(BlueprintSpawnableComponent))
class CREATUREFRONTIER_API UPlayerEquipmentComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UPlayerEquipmentComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction) override;
    void CraftSpear();
    void CraftVest();
    void ToggleSpear();
    void ToggleVest();
    void PlayAttack();
    void RefreshVisual();
    bool IsWeaponVisible() const;
    float GetAttackBonus() const;
    float GetDamageReduction() const;
    const FPlayerEquipmentState& GetState() const { return State; }
    const TArray<TObjectPtr<UStaticMeshComponent>>& GetWeaponParts() const { return WeaponParts; }
    bool Restore(const FPlayerEquipmentState& NewState);
    static bool Validate(const FPlayerEquipmentState& Candidate);
    UPROPERTY(EditDefaultsOnly,Category="Equipment",meta=(ClampMin="1",ClampMax="999")) int32 SpearCost=8;
    UPROPERTY(EditDefaultsOnly,Category="Equipment",meta=(ClampMin="1",ClampMax="999")) int32 VestCost=12;
    UPROPERTY(EditDefaultsOnly,Category="Equipment",meta=(ClampMin="0",ClampMax="100")) float SpearAttackBonus=10;
    UPROPERTY(EditDefaultsOnly,Category="Equipment",meta=(ClampMin="0",ClampMax="0.75")) float VestReduction=.2f;
private:
    bool Craft(bool bSpear);
    UPROPERTY() FPlayerEquipmentState State;
    UPROPERTY() TObjectPtr<class USceneComponent> WeaponPivot;
    UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> WeaponParts;
    UPROPERTY() TObjectPtr<class UStaticMesh> ShaftMesh;
    UPROPERTY() TObjectPtr<class UStaticMesh> TipMesh;
    UPROPERTY() TObjectPtr<class UMaterialInterface> Palette;
    float SwingRemaining=0;
};
