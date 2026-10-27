#include "HomeModule.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

UHomeModuleDefinition* UHomeModuleDefinition::LoadDefault(EHomeModuleKind Kind)
{
    if(static_cast<uint8>(Kind)>static_cast<uint8>(EHomeModuleKind::Roof)) return nullptr;
    const TCHAR* Name=Kind==EHomeModuleKind::Floor?TEXT("DA_Floor"):Kind==EHomeModuleKind::Wall?TEXT("DA_Wall"):Kind==EHomeModuleKind::Doorway?TEXT("DA_Doorway"):TEXT("DA_Roof");
    return LoadObject<UHomeModuleDefinition>(nullptr,*FString::Printf(TEXT("/Game/Home/%s.%s"),Name,Name));
}
TArray<FHomePart> AHomeModule::Parts(EHomeModuleKind Kind)
{
    if(Kind==EHomeModuleKind::Floor || Kind==EHomeModuleKind::Roof) return {{FVector::ZeroVector,FVector(200,200,10)}};
    if(Kind==EHomeModuleKind::Wall) return {{FVector(0,0,150),FVector(200,10,150)}};
    return {{FVector(-155,0,150),FVector(45,10,150)}, {FVector(155,0,150),FVector(45,10,150)}, {FVector(0,0,275),FVector(110,10,25)}};
}
AHomeModule::AHomeModule()
{
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
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
    for(int32 Index=0;Index<Meshes.Num();++Index)
    {
        auto* Mesh=Meshes[Index].Get(); const bool Used=Geometry.IsValidIndex(Index);
        Mesh->SetVisibility(Used); Mesh->SetHiddenInGame(!Used);
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
