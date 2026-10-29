#pragma once
#include "CoreMinimal.h"
#include "HomeModuleDefinition.h"
#include "HomeLayout.generated.h"

USTRUCT()
struct FHomeModuleRecord
{
    GENERATED_BODY()
    UPROPERTY() FGuid Id;
    UPROPERTY() FGuid FloorId;
    UPROPERTY() int32 PaidMaterials=0;
    UPROPERTY() TMap<EFrontierItem,int32> PaidExtraItems;
    UPROPERTY() TMap<EFrontierItem,int32> StoredItems;
    UPROPERTY() uint8 DoorState=0;
    UPROPERTY() bool bUnderConstruction=false;
    UPROPERTY() FGuid ConstructionCrateId;
    UPROPERTY() float ConstructionWork=0;
    UPROPERTY() TSoftObjectPtr<UHomeModuleDefinition> ConstructionOriginalDefinition;
    UPROPERTY() TSoftObjectPtr<UHomeModuleDefinition> Definition;
    UPROPERTY() FTransform Transform;
};

namespace HomeLayout
{
    inline constexpr double StoreyHeight=320.0;
    bool IsFloorHeight(double Z);
    bool IsCanonical(const FHomeModuleRecord& Record);
    bool Validate(const TArray<FHomeModuleRecord>& Records,FString& Reason);
    bool ValidateTerrain(UWorld* World,const FHomeModuleRecord& Record,FString& Reason);
    bool SupportsFacility(const FTransform& Facility,const TArray<FHomeModuleRecord>& Records);
    bool ClearsDoorReservations(const FBox& Bounds,const TArray<FHomeModuleRecord>& Records);
    bool ClearsStairReservations(const FBox& Bounds,const TArray<FHomeModuleRecord>& Records);
    TArray<FHomeModuleRecord> Collect(UWorld* World);
    FGuid FindFloor(const FVector& Point,const TArray<FHomeModuleRecord>& Records,double FloorZ=-40.0);
}
