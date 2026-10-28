#include "StorageInteractionComponent.h"
#include "CFPlayerCharacter.h"
#include "HomeModule.h"
#include "InventoryComponent.h"
#include "BaseBuildingComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"

UStorageInteractionComponent::UStorageInteractionComponent() { PrimaryComponentTick.bCanEverTick=true; }
bool UStorageInteractionComponent::CanAccess(const AHomeModule* Crate) const
{
    const auto* Hero=Cast<ACFPlayerCharacter>(GetOwner());
    if(!Hero || !Hero->HasAuthority() || Hero->Health<=0 || UGameplayStatics::IsGamePaused(this) || Hero->Building->IsActive() ||
        Hero->IsBackpackOpen() || Hero->IsRosterOpen() || Hero->IsJournalOpen() || Hero->IsHelpVisible() ||
        !IsValid(Crate) || Crate->GetWorld()!=GetWorld() || !Crate->HasAuthority() || Crate->IsActorBeingDestroyed() || Crate->bPreview || Crate->bStaged || !Crate->PersistentId.IsValid() ||
        Crate->GetKind()!=EHomeModuleKind::Storage || Hero->GetDistanceTo(Crate)>300) return false;
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(StorageAccess),false,Hero);
    return !GetWorld()->LineTraceSingleByChannel(Hit,Hero->FirstPersonCamera->GetComponentLocation(),Crate->GetActorLocation()+FVector(0,0,40),ECC_Visibility,Params) || Hit.GetActor()==Crate;
}
bool UStorageInteractionComponent::Open(AHomeModule* Crate)
{
    auto* Hero=Cast<ACFPlayerCharacter>(GetOwner());
    if(!CanAccess(Crate)) { if(Hero) Hero->Feedback=TEXT("仓储：请关闭其他面板，靠近3米内并保持视线畅通"); return false; }
    Hero->CancelMeleeAttack(); Target=Crate; Selection=0;
    Hero->Feedback=TEXT("仓储（世界运行中）| 上下选物品 | Enter 存1个 | 退格取1个 | 左右整组转移 | E 关闭"); return true;
}
bool UStorageInteractionComponent::TryOpenAimed()
{
    if(IsOpen()) { Close(); return true; }
    auto* Crate=TraceCrate(); if(!Crate) return false;
    Open(Crate); return true; // A failed storage interaction must not issue a work order.
}
AHomeModule* UStorageInteractionComponent::TraceCrate() const
{
    const auto* Hero=Cast<ACFPlayerCharacter>(GetOwner()); if(!Hero) return nullptr;
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(StorageAim),false,Hero);
    const FVector Start=Hero->FirstPersonCamera->GetComponentLocation();
    GetWorld()->LineTraceSingleByChannel(Hit,Start,Start+Hero->FirstPersonCamera->GetForwardVector()*800,ECC_Visibility,Params);
    auto* Crate=Cast<AHomeModule>(Hit.GetActor());
    return Crate && Crate->GetKind()==EHomeModuleKind::Storage && !Crate->bPreview && !Crate->bStaged?Crate:nullptr;
}
void UStorageInteractionComponent::Close() { Target.Reset(); Selection=0; }
bool UStorageInteractionComponent::IsOpen() const { return CanAccess(Target.Get()); }
AHomeModule* UStorageInteractionComponent::GetCrate() const { return IsOpen()?Target.Get():nullptr; }
void UStorageInteractionComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(DeltaTime,TickType,Tick);
    if(!Target.IsExplicitlyNull() && !IsOpen()) Close();
}
void UStorageInteractionComponent::Step(int32 Direction)
{
    if(IsOpen()) Selection=(Selection+(Direction>0?1:static_cast<int32>(EFrontierItem::Count)-1))%static_cast<int32>(EFrontierItem::Count);
}
bool UStorageInteractionComponent::Transfer(EFrontierItem Item,int32 Amount,bool Deposit)
{
    auto* Hero=Cast<ACFPlayerCharacter>(GetOwner()); auto* Crate=GetCrate();
    if(!Hero || !Crate || Item>=EFrontierItem::Count || Amount<=0) return false;
    auto* Source=Deposit?Hero->Inventory.Get():Crate->StorageInventory.Get();
    auto* Destination=Deposit?Crate->StorageInventory.Get():Hero->Inventory.Get();
    const bool Done=Source->TransferTo(Destination,{{Item,Amount}});
    Hero->Feedback=Done?FString::Printf(TEXT("仓储：%s%d个物品，双方库存已同步"),Deposit?TEXT("存入"):TEXT("取出"),Amount):TEXT("仓储未改变：库存不足或目标已满");
    return Done;
}
void UStorageInteractionComponent::DepositOne() { Transfer(static_cast<EFrontierItem>(Selection),1,true); }
void UStorageInteractionComponent::WithdrawOne() { Transfer(static_cast<EFrontierItem>(Selection),1,false); }
void UStorageInteractionComponent::DepositStack()
{
    if(auto* Hero=Cast<ACFPlayerCharacter>(GetOwner())) Transfer(static_cast<EFrontierItem>(Selection),Hero->Inventory->Count(static_cast<EFrontierItem>(Selection)),true);
}
void UStorageInteractionComponent::WithdrawStack()
{
    if(auto* Crate=GetCrate()) Transfer(static_cast<EFrontierItem>(Selection),Crate->StorageInventory->Count(static_cast<EFrontierItem>(Selection)),false);
}
