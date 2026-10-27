#pragma once
#include "CoreMinimal.h"
#include "FrontierItems.generated.h"

// Serialized numeric values 0-3 remain unchanged for old snapshots.
UENUM(BlueprintType)
enum class EFrontierItem : uint8 { Material, CaptureDevice, Medicine, Food, Crystal, ResonanceDevice, Count UMETA(Hidden) };

UENUM(BlueprintType)
enum class EFrontierResearch : uint8 { None, CrystalSample, CrystalStudy };

USTRUCT()
struct FResearchProgress
{
    GENERATED_BODY()
    UPROPERTY() bool bCrystalSample=false;
    UPROPERTY() bool bCrystalStudy=false;
};
