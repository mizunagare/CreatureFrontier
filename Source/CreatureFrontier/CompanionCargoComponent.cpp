#include "CompanionCargoComponent.h"
#include "CompanionApproach.h"
#include "CreatureCharacter.h"
#include "CFPlayerCharacter.h"
#include "HomeModule.h"
#include "ResourceNode.h"
#include "InventoryComponent.h"
#include "CraftingCatalog.h"
#include "FrontierSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Components/CapsuleComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

int32 UCompanionCargoComponent::Count() const
{
    const auto* Pet=Cast<ACreatureCharacter>(GetOwner()); int32 Total=0;
    if(Pet) for(const auto& Entry:Pet->CargoInventory->GetStacks()) Total+=Entry.Value;
    return Total;
}
AHomeModule* UCompanionCargoComponent::GetTarget() const
{
    auto* Target=Destination.Get(); return IsValid(Target) && !Target->IsActorBeingDestroyed()?Target:nullptr;
}
bool UCompanionCargoComponent::Assign(AHomeModule* Crate,ACFPlayerCharacter* Hero)
{
    auto* Pet=Cast<ACreatureCharacter>(GetOwner());
    if(!Pet || !Pet->HasAuthority() || Pet->Health<=0 || !Hero || Hero->Health<=0 || Pet->Tamer!=Hero->GetController() || Pet->Disposition!=ECreatureDisposition::Tamed ||
        UGameplayStatics::IsGamePaused(this) || !Pet->GetFieldResource() || Pet->CarriedFood>0 ||
        (Pet->Activity!=ECreatureActivity::Work && !(Pet->Activity==ECreatureActivity::Rest && Pet->bResumeWorkAfterRest)) ||
        !IsValid(Crate) || Crate->IsActorBeingDestroyed() || Crate->GetWorld()!=GetWorld() || Crate->bPreview || Crate->bStaged ||
        Crate->GetKind()!=EHomeModuleKind::Storage || !Crate->PersistentId.IsValid()) return false;
    // 玩家通过八米可见性射线选择目标；之后可以远离这个明确选定的
    // 接收设施，再下达资源任务。
    Pet->ResetGathering(); Pet->StopNavigation();
    Destination=Crate; bOrdered=true; bReturning=Count()>0; Expires=0; Retry=0; bFull=false; return true;
}
void UCompanionCargoComponent::CancelOrder() { Destination.Reset(); bOrdered=false; bReturning=false; bFull=false; Expires=0; Retry=0; }
bool UCompanionCargoComponent::ValidateOrder()
{
    auto* Pet=Cast<ACreatureCharacter>(GetOwner());
    if(!bOrdered || (GetTarget() && Pet && IsValid(Pet->GetFieldResource()) && !Pet->GetFieldResource()->IsActorBeingDestroyed())) return true;
    if(Pet) { Pet->StopNavigation(); Pet->SetActivity(ECreatureActivity::Follow); Pet->bResumeWorkAfterRest=false; }
    CancelOrder(); return false;
}
bool UCompanionCargoComponent::CanReceive(EFrontierItem Item) const
{
    const auto* Pet=Cast<ACreatureCharacter>(GetOwner());
    return Pet && bOrdered && GetTarget() && !bReturning && Pet->CarriedFood==0 && Count()<Capacity &&
        IsGatheredResource(Item);
}
bool UCompanionCargoComponent::Receive(EFrontierItem Item)
{
    auto* Pet=Cast<ACreatureCharacter>(GetOwner()); return CanReceive(Item) && Pet->CargoInventory->AddItem(Item,1);
}
bool UCompanionCargoComponent::TickDelivery(float DeltaSeconds,ACFPlayerCharacter* Hero)
{
    auto* Pet=Cast<ACreatureCharacter>(GetOwner()); auto* Crate=GetTarget();
    if(!bOrdered || !Pet || !Crate || !Hero || Hero->Health<=0 || UGameplayStatics::IsGamePaused(this)) return false;
    if(Count()==0) { bReturning=false; bFull=false; Expires=0; Retry=0; return false; }
    const auto* Node=Pet->GetFieldResource();
    if(!bReturning && Count()<Capacity && Node && Node->Remaining>0) return false;
    Pet->bGatheringActive=false;
    bReturning=true;
    Pet->Stamina=FMath::Max(0.f,Pet->Stamina-DeltaSeconds*1.5f);
    if(Pet->Stamina<=0) { Pet->bResumeWorkAfterRest=true; Pet->SetActivity(ECreatureActivity::Rest); Pet->StopNavigation(); return true; }
    Retry=FMath::Max(0.f,Retry-DeltaSeconds);
    if(Retry>0) { Pet->StopNavigation(); return true; }
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(CargoUnload),false,Pet);
    const bool Visible=!GetWorld()->LineTraceSingleByChannel(Hit,Pet->GetActorLocation(),Crate->GetActorLocation()+FVector(0,0,40),ECC_Visibility,Params) || Hit.GetActor()==Crate;
    if(Pet->GetDistanceTo(Crate)<=220 && Visible)
    {
        Pet->StopNavigation(); Expires=0;
        TMap<EFrontierItem,int32> Items;
        for(const auto& Entry:Pet->CargoInventory->GetStacks()) if(Entry.Value>0) Items.Add(Entry.Key,Entry.Value);
        if(Pet->CargoInventory->TransferTo(Crate->StorageInventory,Items))
        { bReturning=false; bFull=false; Hero->Feedback=TEXT("伙伴已把携带批次送入箱子，不重复获得工作经验"); }
        else { bFull=true; Retry=2; }
        return true;
    }
    bFull=false;
    if(Expires==0) Expires=GetWorld()->GetTimeSeconds()+8;
    if(GetWorld()->GetTimeSeconds()>Expires) { Expires=0; Retry=2; Pet->StopNavigation(); return true; }
    const double Now=GetWorld()->GetTimeSeconds();if(Now<Pet->NextNavigationUpdate)return true;
    FVector Best=FVector::ZeroVector;const bool Found=FindCompanionApproach(Pet,Crate,Best);
    if(Found)
    {
        if(FParse::Param(FCommandLine::Get(),TEXT("CFHomeExpansionTest")))UE_LOG(LogTemp,Display,TEXT("CF_CARGO_APPROACH complete=1 goal=%s"),*Best.ToString());
        Pet->NavigateTo(Best,15);
    }
    else{Pet->StopNavigation();Pet->NextNavigationUpdate=Now+.4;}
    return true;
}
bool UCompanionCargoComponent::Recover(ACFPlayerCharacter* Hero)
{
    auto* Pet=Cast<ACreatureCharacter>(GetOwner());
    if(!Pet || !Hero || Hero->Health<=0 || UGameplayStatics::IsGamePaused(this) || !Pet->HasAuthority() || Pet->Tamer!=Hero->GetController() ||
        Pet->Disposition!=ECreatureDisposition::Tamed || Hero->GetDistanceTo(Pet)>600 || Count()==0) return false;
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(CargoRecover),false,Hero);
    if(GetWorld()->LineTraceSingleByChannel(Hit,Hero->GetPawnViewLocation(),Pet->GetActorLocation(),ECC_Visibility,Params) && Hit.GetActor()!=Pet) return false;
    TMap<EFrontierItem,int32> Items;
    for(const auto& Entry:Pet->CargoInventory->GetStacks()) if(Entry.Value>0) Items.Add(Entry.Key,Entry.Value);
    if(!Pet->CargoInventory->TransferTo(Hero->Inventory,Items)) return false;
    bReturning=false; bFull=false; Expires=0; Retry=0; Hero->Feedback=TEXT("已取回携带资源，不额外生成物品或经验"); return true;
}
FString UCompanionCargoComponent::Status() const
{
    const auto* Pet=Cast<ACreatureCharacter>(GetOwner()); if(!Pet || (!bOrdered && Count()==0)) return FString();
    const auto* Hero=Pet->Tamer?Cast<ACFPlayerCharacter>(Pet->Tamer->GetPawn()):nullptr;
    const FString State=Pet->Health<=0?TEXT("已倒下，保留携带物"):Hero && Hero->Health<=0?TEXT("主人已倒下，携物暂停"):
        !bOrdered?TEXT("已保留，Z 取回"):Pet->Activity==ECreatureActivity::Rest?TEXT("携物休息中"):
        bFull?TEXT("箱子已满，保留当前批次"):Retry>0?TEXT("重试路径，保留当前批次"):bReturning?TEXT("正在运往储物箱"):TEXT("正在采集批次");
    return FString::Printf(TEXT("携带 %d/%d | %s | %s"),Count(),Capacity,*State,*UCraftingCatalog::FormatItems(Pet->CargoInventory->GetStacks()));
}
void UCompanionCargoComponent::WriteSnapshot(FCreatureSaveRecord& Record) const
{
    const auto* Pet=Cast<ACreatureCharacter>(GetOwner()); if(!Pet) return;
    for(const auto& Entry:Pet->CargoInventory->GetStacks()) if(Entry.Value>0) Record.CargoItems.Add(Entry.Key,Entry.Value);
    if(!bOrdered) return;
    const auto* Target=GetTarget(); const auto* Source=Pet->GetFieldResource();
    if(!Target || !IsValid(Source) || Source->IsActorBeingDestroyed())
    {
        // 即使目标在本帧 AI 更新前被删除，快照也要规范记录失去目标的任务意图。
        Record.FieldResourceId.Invalidate(); Record.bResumeWorkAfterRest=false;
        Record.Activity=Record.Health>0?ECreatureActivity::Follow:ECreatureActivity::Rest; return;
    }
    Record.DeliveryCrateId=Target->PersistentId; Record.bReturningCargo=bReturning && Count()>0;
}
bool UCompanionCargoComponent::ValidateSnapshot(const FCreatureSaveRecord& Record,AHomeModule* Target)
{
    if(!UInventoryComponent::Validate(Record.CargoItems)) return false;
    int32 Total=0;
    for(const auto& Entry:Record.CargoItems)
    { if(!IsGatheredResource(Entry.Key)) return false; Total+=Entry.Value; }
    if(Total>Capacity || (Total>0 && (!Record.bOwned || Record.Disposition!=ECreatureDisposition::Tamed || Record.CarriedFood>0))) return false;
    if(Record.bReturningCargo && (Total==0 || !Record.DeliveryCrateId.IsValid())) return false;
    if(Record.DeliveryCrateId.IsValid() && (!Target || Target->GetKind()!=EHomeModuleKind::Storage || !Record.bOwned || Record.Health<=0 ||
        Record.Disposition!=ECreatureDisposition::Tamed || Record.CarriedFood>0 || !Record.FieldResourceId.IsValid() ||
        Record.WorkSiteId.IsValid() || !Record.WorkSiteName.IsNone() ||
        (Record.Activity!=ECreatureActivity::Work && !(Record.Activity==ECreatureActivity::Rest && Record.bResumeWorkAfterRest)))) return false;
    return true;
}
void UCompanionCargoComponent::RestoreSnapshot(const FCreatureSaveRecord& Record,AHomeModule* Target)
{
    auto* Pet=Cast<ACreatureCharacter>(GetOwner()); Pet->CargoInventory->Restore(Record.CargoItems);
    Destination=Target; bOrdered=Record.DeliveryCrateId.IsValid(); bReturning=Record.bReturningCargo; bFull=false; Expires=0; Retry=0;
}
