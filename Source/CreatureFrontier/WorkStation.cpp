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
    const FString Name = UEnum::GetValueAsString(RequiredSpecialty).Replace(TEXT("EWorkSpecialty::"), TEXT(""));
    Label->SetText(FText::FromString(Name + (bPlayerBuilt ? TEXT("\nBUILT FACILITY") : TEXT("\nWORK STATION"))));
}
