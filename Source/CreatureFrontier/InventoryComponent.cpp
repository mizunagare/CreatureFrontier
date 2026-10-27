#include "InventoryComponent.h"
#include "CraftingCatalog.h"

UInventoryComponent::UInventoryComponent()
{
    Stacks.Add(EFrontierItem::Material, 6);
    Stacks.Add(EFrontierItem::CaptureDevice, 5);
    Stacks.Add(EFrontierItem::Medicine, 1);
    Stacks.Add(EFrontierItem::Food, 3);
}

int32 UInventoryComponent::Count(EFrontierItem Item) const { return Stacks.FindRef(Item); }

bool UInventoryComponent::GrantItems(const TMap<EFrontierItem,int32>& Items)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || Items.IsEmpty()) return false;
    auto Updated=Stacks;
    for(const auto& Item:Items)
    {
        if(Item.Key>=EFrontierItem::Count || Item.Value<=0 || Item.Value>StackLimit-Updated.FindRef(Item.Key)) return false;
        Updated.FindOrAdd(Item.Key)+=Item.Value;
    }
    Stacks=MoveTemp(Updated); return true;
}

bool UInventoryComponent::AddItem(EFrontierItem Item, int32 Amount)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || Amount <= 0 ||
        Item>=EFrontierItem::Count || Amount > StackLimit - Count(Item)) return false;
    Stacks.FindOrAdd(Item) += Amount;
    return true;
}

bool UInventoryComponent::Consume(EFrontierItem Item, int32 Amount)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || Item>=EFrontierItem::Count || Amount <= 0 || Count(Item) < Amount) return false;
    Stacks.FindOrAdd(Item) -= Amount;
    return true;
}

int32 UInventoryComponent::CraftCost(EFrontierItem Output)
{
    if(auto* Catalog=UCraftingCatalog::Load()) for(const auto& Recipe:Catalog->Recipes)
        if(Recipe.Reward==ERecipeReward::Item && Recipe.Output==Output && Recipe.RequiredResearch==EFrontierResearch::None && Recipe.Inputs.Num()==1)
            return Recipe.Inputs.FindRef(EFrontierItem::Material);
    return 0;
}

bool UInventoryComponent::Craft(EFrontierItem Output)
{
    const auto* Catalog=UCraftingCatalog::Load();
    if(!Catalog || !Catalog->Validate()) return false;
    for(const auto& Recipe:Catalog->Recipes)
        if(Recipe.Reward==ERecipeReward::Item && Recipe.Output==Output && Recipe.RequiredResearch==EFrontierResearch::None && !Recipe.bBaseOnly)
            return Transact(Recipe.Inputs,{{Recipe.Output,Recipe.OutputAmount}});
    return false;
}

bool UInventoryComponent::Validate(const TMap<EFrontierItem, int32>& NewStacks)
{
    for (const auto& Pair : NewStacks)
        if (Pair.Key>=EFrontierItem::Count || Pair.Value < 0 || Pair.Value > StackLimit) return false;
    return true;
}

bool UInventoryComponent::Transact(const TMap<EFrontierItem,int32>& Inputs,const TMap<EFrontierItem,int32>& Outputs)
{
    if(!GetOwner() || !GetOwner()->HasAuthority() || (Inputs.IsEmpty() && Outputs.IsEmpty()) || !Validate(Inputs) || !Validate(Outputs)) return false;
    auto Updated=Stacks;
    for(const auto& Entry:Inputs)
    {
        if(Entry.Value<=0 || Updated.FindRef(Entry.Key)<Entry.Value) return false;
        Updated.FindOrAdd(Entry.Key)-=Entry.Value;
    }
    for(const auto& Entry:Outputs)
    {
        if(Entry.Value<=0 || Entry.Value>StackLimit-Updated.FindRef(Entry.Key)) return false;
        Updated.FindOrAdd(Entry.Key)+=Entry.Value;
    }
    Stacks=MoveTemp(Updated); return true;
}

bool UInventoryComponent::Restore(const TMap<EFrontierItem, int32>& NewStacks)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !Validate(NewStacks)) return false;
    Stacks = NewStacks;
    return true;
}
