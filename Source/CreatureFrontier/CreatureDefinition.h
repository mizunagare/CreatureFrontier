#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CreatureTypes.h"
#include "CreatureDefinition.generated.h"

UCLASS(BlueprintType)
class CREATUREFRONTIER_API UCreatureDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Identity")
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Identity", meta=(MultiLine=true))
    FText Description;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Appearance")
    ECreatureForm Form = ECreatureForm::Leaf;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Appearance")
    FLinearColor BodyColor = FLinearColor(0.16f, 0.6f, 0.3f);
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Appearance")
    FLinearColor AccentColor = FLinearColor(0.5f, 0.9f, 0.2f);
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Appearance")
    TObjectPtr<class UMaterialInterface> PaletteMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stats", meta=(ClampMin="100", ClampMax="600"))
    float MoveSpeed = 320.0f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stats", meta=(ClampMin="0.4", ClampMax="5"))
    float AttackSeconds = 1.2f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Work", meta=(ClampMin="0.5", ClampMax="3"))
    float WorkSpeed = 1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Special")
    FText SpecialName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Special", meta=(ClampMin="150", ClampMax="1200"))
    float SpecialRange = 600.0f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Special", meta=(ClampMin="1", ClampMax="5"))
    float SpecialDamageScale = 2.0f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Special", meta=(ClampMin="3", ClampMax="30"))
    float SpecialCooldownSeconds = 8.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stats", meta=(ClampMin="1"))
    int32 BaseMaxHealth = 100;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stats", meta=(ClampMin="0"))
    int32 BaseAttack = 10;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Capture", meta=(ClampMin="0.01", ClampMax="1.0"))
    float BaseCaptureRate = 0.35f;

    // 0表示不接受和平结交；资源成本由物种数据配置，不进入个体存档。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Companionship", meta=(ClampMin="0", ClampMax="10"))
    int32 PeacefulFoodCost = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Work")
    TArray<EWorkSpecialty> WorkSpecialties;
};

