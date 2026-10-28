#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlayerPaceComponent.generated.h"

UCLASS(ClassGroup=(Player))
class CREATUREFRONTIER_API UPlayerPaceComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UPlayerPaceComponent();
    void StartSlowWalk();
    void StopSlowWalk();
    bool IsSlowWalking() const {return bSlowWalking;}
    void HandleActivationChanged(bool Active);
    UPROPERTY(EditAnywhere,Category="Movement",meta=(ClampMin="1",ClampMax="600")) float SlowWalkSpeed=192.f;
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* Tick) override;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    bool bSlowWalking=false;
    float PreviousSpeed=600.f;
    FDelegateHandle ActivationHandle;
};
