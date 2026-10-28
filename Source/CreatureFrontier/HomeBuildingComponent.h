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
    bool Dismantle(AHomeModule* Module);
    AHomeModule* ReplaceWall(AHomeModule* Module);
    void ConfirmAimedReplacement();
    bool ValidatePlacement(const FTransform& Transform,FGuid FloorId,FString& Reason) const;
    EHomeModuleKind Selected=EHomeModuleKind::Floor;
    AHomeModule* GetPreview() const { return Preview; }
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY() TObjectPtr<AHomeModule> Preview;
    FTransform Placement;
    FGuid SupportingFloor;
    bool bValid=false;
    int32 YawStep=0;
    FString Status;
    TWeakObjectPtr<AHomeModule> PendingReplacement;
    EHomeModuleKind PendingKind=EHomeModuleKind::Floor;
    double ReplacementDeadline=0;
};
