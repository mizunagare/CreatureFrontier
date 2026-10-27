#include "WorkStation.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AWorkStation::AWorkStation()
{
    auto* Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WorkBench"));
    SetRootComponent(Mesh);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (Cube.Succeeded()) Mesh->SetStaticMesh(Cube.Object);
    Mesh->SetRelativeScale3D(FVector(2.0f, 1.0f, 0.7f));
    Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("StationLabel"));
    Label->SetupAttachment(Mesh);
    Label->SetAbsolute(false, false, true);
    Label->SetRelativeLocation(FVector(0,0,130));
    Label->SetWorldSize(24);
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

void AWorkStation::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RefreshLabel();
}

void AWorkStation::BeginPlay()
{
    Super::BeginPlay();
    if (bPlayerBuilt)
        if (auto* Palette = LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_CreaturePalette.M_CreaturePalette")))
        {
            auto* Material = UMaterialInstanceDynamic::Create(Palette,this);
            Material->SetVectorParameterValue(TEXT("Tint"),FLinearColor(.33f,.17f,.065f));
            FindComponentByClass<UStaticMeshComponent>()->SetMaterial(0,Material);
        }
    RefreshLabel();
}

void AWorkStation::RefreshLabel()
{
    const bool Farm = RequiredSpecialty == EWorkSpecialty::Farming;
    for (const auto& Plant : FarmPlants)
    {
        Plant->SetVisibility(Farm); Plant->SetHiddenInGame(!Farm);
        if (Farm)
            if (auto* Palette = LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_CreaturePalette.M_CreaturePalette")))
            {
                auto* Material = UMaterialInstanceDynamic::Create(Palette,this);
                Material->SetVectorParameterValue(TEXT("Tint"),FLinearColor(.12f,.6f,.08f)); Plant->SetMaterial(0,Material);
            }
    }
    const FString Name = UEnum::GetValueAsString(RequiredSpecialty).Replace(TEXT("EWorkSpecialty::"), TEXT(""));
    Label->SetText(FText::FromString(Name + (Farm ? TEXT("\nFOOD GARDEN") : bPlayerBuilt ? TEXT("\nBUILT FACILITY") : TEXT("\nWORK STATION"))));
}
