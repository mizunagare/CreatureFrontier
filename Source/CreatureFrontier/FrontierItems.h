#pragma once
#include "CoreMinimal.h"
#include "FrontierItems.generated.h"

// 为兼容旧快照，序列化数值 0–3 保持不变。
UENUM(BlueprintType)
enum class EFrontierItem : uint8 { Material, CaptureDevice, Medicine, Food, Crystal, ResonanceDevice, Fiber, Ore, Scrap, Seeds, Count UMETA(Hidden) };

inline bool IsGatheredResource(EFrontierItem Item)
{ return Item==EFrontierItem::Material || Item==EFrontierItem::Crystal || Item==EFrontierItem::Fiber || Item==EFrontierItem::Ore || Item==EFrontierItem::Scrap; }

UENUM(BlueprintType)
enum class EFrontierResearch : uint8 { None, CrystalSample, CrystalStudy };

USTRUCT()
struct FResearchProgress
{
    GENERATED_BODY()
    UPROPERTY() bool bCrystalSample=false;
    UPROPERTY() bool bCrystalStudy=false;
};
