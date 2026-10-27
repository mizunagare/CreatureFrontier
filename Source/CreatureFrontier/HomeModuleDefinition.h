#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HomeModuleDefinition.generated.h"

UENUM(BlueprintType)
enum class EHomeModuleKind : uint8 { Floor, Wall, Doorway, Roof };

UCLASS(BlueprintType)
class CREATUREFRONTIER_API UHomeModuleDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FText DisplayName;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) EHomeModuleKind Kind=EHomeModuleKind::Floor;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) int32 MaterialCost=8;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FLinearColor Color=FLinearColor(.32f,.2f,.12f);
    bool IsValidDefinition() const { return static_cast<uint8>(Kind)<=static_cast<uint8>(EHomeModuleKind::Roof) && MaterialCost>=2 && MaterialCost<=100; }
    static UHomeModuleDefinition* LoadDefault(EHomeModuleKind Kind);
};
