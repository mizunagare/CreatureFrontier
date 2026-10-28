#include "WorldLoot.h"
#include "CFPlayerCharacter.h"
#include "InventoryComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

AWorldLoot::AWorldLoot()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = .2f;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Marker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LootMarker"));
    Marker->SetupAttachment(RootComponent);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Shape(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Marker->SetStaticMesh(Shape.Object);
    Marker->SetRelativeScale3D(FVector(.35f));
    Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Marker->SetCanEverAffectNavigation(false);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Palette(TEXT("/Game/Materials/M_CreaturePalette.M_CreaturePalette"));
    Marker->SetMaterial(0, Palette.Object);
    Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("LootLabel"));
    Label->SetupAttachment(RootComponent);
    Label->SetAbsolute(false, false, true);
    Label->SetRelativeLocation(FVector(0,0,55));
    Label->SetWorldSize(20);
    Label->SetHorizontalAlignment(EHTA_Center);
}

void AWorldLoot::BeginPlay()
{
    Super::BeginPlay();
    Label->SetHiddenInGame(true);
    auto* Color = Marker->CreateDynamicMaterialInstance(0);
    if (Color) Color->SetVectorParameterValue(TEXT("Tint"), FLinearColor(1,.65f,.04f));
}

bool AWorldLoot::Collect(ACFPlayerCharacter* Hero)
{
    if (!HasAuthority() || !IsValid(Hero) || Hero->Health <= 0 || GetDistanceTo(Hero) > 220 || IsActorBeingDestroyed()) return false;
    int32 Picked = 0;
    auto Transfer = [&](EFrontierItem Item, int32& Left)
    {
        const int32 Amount = FMath::Min(Left, UInventoryComponent::StackLimit - Hero->Inventory->Count(Item));
        if (Amount > 0 && Hero->Inventory->AddItem(Item, Amount)) { Left -= Amount; Picked += Amount; }
    };
    Transfer(EFrontierItem::Material, Materials);
    Transfer(EFrontierItem::Medicine, Medicine);
    if (Picked > 0) Hero->Feedback = FString::Printf(TEXT("已收取掉落物：%d个物品%s"), Picked,
        Materials + Medicine > 0 ? TEXT("（背包已满，剩余物资包已保留）") : TEXT(""));
    if (Materials <= 0 && Medicine <= 0) Destroy();
    return Picked > 0;
}

void AWorldLoot::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    Label->SetText(FText::FromString(FString::Printf(TEXT("掉落物：%d材料 / %d药品"), Materials, Medicine)));
    auto* Hero = Cast<ACFPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
    if (Hero) { Label->SetWorldRotation((Hero->GetActorLocation() - Label->GetComponentLocation()).Rotation()); Collect(Hero); }
}
