#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlayerGatheringComponent.generated.h"
class AResourceNode;
class ACFPlayerCharacter;

UCLASS(ClassGroup=(Player))
class CREATUREFRONTIER_API UPlayerGatheringComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UPlayerGatheringComponent();
    void Start();
    void Stop();
    bool IsGathering() const {return bHeld;}
    void HandleActivationChanged(bool Active);
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* Tick) override;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    bool CanGather(const ACFPlayerCharacter* Hero) const;
    bool bHeld=false;
    TWeakObjectPtr<AResourceNode> Target;
    FDelegateHandle ActivationHandle;
};
