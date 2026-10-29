#include "HomeBuildingComponent.h"
#include "HomeModule.h"
#include "HomeLayout.h"
#include "CFPlayerCharacter.h"
#include "WorkStation.h"
#include "InventoryComponent.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Components/StaticMeshComponent.h"
#include "ResearchComponent.h"
#include "CraftingCatalog.h"

namespace
{
FHomeModuleRecord Candidate(UHomeModuleDefinition* Definition,const FTransform& Transform,FGuid FloorId)
{
    FHomeModuleRecord Record; Record.Id=FGuid::NewGuid(); Record.Definition=Definition; Record.Transform=Transform; Record.FloorId=FloorId; Record.PaidMaterials=Definition?Definition->MaterialCost:0; if(Definition) {Record.PaidExtraItems=Definition->ExtraCosts;Record.DoorState=Definition->Kind==EHomeModuleKind::Door?1:0;} return Record;
}
}
void UHomeBuildingComponent::StartPreview()
{
    Cancel();
    const FTransform Transform;
    Preview=GetWorld()->SpawnActorDeferred<AHomeModule>(AHomeModule::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(Preview) { Preview->bPreview=true; Preview->Definition=UHomeModuleDefinition::LoadDefault(Selected); Preview->FinishSpawning(Transform); }
    UpdatePreview();
}
void UHomeBuildingComponent::Cancel() { PendingReplacement.Reset(); if(IsValid(Preview)) Preview->Destroy(); Preview=nullptr; bValid=false; }
void UHomeBuildingComponent::EndPlay(const EEndPlayReason::Type Reason) { Cancel(); Super::EndPlay(Reason); }
void UHomeBuildingComponent::Cycle()
{
    Selected=static_cast<EHomeModuleKind>((static_cast<int32>(Selected)+1)%8); StartPreview();
}
void UHomeBuildingComponent::Rotate() { PendingReplacement.Reset(); YawStep=1-YawStep; UpdatePreview(); }
bool UHomeBuildingComponent::ValidatePlacement(const FTransform& Transform,FGuid FloorId,FString& Reason) const
{
    const auto* Hero=Cast<ACFPlayerCharacter>(GetOwner()); auto* Definition=UHomeModuleDefinition::LoadDefault(Selected);
    if(!Hero || !Hero->HasAuthority() || Hero->Health<=0 || UGameplayStatics::IsGamePaused(this)) { Reason=TEXT("需要玩家存活且世界未暂停"); return false; }
    if(!Definition || !Definition->IsValidDefinition() || Hero->Inventory->Count(EFrontierItem::Material)<Definition->MaterialCost)
    { Reason=TEXT("定义缺失或材料不足"); return false; }
    if(!Hero->Research->Has(Definition->RequiredResearch)) { Reason=TEXT("尚未解锁，请在基地研究实际采集的水晶样本"); return false; }
    for(const auto& Cost:Definition->ExtraCosts) if(Hero->Inventory->Count(Cost.Key)<Cost.Value) { Reason=TEXT("需要")+UCraftingCatalog::FormatItems(Definition->ExtraCosts); return false; }
    if(FVector::Dist(Hero->GetActorLocation(),Transform.GetLocation())>800) { Reason=TEXT("距离过远（限8米）"); return false; }
    auto Layout=HomeLayout::Collect(GetWorld()); const auto Proposed=Candidate(Definition,Transform,FloorId); Layout.Add(Proposed);
    if(!HomeLayout::Validate(Layout,Reason)) return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(HomePlacement),false);
    // 规范吸附点允许地板与墙接触，以及垂直转角连接。
    // 其他碰撞仍然生效，尤其是角色、设施及预设掩体。
    for(TActorIterator<AHomeModule> It(GetWorld());It;++It) Params.AddIgnoredActor(*It);
    const auto Objects=FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects);
    for(const auto& Part:AHomeModule::PlacementParts(Selected))
    {
        const FVector Position=Transform.GetLocation()+Transform.GetRotation().RotateVector(Part.Center);
        if(GetWorld()->OverlapAnyTestByObjectType(Position,Transform.GetRotation(),Objects,FCollisionShape::MakeBox(Part.Extent-FVector(1)),Params))
        { Reason=TEXT("模块实体空间被地形、设施或生物占用"); return false; }
    }
    if(!HomeLayout::ValidateTerrain(GetWorld(),Proposed,Reason)) return false;
    Reason=TEXT("可以建造，支撑有效且空间畅通"); return true;
}
AHomeModule* UHomeBuildingComponent::BuildAt(const FTransform& Transform,FGuid FloorId)
{
    auto* Hero=Cast<ACFPlayerCharacter>(GetOwner()); FString Reason;
    if(!ValidatePlacement(Transform,FloorId,Reason)) { if(Hero) Hero->Feedback=Reason; return nullptr; }
    auto* Definition=UHomeModuleDefinition::LoadDefault(Selected);
    auto* Module=GetWorld()->SpawnActorDeferred<AHomeModule>(AHomeModule::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Module) return nullptr;
    Module->Definition=Definition; Module->PersistentId=FGuid::NewGuid(); Module->FloorId=FloorId; Module->PaidMaterials=Definition->MaterialCost; Module->DoorState=Selected==EHomeModuleKind::Door?1:0; Module->FinishSpawning(Transform);
    Module->PaidExtraItems=Definition->ExtraCosts;
    auto Costs=Definition->ExtraCosts; Costs.Add(EFrontierItem::Material,Definition->MaterialCost);
    if(!Hero->Inventory->Transact(Costs,{})) { Module->Destroy(); return nullptr; }
    Hero->Feedback=TEXT("房屋模块已建造 | X 拆除 | 移除最后支撑前请先拆屋顶"); return Module;
}
bool UHomeBuildingComponent::Dismantle(AHomeModule* Module)
{
    auto* Hero=Cast<ACFPlayerCharacter>(GetOwner());
    if(!Hero || !Hero->HasAuthority() || Hero->Health<=0 || UGameplayStatics::IsGamePaused(this) || !IsValid(Module) || Module->IsActorBeingDestroyed() || Module->bPreview || !Module->Definition || Hero->GetDistanceTo(Module)>600) return false;
    if(Module->GetKind()==EHomeModuleKind::Storage)
        for(const auto& Stack:Module->StorageInventory->GetStacks()) if(Stack.Value>0) { Hero->Feedback=TEXT("拆除前请清空储物箱"); return false; }
    for(const auto& Cost:Module->PaidExtraItems) if(Cost.Key==EFrontierItem::Material || Cost.Key>=EFrontierItem::Count || Cost.Value<=0 || Cost.Value>999) return false;
    auto Layout=HomeLayout::Collect(GetWorld()); Layout.RemoveAll([Module](const FHomeModuleRecord& Record){return Record.Id==Module->PersistentId;});
    FString Reason;
    if(!HomeLayout::Validate(Layout,Reason)) { Hero->Feedback=TEXT("无法移除支撑：")+Reason; return false; }
    for(TActorIterator<AWorkStation> It(GetWorld());It;++It) if(It->bPlayerBuilt && !HomeLayout::SupportsFacility(It->GetActorTransform(),Layout))
    { Hero->Feedback=TEXT("拆地板前请先移动或拆除其支撑的室内设施"); return false; }
    TMap<EFrontierItem,int32> Refunds={{EFrontierItem::Material,Module->PaidMaterials/2}};
    for(const auto& Cost:Module->PaidExtraItems) if(Cost.Value>=2) Refunds.Add(Cost.Key,Cost.Value/2);
    if(Module->PaidMaterials<2 || Module->PaidMaterials>100 || !Hero->Inventory->GrantItems(Refunds)) { Hero->Feedback=TEXT("请为全部拆除返还物腾出背包空间"); return false; }
    Module->Destroy(); Hero->Feedback=TEXT("已拆除房屋模块，返还一半材料"); return true;
}
AHomeModule* UHomeBuildingComponent::ReplaceWall(AHomeModule* Module)
{
    auto* Hero=Cast<ACFPlayerCharacter>(GetOwner());
    auto Reject=[Hero](const FString& Reason)->AHomeModule*{if(Hero)Hero->Feedback=Reason;return nullptr;};
    const auto IsWall=[](EHomeModuleKind Kind){return Kind==EHomeModuleKind::Wall || Kind==EHomeModuleKind::Window || Kind==EHomeModuleKind::Doorway || Kind==EHomeModuleKind::Door;};
    if(!IsInGameThread() || !Hero || !Hero->HasAuthority() || Hero->Health<=0 || UGameplayStatics::IsGamePaused(this) ||
       !IsValid(Module) || Module->GetWorld()!=GetWorld() || Module->IsActorBeingDestroyed() || Module->bPreview || Module->bStaged ||
       !Module->Definition || !IsWall(Module->GetKind()) || !IsWall(Selected) || Hero->GetDistanceTo(Module)>600)
        return Reject(TEXT("请在存活且未暂停时，选择6米内的墙类模块进行替换"));
    if(Module->GetKind()==Selected)return Reject(TEXT("同类模块无需替换"));
    auto* Definition=UHomeModuleDefinition::LoadDefault(Selected);
    if(!Definition || !Definition->IsValidDefinition() || !Hero->Research->Has(Definition->RequiredResearch))
        return Reject(TEXT("替换定义缺失或尚未解锁"));
    auto Layout=HomeLayout::Collect(GetWorld());
    auto* Old=Layout.FindByPredicate([Module](const FHomeModuleRecord& Record){return Record.Id==Module->PersistentId;});
    if(!Old || !HomeLayout::IsCanonical(*Old))return Reject(TEXT("原模块记录无效，未进行替换"));
    const auto RefundRecord=*Old;
    auto Proposed=Candidate(Definition,Module->GetActorTransform(),Module->FloorId);
    Proposed.Id=Module->PersistentId;
    *Old=Proposed;
    FString Reason;
    if(!HomeLayout::Validate(Layout,Reason) || !HomeLayout::ValidateTerrain(GetWorld(),Proposed,Reason))return Reject(Reason);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(HomeReplace),false);
    for(TActorIterator<AHomeModule> It(GetWorld());It;++It)Params.AddIgnoredActor(*It);
    const auto Transform=Module->GetActorTransform();
    const auto Objects=FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects);
    for(const auto& Part:AHomeModule::PlacementParts(Selected))
        if(GetWorld()->OverlapAnyTestByObjectType(Transform.TransformPosition(Part.Center),Transform.GetRotation(),Objects,FCollisionShape::MakeBox(Part.Extent-FVector(1)),Params))
            return Reject(TEXT("替换后实体空间被地形、设施或生物占用"));
    if(Selected==EHomeModuleKind::Door || Module->GetKind()==EHomeModuleKind::Door)
    {
        const auto Reserved=AHomeModule::DoorReservation(Transform);
        if(GetWorld()->OverlapAnyTestByObjectType(Reserved.GetCenter(),FQuat::Identity,Objects,FCollisionShape::MakeBox(Reserved.GetExtent()),Params))
            return Reject(TEXT("门的活动范围被占用，不能替换"));
    }
    auto Costs=Definition->ExtraCosts;Costs.Add(EFrontierItem::Material,Definition->MaterialCost);
    TMap<EFrontierItem,int32> Refunds={{EFrontierItem::Material,RefundRecord.PaidMaterials/2}};
    for(const auto& Cost:RefundRecord.PaidExtraItems)if(Cost.Value>=2)Refunds.Add(Cost.Key,Cost.Value/2);
    // 先构造无碰撞暂存实体；背包事务失败时，旧实体及其持久身份完全保留。
    auto* Replacement=GetWorld()->SpawnActorDeferred<AHomeModule>(AHomeModule::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Replacement)return Reject(TEXT("无法创建替换模块"));
    Replacement->bStaged=true;Replacement->Definition=Definition;Replacement->PersistentId=Proposed.Id;
    Replacement->FloorId=Proposed.FloorId;Replacement->PaidMaterials=Proposed.PaidMaterials;
    Replacement->PaidExtraItems=Proposed.PaidExtraItems;Replacement->DoorState=Proposed.DoorState;Replacement->FinishSpawning(Transform);
    const auto Before=Hero->Inventory->GetStacks();
    if(!Hero->Inventory->Transact(Costs,Refunds)){Replacement->Destroy();return Reject(TEXT("替换需要足额材料及返还物资的背包空间"));}
    if(!Module->Destroy()){Hero->Inventory->Restore(Before);Replacement->Destroy();return Reject(TEXT("旧模块不能移除，替换已取消"));}
    Replacement->ActivateInWorld();
    Hero->Feedback=TEXT("墙体已原位替换，保留支撑并返还旧模块一半材料");
    return Replacement;
}
void UHomeBuildingComponent::UpdatePreview()
{
    auto* Hero=Cast<ACFPlayerCharacter>(GetOwner()); if(!Hero || !Preview) return;
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(HomeAim),false,Hero); Params.AddIgnoredActor(Preview);
    const FVector Start=Hero->FirstPersonCamera->GetComponentLocation();
    if(!GetWorld()->LineTraceSingleByChannel(Hit,Start,Start+Hero->FirstPersonCamera->GetForwardVector()*800,ECC_Visibility,Params))
    {
        if(PendingReplacement.IsValid())UE_LOG(LogTemp,Display,TEXT("CF_HOME_REPLACE_CANCEL_TRACE miss target=%s camera=%s direction=%s"),*GetNameSafe(PendingReplacement.Get()),*Start.ToString(),*Hero->FirstPersonCamera->GetForwardVector().ToString());
        PendingReplacement.Reset(); Status=TEXT("请瞄准8米内地面或房屋地板"); bValid=false; Preview->SetActorHiddenInGame(true); return;
    }
    if(PendingReplacement.IsValid() && (Hit.GetActor()!=PendingReplacement.Get() || Selected!=PendingKind || GetWorld()->GetTimeSeconds()>ReplacementDeadline))
    {
        UE_LOG(LogTemp,Verbose,TEXT("CF_HOME_REPLACE_CANCEL_TRACE target=%s hit=%s kind=%d pendingKind=%d time=%.3f deadline=%.3f camera=%s direction=%s"),
            *GetNameSafe(PendingReplacement.Get()),*GetNameSafe(Hit.GetActor()),static_cast<int32>(Selected),static_cast<int32>(PendingKind),GetWorld()->GetTimeSeconds(),ReplacementDeadline,
            *Start.ToString(),*Hero->FirstPersonCamera->GetForwardVector().ToString());
        PendingReplacement.Reset();
    }
    FVector Center(FMath::GridSnap(Hit.ImpactPoint.X,400.),FMath::GridSnap(Hit.ImpactPoint.Y,400.),-40);
    const auto Layout=HomeLayout::Collect(GetWorld()); SupportingFloor=FGuid();
    if(Selected!=EHomeModuleKind::Floor)
    {
        if(auto* Aimed=Cast<AHomeModule>(Hit.GetActor());Aimed && !Aimed->bPreview)
        {
            SupportingFloor=Aimed->GetKind()==EHomeModuleKind::Floor?Aimed->PersistentId:Aimed->FloorId;
            for(const auto& Record:Layout) if(Record.Id==SupportingFloor) Center=Record.Transform.GetLocation();
        }
        else SupportingFloor=HomeLayout::FindFloor(Hit.ImpactPoint,Layout);
        if(!SupportingFloor.IsValid()) { Status=TEXT("请瞄准可支撑的地板、墙或门框"); bValid=false; Preview->SetActorHiddenInGame(true); return; }
    }
    const bool Wall=Selected==EHomeModuleKind::Wall || Selected==EHomeModuleKind::Doorway || Selected==EHomeModuleKind::Door || Selected==EHomeModuleKind::Window;
    FVector Position=Center;
    if(Wall)
    {
        if(YawStep==0) Position.Y+=Hit.ImpactPoint.Y>=Center.Y?200:-200;
        else Position.X+=Hit.ImpactPoint.X>=Center.X?200:-200;
        Position.Z=-30;
    }
    else Position.Z=Selected==EHomeModuleKind::Floor?-40:280;
    if(Selected==EHomeModuleKind::Lantern || Selected==EHomeModuleKind::Storage) Position=FVector(FMath::GridSnap(Hit.ImpactPoint.X,100.),FMath::GridSnap(Hit.ImpactPoint.Y,100.),-30);
    Placement=FTransform(FRotator(0,Wall?YawStep*90:0,0),Position);
    bValid=ValidatePlacement(Placement,SupportingFloor,Status);
    Preview->SetActorTransform(Placement); Preview->SetActorHiddenInGame(PendingReplacement.IsValid()); Preview->SetPreviewTint(bValid);
    if(PendingReplacement.IsValid())Status=TEXT("原位替换待确认；再按Insert确认，移开视线取消");
}
void UHomeBuildingComponent::Confirm() { PendingReplacement.Reset(); UpdatePreview(); if(bValid) BuildAt(Placement,SupportingFloor); }
void UHomeBuildingComponent::ConfirmAimedReplacement()
{
    auto* Hero=Cast<ACFPlayerCharacter>(GetOwner());
    if(!Hero || !IsValid(Preview) || Hero->Health<=0 || !Hero->HasAuthority() || UGameplayStatics::IsGamePaused(this)){PendingReplacement.Reset();return;}
    FHitResult Hit;FCollisionQueryParams Params(SCENE_QUERY_STAT(HomeReplaceAim),false,Hero);Params.AddIgnoredActor(Preview);
    const FVector Start=Hero->FirstPersonCamera->GetComponentLocation();
    const bool Visible=GetWorld()->LineTraceSingleByChannel(Hit,Start,Start+Hero->FirstPersonCamera->GetForwardVector()*600,ECC_Visibility,Params);
    auto* Target=Visible?Cast<AHomeModule>(Hit.GetActor()):nullptr;
    const auto Wall=[](EHomeModuleKind Kind){return Kind==EHomeModuleKind::Wall || Kind==EHomeModuleKind::Window || Kind==EHomeModuleKind::Door || Kind==EHomeModuleKind::Doorway;};
    if(!Target || Target->bPreview || !Wall(Target->GetKind()) || !Wall(Selected) || Target->GetKind()==Selected)
    {PendingReplacement.Reset();Hero->Feedback=TEXT("替换：选择不同墙类，瞄准6米内旧墙实体，再按Insert");return;}
    if(PendingReplacement.Get()==Target && PendingKind==Selected && GetWorld()->GetTimeSeconds()<=ReplacementDeadline)
    {PendingReplacement.Reset();ReplaceWall(Target);UpdatePreview();return;}
    auto* Definition=UHomeModuleDefinition::LoadDefault(Selected);
    if(!Definition || !Definition->IsValidDefinition()){PendingReplacement.Reset();return;}
    auto Costs=Definition->ExtraCosts;Costs.Add(EFrontierItem::Material,Definition->MaterialCost);
    TMap<EFrontierItem,int32> Refunds={{EFrontierItem::Material,Target->PaidMaterials/2}};
    for(const auto& Cost:Target->PaidExtraItems)if(Cost.Value>=2)Refunds.Add(Cost.Key,Cost.Value/2);
    PendingReplacement=Target;PendingKind=Selected;ReplacementDeadline=GetWorld()->GetTimeSeconds()+5;
    Hero->Feedback=FString::Printf(TEXT("替换为%s：消耗%s，返还%s。保持瞄准，5秒内再按Insert确认；移开视线取消"),*Definition->DisplayName.ToString(),*UCraftingCatalog::FormatItems(Costs),*UCraftingCatalog::FormatItems(Refunds));
}
FString UHomeBuildingComponent::GetHint() const
{
    const auto* Definition=UHomeModuleDefinition::LoadDefault(Selected);
    TMap<EFrontierItem,int32> Costs;
    if(Definition) { Costs=Definition->ExtraCosts; Costs.Add(EFrontierItem::Material,Definition->MaterialCost); }
    return FString::Printf(TEXT("房屋 %s | %s\nV选择 | 9旋转 | 8设施 | Insert替换\n左键放置 | 右键/B取消 | X拆除\n%s"),Definition?*Definition->DisplayName.ToString():TEXT("不可用"),*UCraftingCatalog::FormatItems(Costs),*Status);
}
