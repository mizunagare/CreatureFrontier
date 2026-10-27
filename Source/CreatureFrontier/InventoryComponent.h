#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InventoryComponent.generated.h"

UENUM(BlueprintType)
enum class EFrontierItem : uint8 { Material, CaptureDevice, Medicine, Food };

UCLASS(ClassGroup=(Frontier), meta=(BlueprintSpawnableComponent))
class CREATUREFRONTIER_API UInventoryComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UInventoryComponent();
    static constexpr int32 StackLimit = 999;
    UFUNCTION(BlueprintPure) int32 Count(EFrontierItem Item) const;
    UFUNCTION(BlueprintCallable) bool AddItem(EFrontierItem Item, int32 Amount);
    bool GrantItems(const TMap<EFrontierItem,int32>& Items);
    UFUNCTION(BlueprintCallable) bool Consume(EFrontierItem Item, int32 Amount);
    UFUNCTION(BlueprintCallable) bool Craft(EFrontierItem Output);
    static int32 CraftCost(EFrontierItem Output);
    const TMap<EFrontierItem, int32>& GetStacks() const { return Stacks; }
    bool Restore(const TMap<EFrontierItem, int32>& NewStacks);
    static bool Validate(const TMap<EFrontierItem, int32>& NewStacks);
private:
    UPROPERTY() TMap<EFrontierItem, int32> Stacks;
};
