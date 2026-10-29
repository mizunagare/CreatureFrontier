#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HomeModuleDefinition.h"
#include "HomeBuildingComponent.generated.h"
class AHomeModule;

UCLASS()
class CREATUREFRONTIER_API UHomeBuildingComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    void StartPreview();
    void Cancel();
    void Cycle();
    void Rotate();
    void UpdatePreview();
    void Confirm();
    FString GetHint() const;
    AHomeModule* BuildAt(const FTransform& Transform,FGuid FloorId=FGuid());
    AHomeModule* QueueAt(const FTransform& Transform,FGuid FloorId=FGuid());
    bool CompleteConstruction(AHomeModule* Module);
    bool Dismantle(AHomeModule* Module);
    AHomeModule* ReplaceWall(AHomeModule* Module);
    AHomeModule* QueueWallReplacement(AHomeModule* Module);
    void ConfirmAimedReplacement();
    bool ValidatePlacement(const FTransform& Transform,FGuid FloorId,FString& Reason,bool Plan=false,const AHomeModule* Completing=nullptr) const;
    EHomeModuleKind Selected=EHomeModuleKind::Floor;
    AHomeModule* GetPreview() const { return Preview; }
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    bool IsConstructionInput() const;
    UPROPERTY() TObjectPtr<AHomeModule> Preview;
    FTransform Placement;
    FGuid SupportingFloor;
    bool bValid=false;
    int32 YawStep=0;
    FString Status;
    TWeakObjectPtr<AHomeModule> PendingReplacement;
    EHomeModuleKind PendingKind=EHomeModuleKind::Floor;
    bool bPendingConstruction=false;
    double ReplacementDeadline=0;
};
