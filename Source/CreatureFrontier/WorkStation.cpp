#include "WorkStation.h"
#include "FrontierChinese.h"
#include "FarmCropComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"

AWorkStation::AWorkStation()
{
    PrimaryActorTick.bCanEverTick=true;
    Crop=CreateDefaultSubobject<UFarmCropComponent>(TEXT("Crop"));
    auto* Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WorkBench"));
    SetRootComponent(Mesh);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (Cube.Succeeded()) Mesh->SetStaticMesh(Cube.Object);
    Mesh->SetRelativeScale3D(FVector(2.0f, 1.0f, 0.7f));
    Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("StationLabel"));
    Label->SetupAttachment(Mesh);
    Label->SetAbsolute(false, false, true);
    Label->SetRelativeLocation(FVector(0,0,130));
    Label->SetWorldSize(12);Label->SetCullDistance(900);
    Label->SetHorizontalAlignment(EHTA_Center);
    Label->SetTextRenderColor(FColor(235,240,220));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
    for (int32 Index=0; Index<6; ++Index)
    {
        auto* Plant = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("FarmPlant%d"),Index));
        Plant->SetupAttachment(Mesh); Plant->SetStaticMesh(Cone.Object);
        Plant->SetRelativeLocation(FVector((Index%3-1)*30,Index<3 ? -25 : 25,65));
        Plant->SetRelativeScale3D(FVector(.2,.3,.3));
        Plant->SetCollisionEnabled(ECollisionEnabled::NoCollision); Plant->SetCanEverAffectNavigation(false);
        Plant->SetVisibility(false); Plant->SetHiddenInGame(true);
        FarmPlants.Add(Plant);
    }
}

void AWorkStation::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(const auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0))
        Label->SetWorldRotation((Camera->GetCameraLocation()-Label->GetComponentLocation()).Rotation());
}

void AWorkStation::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RefreshLabel();
}

void AWorkStation::BeginPlay()
{
    Super::BeginPlay();
    Label->SetHiddenInGame(true);
    if (bPlayerBuilt)
        if (auto* Palette = LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_CreaturePalette.M_CreaturePalette")))
        {
            auto* Material = UMaterialInstanceDynamic::Create(Palette,this);
            Material->SetVectorParameterValue(TEXT("Tint"),RequiredSpecialty==EWorkSpecialty::Transport?FLinearColor(.08f,.32f,.48f):FLinearColor(.33f,.17f,.065f));
            FindComponentByClass<UStaticMeshComponent>()->SetMaterial(0,Material);
        }
    RefreshLabel();
}

void AWorkStation::RefreshLabel()
{
    const bool Farm = RequiredSpecialty == EWorkSpecialty::Farming;
    const bool Trough = RequiredSpecialty == EWorkSpecialty::Feeding;
    int32 Index = 0;
    for (const auto& Plant : FarmPlants)
    {
        const bool Show = (Farm && Crop->bPlanted) || (Trough && Index < (StoredFood+4)/5);
        if(Farm) Plant->SetRelativeScale3D(FVector(.12,.18,.12+FMath::Max(0,Crop->Stage())*.1));
        ++Index;
        Plant->SetVisibility(Show); Plant->SetHiddenInGame(!Show);
        Plant->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,Farm ? TEXT("/Engine/BasicShapes/Cone.Cone") : TEXT("/Engine/BasicShapes/Sphere.Sphere")));
        if (Show)
            if (auto* Palette = LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_CreaturePalette.M_CreaturePalette")))
            {
                auto* Material = UMaterialInstanceDynamic::Create(Palette,this);
                Material->SetVectorParameterValue(TEXT("Tint"),Farm ? FLinearColor(.12f,.6f,.08f) : FLinearColor(.9f,.4f,.04f)); Plant->SetMaterial(0,Material);
            }
    }
    const FString Name = FrontierChinese::Specialty(RequiredSpecialty);
    Label->SetText(FText::FromString(Trough?TEXT("食槽"):Farm?TEXT("食物农圃"):RequiredSpecialty==EWorkSpecialty::Transport?TEXT("运输站"):Name));
}
