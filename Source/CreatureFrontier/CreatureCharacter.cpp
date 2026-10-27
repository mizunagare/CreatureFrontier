#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "CFPlayerCharacter.h"
#include "InventoryComponent.h"
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
    GetCapsuleComponent()->SetCanEverAffectNavigation(false);
    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->MaxWalkSpeed = 320.0f;
    bUseControllerRotationYaw = false;

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

void ACreatureCharacter::StopNavigation()
{
    if (auto* AI = Cast<AAIController>(GetController())) AI->StopMovement();
}

void ACreatureCharacter::NavigateTo(const FVector& Destination, float Acceptance)
{
    if (GetWorld()->GetTimeSeconds() < NextNavigationUpdate) return;
    NextNavigationUpdate = GetWorld()->GetTimeSeconds() + 0.4;
    auto* AI = Cast<AAIController>(GetController());
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!AI || !Nav) { StopNavigation(); return; }
    FNavLocation Projected;
    if (!Nav->ProjectPointToNavigation(Destination, Projected, FVector(250,250,300)))
    {
        StopNavigation();
        return;
    }
    AI->MoveToLocation(Projected.Location, Acceptance, false, true, false, false, nullptr, false);
}

bool ACreatureCharacter::CanHit(const AActor* Target) const
{
    if (!IsValid(Target) || GetDistanceTo(Target) > AttackRange) return false;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CreatureMelee), false, this);
    return !GetWorld()->LineTraceSingleByChannel(Hit, GetActorLocation(), Target->GetActorLocation(), ECC_Visibility, Params)
        || Hit.GetActor() == Target;
}

void ACreatureCharacter::BeginPlay()
{
    Super::BeginPlay();
    if (HasAuthority() && Definition)
    {
        Health = GetMaxHealth();
    }
}

void ACreatureCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!HasAuthority()) return;
    if (Health <= 0.0f) { StopNavigation(); return; }

    AttackCooldown = FMath::Max(0.0f, AttackCooldown - DeltaSeconds);
    if (Disposition == ECreatureDisposition::Tamed)
    {
        ACFPlayerCharacter* OwnerPlayer = Tamer ? Cast<ACFPlayerCharacter>(Tamer->GetPawn()) : nullptr;
        if (!OwnerPlayer || OwnerPlayer->Health <= 0.0f) { StopNavigation(); return; }
        if (Activity == ECreatureActivity::Rest)
        {
            if (IsValid(WorkSite) && GetDistanceTo(WorkSite) > 220.0f)
            {
                NavigateTo(WorkSite->GetActorLocation(), 35.0f);
                return;
            }
            StopNavigation();
            Stamina = FMath::Min(100.0f, Stamina + DeltaSeconds * (IsValid(WorkSite) ? 12.0f : 4.0f));
            if (IsValid(WorkSite)) Health = FMath::Min(GetMaxHealth(), Health + DeltaSeconds * 6.0f);
            return;
        }
        if (Activity == ECreatureActivity::Work && IsValid(WorkSite))
        {
            const FVector Offset = WorkSite->GetActorLocation() - GetActorLocation();
            if (Offset.Size2D() > 220.0f) { NavigateTo(WorkSite->GetActorLocation(), 35.0f); WorkProgress = 0.0f; }
            else
            {
                StopNavigation();
                Stamina = FMath::Max(0.0f, Stamina - DeltaSeconds * 1.5f);
                if (Stamina <= 0.0f)
                {
                    SetActivity(ECreatureActivity::Rest);
                    WorkProgress = 0.0f;
                    OwnerPlayer->Feedback = TEXT("A companion is exhausted and resting at base");
                    return;
                }
                WorkProgress += DeltaSeconds;
                if (WorkProgress >= 3.0f)
                {
                    if (OwnerPlayer->Inventory->AddItem(EFrontierItem::Material, 1)) AddExperience(1);
                    WorkProgress = 0.0f;
                }
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
        if (Enemy ? !CanHit(Enemy) : Offset.Size2D() > 230.0f) NavigateTo(Destination, Enemy ? 65.0f : 160.0f);
        else
        {
            StopNavigation();
            if (Enemy && AttackCooldown <= 0.0f)
            {
                if (Stamina < 3.0f) { WorkSite = nullptr; SetActivity(ECreatureActivity::Rest); return; }
                Stamina -= 3.0f;
                UGameplayStatics::ApplyDamage(Enemy, GetAttackDamage(), Tamer, this, UDamageType::StaticClass());
                AttackCooldown = AttackInterval;
            }
        }
        return;
    }
    APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!IsValid(Player)) { StopNavigation(); return; }
    if (const auto* Hero = Cast<ACFPlayerCharacter>(Player); Hero && Hero->Health <= 0.0f) { StopNavigation(); return; }

    APawn* Target = Player;
    float Closest = GetDistanceTo(Player);
    for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
    {
        if (It->Disposition != ECreatureDisposition::Tamed || It->Health <= 0) continue;
        const float Distance = GetDistanceTo(*It);
        if (Distance < Closest) { Closest = Distance; Target = *It; }
    }
    const FVector Offset = Target->GetActorLocation() - GetActorLocation();
    const float Distance2D = Offset.Size2D();
    if (Distance2D <= AggroRange)
    {
        Disposition = ECreatureDisposition::Hostile;
        SetActivity(ECreatureActivity::Combat);
        if (!CanHit(Target))
        {
            NavigateTo(Target->GetActorLocation(), 65.0f);
        }
        else
        {
            StopNavigation();
            if (AttackCooldown <= 0.0f)
            {
                UGameplayStatics::ApplyDamage(Target, GetAttackDamage(), GetController(), this, UDamageType::StaticClass());
                AttackCooldown = AttackInterval;
            }
        }
    }
    else { StopNavigation(); SetActivity(ECreatureActivity::Idle); }
}

