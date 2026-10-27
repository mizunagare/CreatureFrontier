#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "AIController.h"
#include "CFPlayerCharacter.h"
#include "WorkStation.h"
#include "EngineUtils.h"
#include "Components/CapsuleComponent.h"
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
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

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
    if (!HasAuthority() || Health <= 0.0f) return;

    AttackCooldown = FMath::Max(0.0f, AttackCooldown - DeltaSeconds);
    if (Disposition == ECreatureDisposition::Tamed)
    {
        ACFPlayerCharacter* OwnerPlayer = Tamer ? Cast<ACFPlayerCharacter>(Tamer->GetPawn()) : nullptr;
        if (!OwnerPlayer || OwnerPlayer->Health <= 0.0f) return;
        if (Activity == ECreatureActivity::Work && IsValid(WorkSite))
        {
            const FVector Offset = WorkSite->GetActorLocation() - GetActorLocation();
            if (Offset.Size2D() > 220.0f) AddMovementInput(Offset.GetSafeNormal2D());
            else
            {
                WorkProgress += DeltaSeconds;
                if (WorkProgress >= 3.0f) { ++OwnerPlayer->Resources; WorkProgress = 0.0f; }
            }
            return;
        }
        WorkProgress = 0.0f;
        ACreatureCharacter* Enemy = nullptr;
        float Best = 1000.0f;
        for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
        {
            if (It->Disposition == ECreatureDisposition::Tamed || It->Health <= 0.0f) continue;
            const float Distance = OwnerPlayer->GetDistanceTo(*It);
            if (Distance < Best) { Best = Distance; Enemy = *It; }
        }
        SetActivity(Enemy ? ECreatureActivity::Combat : ECreatureActivity::Follow);
        const FVector Destination = Enemy ? Enemy->GetActorLocation() : OwnerPlayer->GetActorLocation();
        const FVector Offset = Destination - GetActorLocation();
        if (Offset.Size2D() > (Enemy ? AttackRange : 230.0f)) AddMovementInput(Offset.GetSafeNormal2D());
        else if (Enemy && AttackCooldown <= 0.0f)
        {
            UGameplayStatics::ApplyDamage(Enemy, Definition ? Definition->BaseAttack : 10.0f, Tamer, this, UDamageType::StaticClass());
            AttackCooldown = AttackInterval;
        }
        return;
    }
    APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!IsValid(Player)) return;
    if (const auto* Hero = Cast<ACFPlayerCharacter>(Player); Hero && Hero->Health <= 0.0f) return;

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
    const float Applied = FMath::Min(Health, FMath::Max(0.0f, DamageAmount));
    if (HasAuthority())
    {
        Health = FMath::Max(0.0f, Health - Applied);
        if (Health <= 0.0f) SetActivity(ECreatureActivity::Rest);
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
