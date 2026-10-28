#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FrontierItems.h"
#include "HomeModuleDefinition.generated.h"

UENUM(BlueprintType)
enum class EHomeModuleKind : uint8 { Floor, Wall, Doorway, Roof, Lantern, Storage, Door, Window };

UCLASS(BlueprintType)
class CREATUREFRONTIER_API UHomeModuleDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FText DisplayName;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) EHomeModuleKind Kind=EHomeModuleKind::Floor;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) int32 MaterialCost=8;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) TMap<EFrontierItem,int32> ExtraCosts;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) EFrontierResearch RequiredResearch=EFrontierResearch::None;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FLinearColor Color=FLinearColor(.32f,.2f,.12f);
    // 门扇独立于门框着色；其他模块不使用此字段，不写入玩家存档。
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FLinearColor DoorLeafColor=FLinearColor(.65f,.38f,.12f);
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float LanternLumens=0;
    bool IsValidDefinition() const;
    static UHomeModuleDefinition* LoadDefault(EHomeModuleKind Kind);
};
