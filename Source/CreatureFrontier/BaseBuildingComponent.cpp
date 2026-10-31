#include "BaseBuildingComponent.h"
#include "FrontierChinese.h"
#include "FarmCropComponent.h"
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
#include "HomeBuildingComponent.h"
#include "HomeModule.h"
#include "HomeLayout.h"

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
        FVector::Dist2D(Position, BaseOrigin()) <= 1200 && (FMath::Abs(Position.Z - (BaseOrigin().Z + 35)) <= 2 || HomeLayout::IsFloorHeight(Position.Z-45)) &&
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
    Hero->Feedback = TEXT("建造：8 设施/房屋 | V 选择 | 左键放置 | 右键/B取消 | X 拆除");
}

void UBaseBuildingComponent::Cancel()
{
    if(auto* Hero=Cast<ACFPlayerCharacter>(GetOwner())) Hero->HomeBuilding->Cancel();
    bHomeMode=false;
    bActive = false; bValid = false;bManualFacilityRotation=false;FacilityYaw=0;
    if (IsValid(Preview)) Preview->Destroy();
    Preview = nullptr; PreviewMaterial = nullptr;
}

void UBaseBuildingComponent::EndPlay(const EEndPlayReason::Type Reason) { Cancel(); Super::EndPlay(Reason); }

void UBaseBuildingComponent::Cycle()
{
    if (!bActive) return;
    if(bHomeMode) { CastChecked<ACFPlayerCharacter>(GetOwner())->HomeBuilding->Cycle(); return; }
    Selected = Selected == EWorkSpecialty::Gathering ? EWorkSpecialty::Mining : Selected == EWorkSpecialty::Mining ? EWorkSpecialty::Crafting :
        Selected == EWorkSpecialty::Crafting ? EWorkSpecialty::Farming : Selected == EWorkSpecialty::Farming ? EWorkSpecialty::Feeding : Selected==EWorkSpecialty::Feeding ? EWorkSpecialty::Transport : EWorkSpecialty::Gathering;
    UpdatePlacement();
}

void UBaseBuildingComponent::SwitchCategory()
{
    if(!bActive) return;
    auto* Hero=CastChecked<ACFPlayerCharacter>(GetOwner()); bHomeMode=!bHomeMode;
    if(Preview) Preview->SetActorHiddenInGame(bHomeMode);
    if(bHomeMode) Hero->HomeBuilding->StartPreview(); else { Hero->HomeBuilding->Cancel(); UpdatePlacement(); }
}
void UBaseBuildingComponent::RotateHome()
{
    auto* Hero=Cast<ACFPlayerCharacter>(GetOwner());if(!bActive || !Hero || Hero->Health<=0 || !Hero->HasAuthority())return;
    if(bHomeMode){Hero->HomeBuilding->Rotate();return;}
    // 未手动旋转时保留原视角吸附；按9后锁定朝向，瞄准与移动只改变位置。
    const float Previous=bManualFacilityRotation?FacilityYaw:FMath::GridSnap(Hero->GetControlRotation().Yaw,90.f);
    FacilityYaw=FMath::UnwindDegrees(Previous+90);bManualFacilityRotation=true;UpdatePlacement();
}
void UBaseBuildingComponent::ReplaceHome()
{
    if(bActive && bHomeMode)CastChecked<ACFPlayerCharacter>(GetOwner())->HomeBuilding->ConfirmAimedReplacement();
}

