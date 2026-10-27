#include "BaseBuildingComponent.h"
#include "CFPlayerCharacter.h"
#include "Camera/CameraComponent.h"
#include "InventoryComponent.h"
#include "WorkStation.h"
#include "CreatureCharacter.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"

UBaseBuildingComponent::UBaseBuildingComponent() { PrimaryComponentTick.bCanEverTick = true; }

int32 UBaseBuildingComponent::GetCost(EWorkSpecialty Specialty)
{
    return Specialty == EWorkSpecialty::Gathering ? 10 : Specialty == EWorkSpecialty::Mining ? 12 : Specialty == EWorkSpecialty::Crafting ? 15 : Specialty == EWorkSpecialty::Farming ? 18 : Specialty == EWorkSpecialty::Feeding ? 12 : Specialty==EWorkSpecialty::Transport ? 16 : 0;
}

bool UBaseBuildingComponent::IsBuildTransformValid(const FTransform& Transform)
{
    const FVector Position = Transform.GetLocation();
    const FRotator Rotation = Transform.Rotator();
    return !Transform.ContainsNaN() && Transform.GetScale3D().Equals(FVector(2,1,.7),.001) &&
        FVector::Dist2D(Position, BaseOrigin()) <= 1200 && FMath::Abs(Position.Z - (BaseOrigin().Z + 35)) <= 2 &&
        FMath::IsNearlyZero(Rotation.Pitch,.01) && FMath::IsNearlyZero(Rotation.Roll,.01) &&
        FMath::IsNearlyEqual(Rotation.Yaw, FMath::GridSnap(Rotation.Yaw,90.0),.01) &&
        FMath::IsNearlyEqual(Position.X,FMath::GridSnap(Position.X,100.0),.01) &&
        FMath::IsNearlyEqual(Position.Y,FMath::GridSnap(Position.Y,100.0),.01);
}

void UBaseBuildingComponent::Toggle()
{
    auto* Hero = Cast<ACFPlayerCharacter>(GetOwner());
    if(Hero) Hero->CancelMeleeAttack();
    if (bActive) { Cancel(); return; }
    if (!Hero || Hero->Health <= 0 || !Hero->HasAuthority()) return;
    bActive = true;
    Preview = GetWorld()->SpawnActor<AStaticMeshActor>();
    if (Preview)
    {
        auto* Mesh = Preview->GetStaticMeshComponent();
        Mesh->SetMobility(EComponentMobility::Movable);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetCanEverAffectNavigation(false);
        Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        PreviewMaterial = UMaterialInstanceDynamic::Create(LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_CreaturePalette.M_CreaturePalette")), this);
        Mesh->SetMaterial(0, PreviewMaterial);
    }
    UpdatePlacement();
    Hero->Feedback = TEXT("Build mode: V select | LMB place | RMB/B cancel | X dismantle");
}

void UBaseBuildingComponent::Cancel()
{
    bActive = false; bValid = false;
    if (IsValid(Preview)) Preview->Destroy();
    Preview = nullptr; PreviewMaterial = nullptr;
}

void UBaseBuildingComponent::EndPlay(const EEndPlayReason::Type Reason) { Cancel(); Super::EndPlay(Reason); }

void UBaseBuildingComponent::Cycle()
{
    if (!bActive) return;
    Selected = Selected == EWorkSpecialty::Gathering ? EWorkSpecialty::Mining : Selected == EWorkSpecialty::Mining ? EWorkSpecialty::Crafting :
        Selected == EWorkSpecialty::Crafting ? EWorkSpecialty::Farming : Selected == EWorkSpecialty::Farming ? EWorkSpecialty::Feeding : Selected==EWorkSpecialty::Feeding ? EWorkSpecialty::Transport : EWorkSpecialty::Gathering;
    UpdatePlacement();
}

bool UBaseBuildingComponent::ValidatePlacement(const FTransform& Transform, FString& Reason) const
{
    auto* Hero = Cast<ACFPlayerCharacter>(GetOwner());
    if (!Hero || !Hero->HasAuthority() || Hero->Health <= 0) { Reason = TEXT("Need a living player"); return false; }
    if (!IsBuildTransformValid(Transform)) { Reason = TEXT("Use level ground within 12m of the original base"); return false; }
    if (FVector::Dist(Hero->GetActorLocation(),Transform.GetLocation()) > 800) { Reason = TEXT("Too far (8m)"); return false; }
    int32 Built = 0;
    for (TActorIterator<AWorkStation> It(GetWorld()); It; ++It) if (It->bPlayerBuilt) ++Built;
    if (Built >= 20) { Reason = TEXT("Base limit: 20 placed facilities"); return false; }
    const int32 Cost = GetCost(Selected);
    if (Cost == 0 || Hero->Inventory->Count(EFrontierItem::Material) < Cost) { Reason = TEXT("Not enough materials"); return false; }
    FCollisionQueryParams Params(SCENE_QUERY_STAT(BuildPlacement), false);
    if (IsValid(Preview)) Params.AddIgnoredActor(Preview);
    // Query full solid volume, including pawns. Keep the supporting floor below the box.
    const FCollisionObjectQueryParams Objects(FCollisionObjectQueryParams::AllObjects);
    if (GetWorld()->OverlapAnyTestByObjectType(Transform.GetLocation()+FVector(0,0,1),Transform.GetRotation(),Objects,
        FCollisionShape::MakeBox(FVector(112,62,34)),Params)) { Reason = TEXT("Space occupied - move away from creatures and buildings"); return false; }
    // All four corners must have flat support: no bridging a ledge or stacking benches.
    for (float X : {-95.0f,95.0f}) for (float Y : {-45.0f,45.0f})
    {
        const FVector Corner = Transform.GetLocation() + Transform.GetRotation().RotateVector(FVector(X,Y,0));
        FHitResult Ground;
        if (!GetWorld()->LineTraceSingleByChannel(Ground,Corner+FVector(0,0,10),Corner-FVector(0,0,45),ECC_Visibility,Params) ||
            Ground.ImpactNormal.Z < .98 || FMath::Abs(Ground.ImpactPoint.Z - BaseOrigin().Z) > 2)
        { Reason = TEXT("Need flat support under every corner"); return false; }
    }
    Reason = TEXT("Ready to build");
    return true;
}

bool UBaseBuildingComponent::UpdatePlacement()
{
    auto* Hero = Cast<ACFPlayerCharacter>(GetOwner());
    if (!Hero) return false;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(BuildAim),false,Hero);
    if (IsValid(Preview)) Params.AddIgnoredActor(Preview);
    const FVector Start = Hero->FirstPersonCamera->GetComponentLocation();
    const bool Ground = GetWorld()->LineTraceSingleByChannel(Hit,Start,Start+Hero->FirstPersonCamera->GetForwardVector()*800,ECC_Visibility,Params);
    if (!Ground) { Status = TEXT("Aim at the ground within 8m"); bValid = false; if (Preview) Preview->SetActorHiddenInGame(true); return false; }
    const FVector Position(FMath::GridSnap(Hit.ImpactPoint.X,100.0),FMath::GridSnap(Hit.ImpactPoint.Y,100.0),Hit.ImpactPoint.Z+35);
    Placement = FTransform(FRotator(0,FMath::GridSnap(static_cast<double>(Hero->GetControlRotation().Yaw),90.0),0),Position,FVector(2,1,.7));
    bValid = ValidatePlacement(Placement,Status);
    if (Preview) { Preview->SetActorTransform(Placement); Preview->SetActorHiddenInGame(false); }
    if (PreviewMaterial) PreviewMaterial->SetVectorParameterValue(TEXT("Tint"),bValid ? FLinearColor(.08f,.8f,.15f) : FLinearColor(.9f,.04f,.02f));
    return bValid;
}

void UBaseBuildingComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(DeltaTime,TickType,Tick);
    if (!bActive) return;
    const auto* Hero = Cast<ACFPlayerCharacter>(GetOwner());
    if (!Hero || Hero->Health <= 0) { Cancel(); return; }
    UpdatePlacement();
}

AWorkStation* UBaseBuildingComponent::BuildAt(const FTransform& Transform)
{
    auto* Hero = Cast<ACFPlayerCharacter>(GetOwner());
    FString Reason;
    if (!ValidatePlacement(Transform,Reason)) { if (Hero) Hero->Feedback = Reason; return nullptr; }
    auto* Site = GetWorld()->SpawnActorDeferred<AWorkStation>(AWorkStation::StaticClass(),Transform,nullptr,nullptr,
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn,ESpawnActorScaleMethod::OverrideRootScale);
    if (!Site) return nullptr;
    Site->RequiredSpecialty = Selected; Site->bPlayerBuilt = true; Site->PersistentId = FGuid::NewGuid();
    Site->FinishSpawning(Transform);
    if (!Hero->Inventory->Consume(EFrontierItem::Material,GetCost(Selected))) { Site->Destroy(); return nullptr; }
    Hero->Feedback = TEXT("Facility built. E: assign companions | X: dismantle aimed facility");
    return Site;
}

void UBaseBuildingComponent::Confirm() { if (bActive && UpdatePlacement()) BuildAt(Placement); }

bool UBaseBuildingComponent::DismantleStation(AWorkStation* Station)
{
    auto* Hero = Cast<ACFPlayerCharacter>(GetOwner());
    if (!Hero || !Hero->HasAuthority() || Hero->Health <= 0 || !IsValid(Station) || !Station->bPlayerBuilt || Hero->GetDistanceTo(Station) > 600) return false;
    const int32 Refund = GetCost(Station->RequiredSpecialty)/2;
    if (Station->StoredFood > 0) { Hero->Feedback = TEXT("Withdraw food with O before dismantling the facility"); return false; }
    if (!Hero->Inventory->AddItem(EFrontierItem::Material,Refund)) { Hero->Feedback = TEXT("Make inventory space for dismantle refund"); return false; }
    for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
        if (It->WorkSite == Station) { It->WorkSite = nullptr; It->SetActivity(ECreatureActivity::Follow); }
    Station->Destroy();
    Hero->Feedback = FString::Printf(TEXT("Dismantled: returned %d materials; workers now following"),Refund);
    return true;
}

void UBaseBuildingComponent::Dismantle()
{
    auto* Hero = Cast<ACFPlayerCharacter>(GetOwner());
    if (!Hero) return;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(DismantleAim),false,Hero);
    if (IsValid(Preview)) Params.AddIgnoredActor(Preview);
    const FVector Start = Hero->FirstPersonCamera->GetComponentLocation();
    GetWorld()->LineTraceSingleByChannel(Hit,Start,Start+Hero->FirstPersonCamera->GetForwardVector()*600,ECC_Visibility,Params);
    auto* Site = Cast<AWorkStation>(Hit.GetActor());
    if (!Site || !Site->bPlayerBuilt) Hero->Feedback = TEXT("Aim at a player-built facility within 6m (original stations are protected)");
    else DismantleStation(Site);
}

FString UBaseBuildingComponent::GetHint() const
{
    return FString::Printf(TEXT("BUILD %s (%d materials) | V cycle | LMB place | RMB cancel\n%s"),
        *UEnum::GetValueAsString(Selected),GetCost(Selected),*Status);
}
