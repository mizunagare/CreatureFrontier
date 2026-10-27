#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "AIController.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

ACreatureCharacter::ACreatureCharacter()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    SetReplicateMovement(true);
    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
    AIControllerClass = AAIController::StaticClass();

    CreatureVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CreatureVisual"));
    CreatureVisual->SetupAttachment(GetRootComponent());
    CreatureVisual->SetRelativeLocation(FVector(0.0f, 0.0f, -10.0f));
    CreatureVisual->SetRelativeScale3D(FVector(0.75f, 0.55f, 0.8f));
    CreatureVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
        TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (SphereMesh.Succeeded())
    {
        CreatureVisual->SetStaticMesh(SphereMesh.Object);
    }
}

void ACreatureCharacter::BeginPlay()
{
    Super::BeginPlay();
    if (HasAuthority() && Definition)
    {
        Health = Definition->BaseMaxHealth;
    }
}

void ACreatureCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!HasAuthority() || Health <= 0.0f || Disposition == ECreatureDisposition::Tamed) return;

    AttackCooldown = FMath::Max(0.0f, AttackCooldown - DeltaSeconds);
    APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!IsValid(Player)) return;

    const FVector Offset = Player->GetActorLocation() - GetActorLocation();
    const float Distance2D = Offset.Size2D();
    if (Distance2D <= AggroRange)
    {
        Disposition = ECreatureDisposition::Hostile;
        SetActivity(ECreatureActivity::Combat);
        if (Distance2D > AttackRange)
        {
            AddMovementInput(Offset.GetSafeNormal2D(), 1.0f);
            SetActorRotation(Offset.Rotation());
        }
        else if (AttackCooldown <= 0.0f)
        {
            UGameplayStatics::ApplyDamage(Player, 10.0f, GetController(), this, UDamageType::StaticClass());
            AttackCooldown = AttackInterval;
        }
    }
}

float ACreatureCharacter::GetHealthRatio() const
{
    const float MaxHealth = Definition ? Definition->BaseMaxHealth : 100.0f;
    return MaxHealth > 0.0f ? FMath::Clamp(Health / MaxHealth, 0.0f, 1.0f) : 0.0f;
}

float ACreatureCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
    AController* EventInstigator, AActor* DamageCauser)
{
    const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
    if (HasAuthority())
    {
        Health = FMath::Max(0.0f, Health - Applied);
        OnRep_Health();
    }
    return Applied;
}

void ACreatureCharacter::SetActivity(ECreatureActivity NewActivity)
{
    if (!HasAuthority() || Activity == NewActivity) return;
    Activity = NewActivity;
    OnRep_Activity();
}

void ACreatureCharacter::Tame(AController* NewTamer)
{
    if (!HasAuthority() || !IsValid(NewTamer)) return;
    Tamer = NewTamer;
    Disposition = ECreatureDisposition::Tamed;
    Activity = ECreatureActivity::Follow;
    OnTamed();
}

void ACreatureCharacter::OnTamed_Implementation() {}
void ACreatureCharacter::OnRep_Health() {}
void ACreatureCharacter::OnRep_Activity() {}

void ACreatureCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACreatureCharacter, Health);
    DOREPLIFETIME(ACreatureCharacter, Disposition);
    DOREPLIFETIME(ACreatureCharacter, Activity);
    DOREPLIFETIME(ACreatureCharacter, Tamer);
}
