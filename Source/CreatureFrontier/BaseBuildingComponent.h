#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CreatureTypes.h"
#include "BaseBuildingComponent.generated.h"
class AWorkStation;

UCLASS()
class CREATUREFRONTIER_API UBaseBuildingComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UBaseBuildingComponent();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Tick) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void Toggle();
    void Cancel();
    void Cycle();
    void Confirm();
    void Dismantle();
    void SwitchCategory();
    void RotateHome();
    void ReplaceHome();
    bool IsActive() const { return bActive; }
    bool IsRelocating() const { return bRelocating; }
    FString GetHint() const;
    FTransform GetPlacementTransform() const { return Placement; }
    static int32 GetCost(EWorkSpecialty Specialty);
    static bool IsBuildTransformValid(const FTransform& Transform);
    bool ValidatePlacement(const FTransform& Transform, FString& Reason) const;
    AWorkStation* BuildAt(const FTransform& Transform);
    bool DismantleStation(AWorkStation* Station);
    EWorkSpecialty Selected = EWorkSpecialty::Gathering;
    static FVector BaseOrigin() { return FVector(-450,0,-50); }
private:
    bool UpdatePlacement();
    bool bActive = false;
    bool bHomeMode=false;
    bool bValid = false;
    FString Status;
    FTransform Placement;
    UPROPERTY() TObjectPtr<class AStaticMeshActor> Preview;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> PreviewMaterial;
    bool bManualFacilityRotation=false;
    float FacilityYaw=0;
    bool bRelocating=false;
    TWeakObjectPtr<AWorkStation> RelocatingStation;
    void ConfirmRelocation();
};
