#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FrontierItems.h"
#include "StorageInteractionComponent.generated.h"
class AHomeModule;

UCLASS()
class CREATUREFRONTIER_API UStorageInteractionComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UStorageInteractionComponent();
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* Tick) override;
    bool TryOpenAimed();
    AHomeModule* TraceCrate() const;
    bool Open(AHomeModule* Crate);
    void Close();
    bool IsOpen() const;
    AHomeModule* GetCrate() const;
    int32 GetSelection() const { return Selection; }
    void Step(int32 Direction);
    bool Transfer(EFrontierItem Item,int32 Amount,bool Deposit);
    void DepositOne();
    void WithdrawOne();
    void DepositStack();
    void WithdrawStack();
private:
    bool CanAccess(const AHomeModule* Crate) const;
    TWeakObjectPtr<AHomeModule> Target;
    int32 Selection=0;
};
