#include "HomeModule.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/PointLightComponent.h"
#include "InventoryComponent.h"
#include "CFPlayerCharacter.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "HomeLayout.h"
#include "CreatureCharacter.h"
#include "CompanionConstructionComponent.h"
#include "CompanionCargoComponent.h"
#include "EngineUtils.h"

bool UHomeModuleDefinition::IsValidDefinition() const
{
    if(static_cast<uint8>(Kind)>=static_cast<uint8>(EHomeModuleKind::Count) || MaterialCost<2 || MaterialCost>100 ||
        static_cast<uint8>(RequiredResearch)>static_cast<uint8>(EFrontierResearch::CrystalStudy) || !FMath::IsFinite(LanternLumens) || LanternLumens<0 || LanternLumens>50000 ||
        (Kind==EHomeModuleKind::Lantern && LanternLumens<=0)) return false;
    for(const auto& Cost:ExtraCosts) if(Cost.Key==EFrontierItem::Material || Cost.Key>=EFrontierItem::Count || Cost.Value<=0 || Cost.Value>999) return false;
    return true;
}

UHomeModuleDefinition* UHomeModuleDefinition::LoadDefault(EHomeModuleKind Kind)
{
    if(static_cast<uint8>(Kind)>=static_cast<uint8>(EHomeModuleKind::Count)) return nullptr;
    if(Kind==EHomeModuleKind::Stairs) return LoadObject<UHomeModuleDefinition>(nullptr,TEXT("/Game/Home/DA_Stairs.DA_Stairs"));
    if(Kind==EHomeModuleKind::Fence) return LoadObject<UHomeModuleDefinition>(nullptr,TEXT("/Game/Home/DA_Fence.DA_Fence"));
    if(Kind==EHomeModuleKind::Window) return LoadObject<UHomeModuleDefinition>(nullptr,TEXT("/Game/Home/DA_Window.DA_Window"));
    if(Kind==EHomeModuleKind::Door) return LoadObject<UHomeModuleDefinition>(nullptr,TEXT("/Game/Home/DA_Door.DA_Door"));
    const TCHAR* Name=Kind==EHomeModuleKind::Floor?TEXT("DA_Floor"):Kind==EHomeModuleKind::Wall?TEXT("DA_Wall"):Kind==EHomeModuleKind::Doorway?TEXT("DA_Doorway"):Kind==EHomeModuleKind::Roof?TEXT("DA_Roof"):Kind==EHomeModuleKind::Lantern?TEXT("DA_Lantern"):TEXT("DA_Storage");
    return LoadObject<UHomeModuleDefinition>(nullptr,*FString::Printf(TEXT("/Game/Home/%s.%s"),Name,Name));
}
TArray<FHomePart> AHomeModule::Parts(EHomeModuleKind Kind)
{
    if(Kind==EHomeModuleKind::Floor || Kind==EHomeModuleKind::Roof) return {{FVector::ZeroVector,FVector(200,200,10)}};
    if(Kind==EHomeModuleKind::Stairs)
    {
        // 底端先留50厘米平坦入口，分开地基与首级抬升，避免合成40厘米的导航断阶。
        // 16级实体踏步，级高20厘米；上升320厘米后接相邻二层楼板，不使用隐形斜坡。
        TArray<FHomePart> Steps;
        for(int32 Index=0;Index<16;++Index)Steps.Add({FVector(-150+(Index+.5)*21.875,0,(Index+1)*10.),FVector(10.9375,100,(Index+1)*10.)});
        return Steps;
    }
    if(Kind==EHomeModuleKind::Wall) return {{FVector(0,0,150),FVector(200,10,150)}};
    // 两立柱和两横梁均参与实体碰撞与导航，不作为屋顶承重墙。
    if(Kind==EHomeModuleKind::Fence) return {{FVector(-190,0,65),FVector(10,10,65)}, {FVector(190,0,65),FVector(10,10,65)}, {FVector(0,0,40),FVector(180,7,10)}, {FVector(0,0,100),FVector(180,7,10)}};
    // 四块实体围成180×100厘米的开放窗洞；不是玻璃贴图，也不关闭整面墙的碰撞。
    if(Kind==EHomeModuleKind::Window) return {{FVector(-145,0,150),FVector(55,10,150)}, {FVector(145,0,150),FVector(55,10,150)}, {FVector(0,0,60),FVector(90,10,60)}, {FVector(0,0,260),FVector(90,10,40)}};
    if(Kind==EHomeModuleKind::Lantern) return {{FVector(0,0,45),FVector(12,12,45)}, {FVector(0,0,105),FVector(25,25,20)}};
    if(Kind==EHomeModuleKind::Storage) return {{FVector(0,0,40),FVector(45,35,40)}, {FVector(0,0,85),FVector(48,38,5)}, {FVector(48,0,60),FVector(3,8,10)}};
    if(Kind==EHomeModuleKind::Door) return {{FVector(-155,0,150),FVector(45,10,150)}, {FVector(155,0,150),FVector(45,10,150)}, {FVector(0,0,275),FVector(110,10,25)}, {FVector(0,0,125),FVector(109,8,124)}};
    return {{FVector(-155,0,150),FVector(45,10,150)}, {FVector(155,0,150),FVector(45,10,150)}, {FVector(0,0,275),FVector(110,10,25)}};
}
AHomeModule::AHomeModule()
{
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    StorageInventory=CreateDefaultSubobject<UInventoryComponent>(TEXT("StorageInventory"));
    LanternLight=CreateDefaultSubobject<UPointLightComponent>(TEXT("LanternLight")); LanternLight->SetupAttachment(RootComponent);
    LanternLight->SetRelativeLocation(FVector(0,0,105)); LanternLight->SetMobility(EComponentMobility::Movable);
    LanternLight->SetIntensityUnits(ELightUnits::Lumens); LanternLight->SetIntensity(0); LanternLight->SetAttenuationRadius(650);
    LanternLight->SetLightColor(FLinearColor(1,.72f,.35f)); LanternLight->SetVisibility(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Materials/M_CreaturePalette.M_CreaturePalette"));
    for(int32 Index=0;Index<4;++Index)
    {
        auto* Mesh=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Part%d"),Index));
        Mesh->SetupAttachment(RootComponent); Mesh->SetStaticMesh(Cube.Object); Mesh->SetMaterial(0,Material.Object);
        Mesh->SetMobility(EComponentMobility::Movable); Meshes.Add(Mesh);
    }
}
void AHomeModule::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    if(!Palette) Palette=Meshes[0]->CreateDynamicMaterialInstance(0);
    if(Palette && Definition) Palette->SetVectorParameterValue(TEXT("Tint"),Definition->Color);
    // 从基础材质创建独立实例，避免从共享MID创建而意外改写门框。
    if(!DoorLeafPalette && Palette) DoorLeafPalette=UMaterialInstanceDynamic::Create(Palette->GetMaterial(),this);
    if(DoorLeafPalette && Definition) DoorLeafPalette->SetVectorParameterValue(TEXT("Tint"),Definition->DoorLeafColor);
    // 新工地使用有实体碰撞的脚手架；改建保留旧墙。高处模块的供料箱位于下层。
    const TArray<FHomePart> Geometry=ConstructionOriginalDefinition?Parts(ConstructionOriginalDefinition->Kind):bUnderConstruction?ConstructionParts(GetKind(),FloorId.IsValid()):Parts(GetKind());
    if(bUnderConstruction && Palette)Palette->SetVectorParameterValue(TEXT("Tint"),FLinearColor(.65f,.38f,.08f));
    // 保留原组件，按楼梯踏步或施工轮廓需要补充，不销毁可复用组件。
    while(Meshes.Num()<Geometry.Num())
    {
        auto* Mesh=NewObject<UStaticMeshComponent>(this,*FString::Printf(TEXT("StepPart%d"),Meshes.Num()));
        Mesh->SetupAttachment(RootComponent);Mesh->SetStaticMesh(Meshes[0]->GetStaticMesh());
        Mesh->SetMobility(EComponentMobility::Movable);Mesh->RegisterComponent();Meshes.Add(Mesh);
    }
    LanternLight->SetIntensity(Definition?Definition->LanternLumens:0);
    LanternLight->SetVisibility(GetKind()==EHomeModuleKind::Lantern && !bPreview && !bStaged);
    for(int32 Index=0;Index<Meshes.Num();++Index)
    {
        auto* Mesh=Meshes[Index].Get(); const bool Used=Geometry.IsValidIndex(Index);
        Mesh->SetVisibility(Used); Mesh->SetHiddenInGame(!Used);
        Mesh->SetCastShadow(GetKind()!=EHomeModuleKind::Lantern);
        Mesh->SetCanEverAffectNavigation(Used && !bPreview && !bStaged);
        Mesh->SetCollisionProfileName(TEXT("BlockAll"));
        Mesh->SetCollisionEnabled(Used && !bPreview && !bStaged?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
        Mesh->SetRelativeRotation(FRotator::ZeroRotator);
        if(Used) { Mesh->SetRelativeLocation(Geometry[Index].Center); Mesh->SetRelativeScale3D(Geometry[Index].Extent/50); }
        if(Used && GetKind()==EHomeModuleKind::Door && Index==3 && DoorState==2)
        { Mesh->SetRelativeLocation(FVector(-110,110,125)); Mesh->SetRelativeRotation(FRotator(0,90,0)); }
        if(Palette) Mesh->SetMaterial(0,GetKind()==EHomeModuleKind::Door && Index==3 && DoorLeafPalette?DoorLeafPalette.Get():Palette.Get());
    }
}
void AHomeModule::ActivateInWorld()
{
    bStaged=false; SetActorEnableCollision(true); OnConstruction(GetActorTransform());
}
void AHomeModule::SetPreviewTint(bool Valid)
{
    if(Palette && bPreview) Palette->SetVectorParameterValue(TEXT("Tint"),Valid?FLinearColor(.08f,.8f,.15f):FLinearColor(.9f,.04f,.02f));
    if(DoorLeafPalette && bPreview) DoorLeafPalette->SetVectorParameterValue(TEXT("Tint"),Valid?FLinearColor(.08f,.8f,.15f):FLinearColor(.9f,.04f,.02f));
}
AHomeModule* AHomeModule::TraceConstruction(ACFPlayerCharacter* Hero)
{
    if(!Hero)return nullptr;FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(ConstructionAim),false,Hero);
    const auto Start=Hero->FirstPersonCamera->GetComponentLocation();
    Hero->GetWorld()->LineTraceSingleByChannel(Hit,Start,Start+Hero->FirstPersonCamera->GetForwardVector()*800,ECC_Visibility,Query);
    auto* Site=Cast<AHomeModule>(Hit.GetActor());return Site && Site->bUnderConstruction && !Site->bPreview && !Site->bStaged?Site:nullptr;
}
bool AHomeModule::SupportsConstruction(EHomeModuleKind Kind)
{
    return Kind==EHomeModuleKind::Floor || Kind==EHomeModuleKind::Roof || Kind==EHomeModuleKind::Wall ||
        Kind==EHomeModuleKind::Window || Kind==EHomeModuleKind::Doorway || Kind==EHomeModuleKind::Fence || Kind==EHomeModuleKind::Stairs;
}
TArray<FHomePart> AHomeModule::ConstructionParts(EHomeModuleKind Kind,bool ElevatedFloor)
{
    // 楼梯工地只有低矮实体边框和料箱，不提前生成可通往上层的踏步。
    if(Kind==EHomeModuleKind::Stairs)
        return {{FVector(-195,0,12),FVector(5,100,8)},{FVector(195,0,12),FVector(5,100,8)},
            {FVector(0,-95,12),FVector(190,5,8)},{FVector(0,95,12),FVector(190,5,8)},
            {FVector(0,0,12),FVector(35,25,12)}};
    if(Kind==EHomeModuleKind::Floor && !ElevatedFloor)
        return {{FVector(-195,0,12),FVector(5,200,8)},{FVector(195,0,12),FVector(5,200,8)},
            {FVector(0,-195,12),FVector(190,5,8)},{FVector(0,195,12),FVector(190,5,8)},
            {FVector(0,0,12),FVector(35,25,12)}};
    if(Kind==EHomeModuleKind::Roof || (Kind==EHomeModuleKind::Floor && ElevatedFloor))
        return {{FVector(-195,0,0),FVector(5,200,8)},{FVector(195,0,0),FVector(5,200,8)},
            {FVector(0,-195,0),FVector(190,5,8)},{FVector(0,195,0),FVector(190,5,8)},
            {FVector(0,0,22-HomeLayout::StoreyHeight),FVector(35,25,12)}};
    return {{FVector(-160,0,50),FVector(10,10,50)},{FVector(160,0,50),FVector(10,10,50)},
        {FVector(0,0,90),FVector(160,10,8)},{FVector(0,0,12),FVector(35,25,12)}};
}
FVector AHomeModule::ConstructionAnchor() const
{
    return GetActorLocation()-FVector(0,0,GetKind()==EHomeModuleKind::Roof || (GetKind()==EHomeModuleKind::Floor && FloorId.IsValid())?HomeLayout::StoreyHeight-10:0);
}
FVector AHomeModule::ConstructionPoint(const FVector& From) const
{
    if(GetKind()!=EHomeModuleKind::Floor || FloorId.IsValid())return ConstructionAnchor();
    // 地面地板从边缘施工；工人不能站在尚未出现的平台上，也不扩大施工接触距离。
    const auto Local=GetActorTransform().InverseTransformPosition(From);
    return GetActorTransform().TransformPosition(FVector(FMath::Clamp(Local.X,-200.,200.),FMath::Clamp(Local.Y,-200.,200.),0));
}
bool AHomeModule::ClearsCompletedPawn(const ACharacter* Pawn,const FVector& Center) const
{
    if(!Pawn || !Definition)return false;const auto* Shape=Pawn->GetCapsuleComponent();if(!Shape)return false;
    const FVector Local=GetActorTransform().InverseTransformPosition(Center);
    const double Radius=Shape->GetScaledCapsuleRadius(),Segment=Shape->GetScaledCapsuleHalfHeight()-Radius;
    // 判断真实直立胶囊与完工模块的实体，不把当前门洞开口当作安全施工站位。
    for(const auto& Part:Parts(GetKind()))
    {
        const FVector Delta=(Local-Part.Center).GetAbs()-Part.Extent-FVector(1);
        const double X=FMath::Max(0.,Delta.X),Y=FMath::Max(0.,Delta.Y),Z=FMath::Max(0.,Delta.Z-Segment);
        if(X*X+Y*Y+Z*Z<Radius*Radius)return false;
    }
    return true;
}
FString AHomeModule::ConstructionStatus() const
{
    const FString Target=ConstructionOriginalDefinition?ConstructionOriginalDefinition->DisplayName.ToString()+TEXT("→")+Definition->DisplayName.ToString():TEXT("待建")+(Definition?Definition->DisplayName.ToString():TEXT("模块"));
    const bool High=GetKind()==EHomeModuleKind::Roof || (GetKind()==EHomeModuleKind::Floor && FloorId.IsValid());
    const TCHAR* Hint=High?TEXT("先V选料源，再瞄下层工地料箱按E派工"):TEXT("先瞄准箱子按V，再瞄准此处按E派工");
    const TCHAR* Cancel=ConstructionOriginalDefinition?TEXT("旧墙仍承重 | X取消恢复旧墙并退料"):
        GetKind()==EHomeModuleKind::Floor?TEXT("未完工不承重 | F取消任务 | X取消并退料"):GetKind()==EHomeModuleKind::Stairs?TEXT("未完工不能通行 | F取消任务 | X取消并退料"):High?TEXT("从下层施工 | F取消任务 | X取消并退料"):TEXT("F取消伙伴任务 | X取消施工并退回已交材料");
    const ACreatureCharacter* Worker=nullptr;const AHomeModule* Crate=nullptr;
    for(TActorIterator<ACreatureCharacter> It(GetWorld());It;++It)if(It->Construction->GetSite()==this){Worker=*It;break;}
    for(TActorIterator<AHomeModule> It(GetWorld());It;++It)if(It->PersistentId==ConstructionCrateId && !It->IsActorBeingDestroyed() && !It->bPreview && !It->bStaged && It->GetKind()==EHomeModuleKind::Storage){Crate=*It;break;}
    const int32 Stock=Crate?Crate->StorageInventory->Count(EFrontierItem::Material):0;
    FString State=TEXT("尚未派工"),Action=Hint;
    // 提示只读取真实任务、携物、需求与箱内库存，不维护另一套施工状态。
    if(Worker)
    {
        if(Worker->FeedingSite.IsValid()){State=TEXT("伙伴进食中，保留施工任务");Action=TEXT("可向原箱补料，无需重新派工");}
        else if(Worker->Activity==ECreatureActivity::Rest){State=TEXT("伙伴休息中，满足需求后复工");Action=TEXT("J喂食 | P治疗 | 原任务保留");}
        else if(Worker->Cargo->Count()>0){State=FString::Printf(TEXT("伙伴携料%d | 箱内材料%d"),Worker->Cargo->Count(),Stock);Action=TEXT("等待伙伴实际交付材料");}
        else if(Definition && StorageInventory->Count(EFrontierItem::Material)>=Definition->MaterialCost)
        {State=Worker->Construction->IsWorking()?TEXT("伙伴正在施工"):TEXT("材料已齐，等待施工站位");Action=TEXT("无需补料，保持工地净空");}
        else if(!Crate){State=TEXT("未指定有效料箱");Action=TEXT("瞄准新箱V，再瞄工地E改派");}
        else if(Definition && StorageInventory->Count(EFrontierItem::Material)<Definition->MaterialCost && Stock==0){State=TEXT("缺料等待 | 原箱材料0");Action=TEXT("E开原箱存材料，自动继续");}
        else {State=FString::Printf(TEXT("施工任务进行中 | 箱内材料%d"),Stock);Action=TEXT("等待伙伴靠近，保持工地净空");}
    }
    return FString::Printf(TEXT("%s | 材料%d/%d | 施工%.1f/6\n%s\n%s\n%s"),
        *Target,StorageInventory->Count(EFrontierItem::Material),Definition?Definition->MaterialCost:0,ConstructionWork,
        *State,*Action,Cancel);
}