float ACreatureCharacter::GetHealthRatio() const
{
    const float MaxHealth = GetMaxHealth();
    return MaxHealth > 0.0f ? FMath::Clamp(Health / MaxHealth, 0.0f, 1.0f) : 0.0f;
}

float ACreatureCharacter::GetMaxHealth() const
{
    return (Definition ? Definition->BaseMaxHealth : 100.0f) + (Level - 1) * 20.0f;
}

float ACreatureCharacter::GetAttackDamage() const
{
    return (Definition ? Definition->BaseAttack : 10.0f) + (Level - 1) * 2.0f;
}

void ACreatureCharacter::AddExperience(int32 Amount)
{
    if (!HasAuthority() || Health <= 0 || Disposition != ECreatureDisposition::Tamed || Amount <= 0 || Level >= 20) return;
    Experience += FMath::Min(Amount, 10000);
    while (Level < 20 && Experience >= Level * 100)
    {
        Experience -= Level * 100;
        ++Level;
        Health = FMath::Min(GetMaxHealth(), Health + 20.0f);
    }
    if (Level == 20) Experience = 0;
}

float ACreatureCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
    AController* EventInstigator, AActor* DamageCauser)
{
    const float Applied = FMath::Min(Health, FMath::Max(0.0f, DamageAmount));
    if (HasAuthority() && Applied > 0)
    {
        Health = FMath::Max(0.0f, Health - Applied);
        if (Health <= 0.0f)
        {
            SetActivity(ECreatureActivity::Rest);
            StopNavigation();
            if (Disposition != ECreatureDisposition::Tamed && IsValid(EventInstigator))
            {
                for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
                    if (It->Tamer == EventInstigator && It->GetDistanceTo(this) <= 1500.0f)
                        It->AddExperience((DamageCauser == *It ? 50 : 25) * Level);
            }
        }
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
    DOREPLIFETIME(ACreatureCharacter, Level);
    DOREPLIFETIME(ACreatureCharacter, Experience);
    DOREPLIFETIME(ACreatureCharacter, Stamina);
    DOREPLIFETIME(ACreatureCharacter, Disposition);
    DOREPLIFETIME(ACreatureCharacter, Activity);
    DOREPLIFETIME(ACreatureCharacter, Tamer);
}