bool UBaseBuildingComponent::ValidatePlacement(const FTransform& Transform, FString& Reason) const
{
    auto* Hero = Cast<ACFPlayerCharacter>(GetOwner());
    if (!Hero || !Hero->HasAuthority() || Hero->Health <= 0) { Reason = TEXT("需要存活的玩家"); return false; }
    if (!IsBuildTransformValid(Transform)) { Reason = TEXT("请使用初始基地12米内的平地"); return false; }
    if (FVector::Dist(Hero->GetActorLocation(),Transform.GetLocation()) > 800) { Reason = TEXT("距离过远（限8米）"); return false; }
    int32 Built = 0;
    for (TActorIterator<AWorkStation> It(GetWorld()); It; ++It) if (It->bPlayerBuilt) ++Built;
    if (Built >= 20) { Reason = TEXT("基地最多放置20个设施"); return false; }
    const int32 Cost = GetCost(Selected);
    if (Cost == 0 || Hero->Inventory->Count(EFrontierItem::Material) < Cost) { Reason = TEXT("材料不足"); return false; }
    if(!HomeLayout::ClearsDoorReservations(FBox(FVector(-112,-62,-33),FVector(112,62,35)).TransformBy(Transform.ToMatrixWithScale()),HomeLayout::Collect(GetWorld())))
    {Reason=TEXT("设施不能占用门扇活动空间");return false;}
    // 与读档使用同一实际设施包围盒，不能把已换算的世界尺寸再次乘以缩放。
    if(!HomeLayout::ClearsStairReservations(FBox(FVector(-50),FVector(50)).TransformBy(Transform.ToMatrixWithScale()),HomeLayout::Collect(GetWorld())))
    {Reason=TEXT("设施不能占用楼梯踏步或通行净空");return false;}
    FCollisionQueryParams Params(SCENE_QUERY_STAT(BuildPlacement), false);
    if (IsValid(Preview)) Params.AddIgnoredActor(Preview);
    // 检查包括角色在内的完整实体体积，让支撑地面位于检测盒下方。
    const FCollisionObjectQueryParams Objects(FCollisionObjectQueryParams::AllObjects);
    if (GetWorld()->OverlapAnyTestByObjectType(Transform.GetLocation()+FVector(0,0,1),Transform.GetRotation(),Objects,
        FCollisionShape::MakeBox(FVector(112,62,34)),Params)) { Reason = TEXT("空间被占用，请避开生物与建筑"); return false; }
    // 四角都必须有平坦支撑，不允许跨越悬边或叠放工作台。
    for (float X : {-95.0f,95.0f}) for (float Y : {-45.0f,45.0f})
    {
        const FVector Corner = Transform.GetLocation() + Transform.GetRotation().RotateVector(FVector(X,Y,0));
        FHitResult Ground;
        if (!GetWorld()->LineTraceSingleByChannel(Ground,Corner+FVector(0,0,10),Corner-FVector(0,0,45),ECC_Visibility,Params) ||
            Ground.ImpactNormal.Z < .98 || !((FMath::Abs(Ground.ImpactPoint.Z - BaseOrigin().Z) <= 2 && FMath::Abs(Transform.GetLocation().Z+15)<=2) ||
                (HomeLayout::IsFloorHeight(Transform.GetLocation().Z-45) && FMath::IsNearlyEqual(Ground.ImpactPoint.Z,Transform.GetLocation().Z-35,.01) &&
                 Cast<AHomeModule>(Ground.GetActor()) && CastChecked<AHomeModule>(Ground.GetActor())->GetKind()==EHomeModuleKind::Floor)))
        { Reason = TEXT("每个角下方都需要平坦支撑"); return false; }
    }
    Reason = TEXT("可以建造");
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
    if (!Ground) { Status = TEXT("请瞄准8米内的地面"); bValid = false; if (Preview) Preview->SetActorHiddenInGame(true); return false; }
    const FVector Position(FMath::GridSnap(Hit.ImpactPoint.X,100.0),FMath::GridSnap(Hit.ImpactPoint.Y,100.0),Hit.ImpactPoint.Z+35);
    const double Yaw=bManualFacilityRotation?FacilityYaw:FMath::GridSnap(static_cast<double>(Hero->GetControlRotation().Yaw),90.0);
    Placement = FTransform(FRotator(0,Yaw,0),Position,FVector(2,1,.7));
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
    if(bHomeMode) { Hero->HomeBuilding->UpdatePreview(); return; }
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
    Hero->Feedback = TEXT("设施已建造。E 指派伙伴 | X 拆除瞄准的设施");
    return Site;
}

void UBaseBuildingComponent::Confirm()
{
    if(!bActive) return;
    if(bHomeMode) { CastChecked<ACFPlayerCharacter>(GetOwner())->HomeBuilding->Confirm(); return; }
    if(UpdatePlacement()) BuildAt(Placement);
}

bool UBaseBuildingComponent::DismantleStation(AWorkStation* Station)
{
    auto* Hero = Cast<ACFPlayerCharacter>(GetOwner());
    if (!Hero || !Hero->HasAuthority() || Hero->Health <= 0 || !IsValid(Station) || !Station->bPlayerBuilt || Hero->GetDistanceTo(Station) > 600) return false;
    const int32 Refund = GetCost(Station->RequiredSpecialty)/2;
    if (Station->StoredFood > 0) { Hero->Feedback = TEXT("拆除前请按O取出设施里的食物"); return false; }
    if(Station->Crop->IsOccupied()) {Hero->Feedback=TEXT("拆除前请收获作物并按F3取出预留种子");return false;}
    if (!Hero->Inventory->AddItem(EFrontierItem::Material,Refund)) { Hero->Feedback = TEXT("请为拆除返还物腾出背包空间"); return false; }
    for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
        if (It->WorkSite == Station) { It->WorkSite = nullptr; It->SetActivity(ECreatureActivity::Follow); }
    Station->Destroy();
    Hero->Feedback = FString::Printf(TEXT("已拆除，返还%d份材料；原工人改为跟随"),Refund);
    return true;
}

void UBaseBuildingComponent::Dismantle()
{
    auto* Hero = Cast<ACFPlayerCharacter>(GetOwner());
    if (!Hero) return;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(DismantleAim),false,Hero);
    if (IsValid(Preview)) Params.AddIgnoredActor(Preview);
    if(auto* Ghost=Hero->HomeBuilding->GetPreview()) Params.AddIgnoredActor(Ghost);
    const FVector Start = Hero->FirstPersonCamera->GetComponentLocation();
    GetWorld()->LineTraceSingleByChannel(Hit,Start,Start+Hero->FirstPersonCamera->GetForwardVector()*600,ECC_Visibility,Params);
    auto* Site = Cast<AWorkStation>(Hit.GetActor());
    if(auto* Module=Cast<AHomeModule>(Hit.GetActor())) { Hero->HomeBuilding->Dismantle(Module); return; }
    if (!Site || !Site->bPlayerBuilt) Hero->Feedback = TEXT("请瞄准6米内玩家建造的设施（初始设施受保护）");
    else DismantleStation(Site);
}

FString UBaseBuildingComponent::GetHint() const
{
    if(bHomeMode) return CastChecked<ACFPlayerCharacter>(GetOwner())->HomeBuilding->GetHint();
    return FString::Printf(TEXT("设施 %s（%d材料）| V 选择 | 8 房屋\n9 旋转 | 朝向%d（%s）\n%s"),
        *FrontierChinese::Specialty(Selected),GetCost(Selected),FMath::RoundToInt(FRotator::ClampAxis(Placement.Rotator().Yaw)),bManualFacilityRotation?TEXT("手动"):TEXT("随视角"),*Status);
}
