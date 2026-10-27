#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FrontierItems.h"
#include "CompanionCargoComponent.generated.h"
class AHomeModule;
class ACFPlayerCharacter;
struct FCreatureSaveRecord;

UCLASS()
class CREATUREFRONTIER_API UCompanionCargoComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    static constexpr int32 Capacity=3;
    int32 Count() const;
    bool HasOrder() const { return bOrdered; }
    bool IsReturning() const { return bReturning; }
    AHomeModule* GetTarget() const;
    bool Assign(AHomeModule* Crate,ACFPlayerCharacter* Hero);
    void CancelOrder();
    bool ValidateOrder();
    bool TickDelivery(float DeltaSeconds,ACFPlayerCharacter* Hero);
    bool CanReceive(EFrontierItem Item) const;
    bool Recover(ACFPlayerCharacter* Hero);
    FString Status() const;
    void WriteSnapshot(FCreatureSaveRecord& Record) const;
    void RestoreSnapshot(const FCreatureSaveRecord& Record,AHomeModule* Target);
    static bool ValidateSnapshot(const FCreatureSaveRecord& Record,AHomeModule* Target);
private:
    friend class AResourceNode;
    bool Receive(EFrontierItem Item);
    TWeakObjectPtr<AHomeModule> Destination;
    bool bOrdered=false, bReturning=false, bFull=false;
    double Expires=0;
    float Retry=0;
};
