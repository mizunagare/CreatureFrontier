#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CreatureTypes.h"
#include "FrontierItems.h"
#include "ResourceDefinition.generated.h"

// 资源预算与工作成本属于内容数据，不在玩家和 AI 规则中重复定义。
UCLASS(BlueprintType)
class CREATUREFRONTIER_API UResourceDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FText DisplayName;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) EWorkSpecialty Specialty=EWorkSpecialty::Gathering;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) int32 Capacity=12;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float WorkSeconds=3;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float HarvestInterval=.8f;
    // 零表示当前世界快照中永久耗尽，不进行离线再生。
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float RegrowSeconds=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FLinearColor Color=FLinearColor::Green;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) EFrontierItem OutputItem=EFrontierItem::Material;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) EFrontierResearch Discovery=EFrontierResearch::None;
    bool IsValidDefinition() const
    {
        return (Specialty==EWorkSpecialty::Gathering || Specialty==EWorkSpecialty::Mining || Specialty==EWorkSpecialty::Crafting) &&
            IsGatheredResource(OutputItem) &&
            (Discovery==EFrontierResearch::None || (Discovery==EFrontierResearch::CrystalSample && OutputItem==EFrontierItem::Crystal)) &&
            Capacity>0 && Capacity<=999 && FMath::IsFinite(WorkSeconds) && WorkSeconds>=1 && WorkSeconds<=60 &&
            FMath::IsFinite(HarvestInterval) && HarvestInterval>=.1f && HarvestInterval<=10 &&
            FMath::IsFinite(RegrowSeconds) && RegrowSeconds>=0 && RegrowSeconds<=3600;
    }
};
