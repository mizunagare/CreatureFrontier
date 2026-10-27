#include "HomeModule.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/PointLightComponent.h"

bool UHomeModuleDefinition::IsValidDefinition() const
{
    if(static_cast<uint8>(Kind)>static_cast<uint8>(EHomeModuleKind::Lantern) || MaterialCost<2 || MaterialCost>100 ||
        static_cast<uint8>(RequiredResearch)>static_cast<uint8>(EFrontierResearch::CrystalStudy) || !FMath::IsFinite(LanternLumens) || LanternLumens<0 || LanternLumens>50000 ||
        (Kind==EHomeModuleKind::Lantern && LanternLumens<=0)) return false;
    for(const auto& Cost:ExtraCosts) if(Cost.Key==EFrontierItem::Material || Cost.Key>=EFrontierItem::Count || Cost.Value<=0 || Cost.Value>999) return false;
    return true;
}

UHomeModuleDefinition* UHomeModuleDefinition::LoadDefault(EHomeModuleKind Kind)
{
    if(static_cast<uint8>(Kind)>static_cast<uint8>(EHomeModuleKind::Lantern)) return nullptr;
    const TCHAR* Name=Kind==EHomeModuleKind::Floor?TEXT("DA_Floor"):Kind==EHomeModuleKind::Wall?TEXT("DA_Wall"):Kind==EHomeModuleKind::Doorway?TEXT("DA_Doorway"):Kind==EHomeModuleKind::Roof?TEXT("DA_Roof"):TEXT("DA_Lantern");
    return LoadObject<UHomeModuleDefinition>(nullptr,*FString::Printf(TEXT("/Game/Home/%s.%s"),Name,Name));
}
TArray<FHomePart> AHomeModule::Parts(EHomeModuleKind Kind)
{
    if(Kind==EHomeModuleKind::Floor || Kind==EHomeModuleKind::Roof) return {{FVector::ZeroVector,FVector(200,200,10)}};
    if(Kind==EHomeModuleKind::Wall) return {{FVector(0,0,150),FVector(200,10,150)}};
    if(Kind==EHomeModuleKind::Lantern) return {{FVector(0,0,45),FVector(12,12,45)}, {FVector(0,0,105),FVector(25,25,20)}};
    return {{FVector(-155,0,150),FVector(45,10,150)}, {FVector(155,0,150),FVector(45,10,150)}, {FVector(0,0,275),FVector(110,10,25)}};
}
AHomeModule::AHomeModule()
{
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    LanternLight=CreateDefaultSubobject<UPointLightComponent>(TEXT("LanternLight")); LanternLight->SetupAttachment(RootComponent);
    LanternLight->SetRelativeLocation(FVector(0,0,105)); LanternLight->SetMobility(EComponentMobility::Movable);
    LanternLight->SetIntensityUnits(ELightUnits::Lumens); LanternLight->SetIntensity(0); LanternLight->SetAttenuationRadius(650);
    LanternLight->SetLightColor(FLinearColor(1,.72f,.35f)); LanternLight->SetVisibility(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Materials/M_CreaturePalette.M_CreaturePalette"));
    for(int32 Index=0;Index<3;++Index)
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
        if(Used) { Mesh->SetRelativeLocation(Geometry[Index].Center); Mesh->SetRelativeScale3D(Geometry[Index].Extent/50); }
        if(Palette) Mesh->SetMaterial(0,Palette);
    }
}
void AHomeModule::ActivateInWorld()
{
    bStaged=false; SetActorEnableCollision(true); OnConstruction(GetActorTransform());
}
void AHomeModule::SetPreviewTint(bool Valid)
{
    if(Palette && bPreview) Palette->SetVectorParameterValue(TEXT("Tint"),Valid?FLinearColor(.08f,.8f,.15f):FLinearColor(.9f,.04f,.02f));
}
