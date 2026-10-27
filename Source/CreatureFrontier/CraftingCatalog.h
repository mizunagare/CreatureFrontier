#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FrontierItems.h"
#include "CraftingCatalog.generated.h"

UENUM(BlueprintType)
enum class ERecipeReward : uint8 { Item, Spear, Vest, CrystalStudy };

USTRUCT(BlueprintType)
struct FFrontierItemDisplay
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadOnly) EFrontierItem Item=EFrontierItem::Material;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FText DisplayName;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float CaptureMultiplier=0;
};
USTRUCT(BlueprintType)
struct FFrontierRecipe
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FName Id;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FText DisplayName;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FText Description;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) TMap<EFrontierItem,int32> Inputs;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) ERecipeReward Reward=ERecipeReward::Item;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) EFrontierItem Output=EFrontierItem::CaptureDevice;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) int32 OutputAmount=1;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) EFrontierResearch RequiredResearch=EFrontierResearch::None;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) bool bBaseOnly=false;
};
UCLASS(BlueprintType)
class CREATUREFRONTIER_API UCraftingCatalog : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadOnly) TArray<FFrontierItemDisplay> Items;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) TArray<FFrontierRecipe> Recipes;
    static UCraftingCatalog* Load();
    const FFrontierRecipe* Find(FName Id) const;
    static FString ItemName(EFrontierItem Item);
    static FString FormatItems(const TMap<EFrontierItem,int32>& Items);
    bool Validate() const;
};
