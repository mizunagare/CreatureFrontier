#include "HomeModule.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/PointLightComponent.h"
#include "InventoryComponent.h"

bool UHomeModuleDefinition::IsValidDefinition() const
{
    if(static_cast<uint8>(Kind)>static_cast<uint8>(EHomeModuleKind::Window) || MaterialCost<2 || MaterialCost>100 ||
        static_cast<uint8>(RequiredResearch)>static_cast<uint8>(EFrontierResearch::CrystalStudy) || !FMath::IsFinite(LanternLumens) || LanternLumens<0 || LanternLumens>50000 ||
        (Kind==EHomeModuleKind::Lantern && LanternLumens<=0)) return false;
    for(const auto& Cost:ExtraCosts) if(Cost.Key==EFrontierItem::Material || Cost.Key>=EFrontierItem::Count || Cost.Value<=0 || Cost.Value>999) return false;
    return true;
}

UHomeModuleDefinition* UHomeModuleDefinition::LoadDefault(EHomeModuleKind Kind)
{
    if(static_cast<uint8>(Kind)>static_cast<uint8>(EHomeModuleKind::Window)) return nullptr;
    if(Kind==EHomeModuleKind::Window) return LoadObject<UHomeModuleDefinition>(nullptr,TEXT("/Game/Home/DA_Window.DA_Window"));
    if(Kind==EHomeModuleKind::Door) return LoadObject<UHomeModuleDefinition>(nullptr,TEXT("/Game/Home/DA_Door.DA_Door"));
    const TCHAR* Name=Kind==EHomeModuleKind::Floor?TEXT("DA_Floor"):Kind==EHomeModuleKind::Wall?TEXT("DA_Wall"):Kind==EHomeModuleKind::Doorway?TEXT("DA_Doorway"):Kind==EHomeModuleKind::Roof?TEXT("DA_Roof"):Kind==EHomeModuleKind::Lantern?TEXT("DA_Lantern"):TEXT("DA_Storage");
    return LoadObject<UHomeModuleDefinition>(nullptr,*FString::Printf(TEXT("/Game/Home/%s.%s"),Name,Name));
}
TArray<FHomePart> AHomeModule::Parts(EHomeModuleKind Kind)
{
    if(Kind==EHomeModuleKind::Floor || Kind==EHomeModuleKind::Roof) return {{FVector::ZeroVector,FVector(200,200,10)}};
    if(Kind==EHomeModuleKind::Wall) return {{FVector(0,0,150),FVector(200,10,150)}};
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
    const auto Geometry=Parts(GetKind());
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
