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

namespace
{
FHomeModuleRecord Candidate(UHomeModuleDefinition* Definition,const FTransform& Transform,FGuid FloorId)
{
    FHomeModuleRecord Record; Record.Id=FGuid::NewGuid(); Record.Definition=Definition; Record.Transform=Transform; Record.FloorId=FloorId; Record.PaidMaterials=Definition?Definition->MaterialCost:0; return Record;
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
void UHomeBuildingComponent::Cancel() { if(IsValid(Preview)) Preview->Destroy(); Preview=nullptr; bValid=false; }
void UHomeBuildingComponent::EndPlay(const EEndPlayReason::Type Reason) { Cancel(); Super::EndPlay(Reason); }
void UHomeBuildingComponent::Cycle()
{
    Selected=static_cast<EHomeModuleKind>((static_cast<int32>(Selected)+1)%4); StartPreview();
}
void UHomeBuildingComponent::Rotate() { YawStep=1-YawStep; UpdatePreview(); }
bool UHomeBuildingComponent::ValidatePlacement(const FTransform& Transform,FGuid FloorId,FString& Reason) const
{
    const auto* Hero=Cast<ACFPlayerCharacter>(GetOwner()); auto* Definition=UHomeModuleDefinition::LoadDefault(Selected);
    if(!Hero || !Hero->HasAuthority() || Hero->Health<=0 || UGameplayStatics::IsGamePaused(this)) { Reason=TEXT("Need a living, unpaused player"); return false; }
    if(!Definition || !Definition->IsValidDefinition() || Hero->Inventory->Count(EFrontierItem::Material)<Definition->MaterialCost)
    { Reason=TEXT("Missing definition or not enough materials"); return false; }
    if(FVector::Dist(Hero->GetActorLocation(),Transform.GetLocation())>800) { Reason=TEXT("Too far (8m)"); return false; }
    auto Layout=HomeLayout::Collect(GetWorld()); const auto Proposed=Candidate(Definition,Transform,FloorId); Layout.Add(Proposed);
    if(!HomeLayout::Validate(Layout,Reason)) return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(HomePlacement),false);
    // Canonical sockets permit floor/wall contact and perpendicular corner joints.
    // Other collisions, especially pawns, facilities and authored cover, remain real.
    for(TActorIterator<AHomeModule> It(GetWorld());It;++It) Params.AddIgnoredActor(*It);
    const auto Objects=FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects);
    for(const auto& Part:AHomeModule::Parts(Selected))
    {
        const FVector Position=Transform.GetLocation()+Transform.GetRotation().RotateVector(Part.Center);
        if(GetWorld()->OverlapAnyTestByObjectType(Position,Transform.GetRotation(),Objects,FCollisionShape::MakeBox(Part.Extent-FVector(1)),Params))
        { Reason=TEXT("Module solid volume occupied by terrain, facility or creature"); return false; }
    }
    if(!HomeLayout::ValidateTerrain(GetWorld(),Proposed,Reason)) return false;
    Reason=TEXT("Ready - supported and clear"); return true;
}
AHomeModule* UHomeBuildingComponent::BuildAt(const FTransform& Transform,FGuid FloorId)
{
    auto* Hero=Cast<ACFPlayerCharacter>(GetOwner()); FString Reason;
    if(!ValidatePlacement(Transform,FloorId,Reason)) { if(Hero) Hero->Feedback=Reason; return nullptr; }
    auto* Definition=UHomeModuleDefinition::LoadDefault(Selected);
    auto* Module=GetWorld()->SpawnActorDeferred<AHomeModule>(AHomeModule::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Module) return nullptr;
    Module->Definition=Definition; Module->PersistentId=FGuid::NewGuid(); Module->FloorId=FloorId; Module->PaidMaterials=Definition->MaterialCost; Module->FinishSpawning(Transform);
    if(!Hero->Inventory->Consume(EFrontierItem::Material,Definition->MaterialCost)) { Module->Destroy(); return nullptr; }
    Hero->Feedback=TEXT("Home module built | X dismantle | remove roof before its last supports"); return Module;
}
bool UHomeBuildingComponent::Dismantle(AHomeModule* Module)
{
    auto* Hero=Cast<ACFPlayerCharacter>(GetOwner());
    if(!Hero || !Hero->HasAuthority() || Hero->Health<=0 || UGameplayStatics::IsGamePaused(this) || !IsValid(Module) || Module->IsActorBeingDestroyed() || Module->bPreview || !Module->Definition || Hero->GetDistanceTo(Module)>600) return false;
    auto Layout=HomeLayout::Collect(GetWorld()); Layout.RemoveAll([Module](const FHomeModuleRecord& Record){return Record.Id==Module->PersistentId;});
    FString Reason;
    if(!HomeLayout::Validate(Layout,Reason)) { Hero->Feedback=TEXT("Cannot remove support: ")+Reason; return false; }
    for(TActorIterator<AWorkStation> It(GetWorld());It;++It) if(It->bPlayerBuilt && !HomeLayout::SupportsFacility(It->GetActorTransform(),Layout))
    { Hero->Feedback=TEXT("Move/dismantle the indoor facility before its supporting floor"); return false; }
    if(Module->PaidMaterials<2 || Module->PaidMaterials>100 || !Hero->Inventory->AddItem(EFrontierItem::Material,Module->PaidMaterials/2)) { Hero->Feedback=TEXT("Make inventory space for dismantle refund"); return false; }
    Module->Destroy(); Hero->Feedback=TEXT("Home module dismantled; half materials returned"); return true;
}
void UHomeBuildingComponent::UpdatePreview()
{
    auto* Hero=Cast<ACFPlayerCharacter>(GetOwner()); if(!Hero || !Preview) return;
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(HomeAim),false,Hero); Params.AddIgnoredActor(Preview);
    const FVector Start=Hero->FirstPersonCamera->GetComponentLocation();
    if(!GetWorld()->LineTraceSingleByChannel(Hit,Start,Start+Hero->FirstPersonCamera->GetForwardVector()*800,ECC_Visibility,Params))
    { Status=TEXT("Aim at ground or a home floor (8m)"); bValid=false; Preview->SetActorHiddenInGame(true); return; }
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
        if(!SupportingFloor.IsValid()) { Status=TEXT("Aim at a supporting floor, wall or doorway"); bValid=false; Preview->SetActorHiddenInGame(true); return; }
    }
    const bool Wall=Selected==EHomeModuleKind::Wall || Selected==EHomeModuleKind::Doorway;
    FVector Position=Center;
    if(Wall)
    {
        if(YawStep==0) Position.Y+=Hit.ImpactPoint.Y>=Center.Y?200:-200;
        else Position.X+=Hit.ImpactPoint.X>=Center.X?200:-200;
        Position.Z=-30;
    }
    else Position.Z=Selected==EHomeModuleKind::Floor?-40:280;
    Placement=FTransform(FRotator(0,Wall?YawStep*90:0,0),Position);
    bValid=ValidatePlacement(Placement,SupportingFloor,Status);
    Preview->SetActorTransform(Placement); Preview->SetActorHiddenInGame(false); Preview->SetPreviewTint(bValid);
}
void UHomeBuildingComponent::Confirm() { UpdatePreview(); if(bValid) BuildAt(Placement,SupportingFloor); }
FString UHomeBuildingComponent::GetHint() const
{
    const auto* Definition=UHomeModuleDefinition::LoadDefault(Selected);
    return FString::Printf(TEXT("HOME %s (%d) | V select | 9 rotate | 8 facilities\n%s"),Definition?*Definition->DisplayName.ToString():TEXT("unavailable"),Definition?Definition->MaterialCost:0,*Status);
}
