#include "PlayerEquipmentComponent.h"
#include "CFPlayerCharacter.h"
#include "InventoryComponent.h"
#include "BaseBuildingComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMesh.h"

UPlayerEquipmentComponent::UPlayerEquipmentComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Shaft(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Tip(TEXT("/Engine/BasicShapes/Cone.Cone"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Tint(TEXT("/Game/Materials/M_CreaturePalette.M_CreaturePalette"));
    ShaftMesh=Shaft.Object; TipMesh=Tip.Object; Palette=Tint.Object;
}

void UPlayerEquipmentComponent::BeginPlay()
{
    Super::BeginPlay(); auto* Hero=Cast<ACFPlayerCharacter>(GetOwner()); if(!Hero) return;
    WeaponPivot=NewObject<USceneComponent>(Hero); Hero->AddInstanceComponent(WeaponPivot); WeaponPivot->SetupAttachment(Hero->FirstPersonCamera); WeaponPivot->RegisterComponent();
    WeaponPivot->SetRelativeLocation(FVector(20,18,-22));
    auto Part=[&](UStaticMesh* Mesh,FVector Position,FVector Scale,FLinearColor Color)
    {
        auto* Piece=NewObject<UStaticMeshComponent>(Hero); Hero->AddInstanceComponent(Piece); Piece->SetupAttachment(WeaponPivot); Piece->SetStaticMesh(Mesh);
        Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision); Piece->SetCanEverAffectNavigation(false); Piece->SetCastShadow(false); Piece->SetOnlyOwnerSee(true);
        Piece->SetRelativeLocation(Position); Piece->SetRelativeRotation(FRotator(-90,0,0)); Piece->SetRelativeScale3D(Scale); Piece->RegisterComponent();
        if(Palette) { auto* Material=UMaterialInstanceDynamic::Create(Palette,this); Material->SetVectorParameterValue(TEXT("Tint"),Color); Piece->SetMaterial(0,Material); }
        WeaponParts.Add(Piece);
    };
    Part(ShaftMesh,FVector(20,0,0),FVector(.035,.035,.8),FLinearColor(.3f,.12f,.04f));
    Part(TipMesh,FVector(70,0,0),FVector(.11,.11,.25),FLinearColor(.28f,.42f,.53f));
    RefreshVisual();
}

bool UPlayerEquipmentComponent::Validate(const FPlayerEquipmentState& Candidate)
{ return (!Candidate.bSpearEquipped || Candidate.bOwnSpear) && (!Candidate.bVestEquipped || Candidate.bOwnVest); }
bool UPlayerEquipmentComponent::Restore(const FPlayerEquipmentState& NewState)
{
    if(!GetOwner() || !GetOwner()->HasAuthority() || !Validate(NewState)) return false;
    State=NewState; SwingRemaining=0; RefreshVisual(); return true;
}
float UPlayerEquipmentComponent::GetAttackBonus() const { return State.bOwnSpear && State.bSpearEquipped?FMath::Clamp(SpearAttackBonus,0.f,100.f):0; }
float UPlayerEquipmentComponent::GetDamageReduction() const { return State.bOwnVest && State.bVestEquipped?FMath::Clamp(VestReduction,0.f,.75f):0; }

bool UPlayerEquipmentComponent::Craft(bool bSpear)
{
    auto* Hero=Cast<ACFPlayerCharacter>(GetOwner()); if(!Hero || !Hero->HasAuthority() || Hero->Health<=0) return false;
    if(bSpear?State.bOwnSpear:State.bOwnVest) { Hero->Feedback=TEXT("Already crafted - 6 toggles spear, 7 toggles vest"); return false; }
    if(!Hero->IsNearWorkStation()) { Hero->Feedback=TEXT("Equipment crafting needs a work station within 8m"); return false; }
    const int32 Cost=FMath::Clamp(bSpear?SpearCost:VestCost,1,999);
    if(!Hero->Inventory->Consume(EFrontierItem::Material,Cost)) { Hero->Feedback=TEXT("Not enough materials for equipment"); return false; }
    if(bSpear) { State.bOwnSpear=true; State.bSpearEquipped=true; }
    else { State.bOwnVest=true; State.bVestEquipped=true; }
    RefreshVisual(); Hero->Feedback=bSpear?TEXT("Crafted and equipped Frontier Spear | 6 toggles weapon"):TEXT("Crafted and equipped Trail Vest | 7 toggles armor"); return true;
}
void UPlayerEquipmentComponent::CraftSpear() { Craft(true); }
void UPlayerEquipmentComponent::CraftVest() { Craft(false); }
void UPlayerEquipmentComponent::ToggleSpear()
{
    auto* Hero=Cast<ACFPlayerCharacter>(GetOwner()); if(!Hero || !Hero->HasAuthority() || Hero->Health<=0) return;
    if(!State.bOwnSpear) { Hero->Feedback=TEXT("Craft a spear first: 4 near a work station"); return; }
    State.bSpearEquipped=!State.bSpearEquipped; SwingRemaining=0; RefreshVisual(); Hero->Feedback=State.bSpearEquipped?TEXT("Spear equipped"):TEXT("Spear unequipped");
}
void UPlayerEquipmentComponent::ToggleVest()
{
    auto* Hero=Cast<ACFPlayerCharacter>(GetOwner()); if(!Hero || !Hero->HasAuthority() || Hero->Health<=0) return;
    if(!State.bOwnVest) { Hero->Feedback=TEXT("Craft a vest first: 5 near a work station"); return; }
    State.bVestEquipped=!State.bVestEquipped; Hero->Feedback=State.bVestEquipped?TEXT("Vest equipped"):TEXT("Vest unequipped");
}
void UPlayerEquipmentComponent::PlayAttack() { if(State.bSpearEquipped) SwingRemaining=.22f; }
bool UPlayerEquipmentComponent::IsWeaponVisible() const { return WeaponPivot && WeaponPivot->IsVisible(); }
void UPlayerEquipmentComponent::RefreshVisual()
{
    const auto* Hero=Cast<ACFPlayerCharacter>(GetOwner());
    if(WeaponPivot) WeaponPivot->SetVisibility(Hero && Hero->Health>0 && State.bOwnSpear && State.bSpearEquipped && !Hero->Building->IsActive(),true);
}
void UPlayerEquipmentComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime,TickType,TickFunction); RefreshVisual();
    SwingRemaining=FMath::Max(0.f,SwingRemaining-DeltaTime);
    const float Swing=SwingRemaining>0?FMath::Sin(PI*(1-SwingRemaining/.22f)):0;
    if(WeaponPivot) { WeaponPivot->SetRelativeLocation(FVector(20+Swing*18,18,-22+Swing*5)); WeaponPivot->SetRelativeRotation(FRotator(-Swing*8,0,0)); }
}
