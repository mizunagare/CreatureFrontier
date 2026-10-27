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
    UPROPERTY() TSoftObjectPtr<UHomeModuleDefinition> Definition;
    UPROPERTY() FTransform Transform;
};

namespace HomeLayout
{
    bool IsCanonical(const FHomeModuleRecord& Record);
    bool Validate(const TArray<FHomeModuleRecord>& Records,FString& Reason);
    bool ValidateTerrain(UWorld* World,const FHomeModuleRecord& Record,FString& Reason);
    bool SupportsFacility(const FTransform& Facility,const TArray<FHomeModuleRecord>& Records);
    TArray<FHomeModuleRecord> Collect(UWorld* World);
    FGuid FindFloor(const FVector& Point,const TArray<FHomeModuleRecord>& Records);
}
