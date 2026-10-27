#include "InventoryComponent.h"

UInventoryComponent::UInventoryComponent()
{
    Stacks.Add(EFrontierItem::Material, 6);
    Stacks.Add(EFrontierItem::CaptureDevice, 5);
    Stacks.Add(EFrontierItem::Medicine, 1);
    Stacks.Add(EFrontierItem::Food, 3);
}

int32 UInventoryComponent::Count(EFrontierItem Item) const { return Stacks.FindRef(Item); }

bool UInventoryComponent::AddItem(EFrontierItem Item, int32 Amount)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || Amount <= 0 ||
        static_cast<uint8>(Item) > static_cast<uint8>(EFrontierItem::Food) || Amount > StackLimit - Count(Item)) return false;
    Stacks.FindOrAdd(Item) += Amount;
    return true;
}

bool UInventoryComponent::Consume(EFrontierItem Item, int32 Amount)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || Amount <= 0 || Count(Item) < Amount) return false;
    Stacks.FindOrAdd(Item) -= Amount;
    return true;
}

bool UInventoryComponent::Craft(EFrontierItem Output)
{
    const int32 Cost = Output == EFrontierItem::CaptureDevice ? 3 : Output == EFrontierItem::Medicine ? 5 : Output == EFrontierItem::Food ? 2 : 0;
    if (!GetOwner() || !GetOwner()->HasAuthority() || Cost == 0 || Count(Output) >= StackLimit || Count(EFrontierItem::Material) < Cost) return false;
    Stacks.FindOrAdd(EFrontierItem::Material) -= Cost;
    ++Stacks.FindOrAdd(Output);
    return true;
}

bool UInventoryComponent::Validate(const TMap<EFrontierItem, int32>& NewStacks)
{
    for (const auto& Pair : NewStacks)
        if (static_cast<uint8>(Pair.Key) > static_cast<uint8>(EFrontierItem::Food) || Pair.Value < 0 || Pair.Value > StackLimit) return false;
    return true;
}

bool UInventoryComponent::Restore(const TMap<EFrontierItem, int32>& NewStacks)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !Validate(NewStacks)) return false;
    Stacks = NewStacks;
    return true;
}
