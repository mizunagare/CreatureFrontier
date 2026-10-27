#include "CFPlayerCharacter.h"
#include "Camera/CameraComponent.h"
#include "CaptureComponent.h"
#include "Components/CapsuleComponent.h"
#include "CreatureCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "WorkStation.h"
#include "GameFramework/CharacterMovementComponent.h"

void ACFPlayerCharacter::RestartLevel()
{
    UGameplayStatics::OpenLevel(this, FName(*GetWorld()->GetName()));
}

void ACFPlayerCharacter::BeginPlay()
{
    Super::BeginPlay();
    CaptureComponent->OnCaptureResolved.AddDynamic(this, &ACFPlayerCharacter::CaptureResolved);
}

float ACFPlayerCharacter::TakeDamage(float Amount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer)
{
    if (!HasAuthority() || Health <= 0.0f) return 0.0f;
    const float Applied = FMath::Min(Health, FMath::Max(0.0f, Amount));
    Health -= Applied;
    Feedback = Health <= 0.0f ? TEXT("Downed - press R to restart") : TEXT("Taking damage!");
    if (Health <= 0.0f) GetCharacterMovement()->DisableMovement();
    return Applied;
}

void ACFPlayerCharacter::CaptureResolved(ACreatureCharacter* Creature, bool bSucceeded)
{
    Feedback = bSucceeded ? TEXT("Captured! F: follow | E: assign work") : TEXT("Capture failed - weaken it and retry");
}

void ACFPlayerCharacter::CommandFollow()
{
    if (Health <= 0.0f) return;
    for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
        if (It->Tamer == GetController()) { It->WorkSite = nullptr; It->SetActivity(ECreatureActivity::Follow); }
    Feedback = TEXT("Companions following");
}

void ACFPlayerCharacter::CommandWork()
{
    if (Health <= 0.0f) return;
    AWorkStation* Site = nullptr;
    float Best = 800.0f;
    for (TActorIterator<AWorkStation> It(GetWorld()); It; ++It)
    {
        const float Distance = GetDistanceTo(*It);
        if (Distance < Best) { Best = Distance; Site = *It; }
    }
    if (!Site) { Feedback = TEXT("Move within 8m of the work station"); return; }
    for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
        if (It->Tamer == GetController() && It->Health > 0.0f)
        { It->WorkSite = Site; It->SetActivity(ECreatureActivity::Work); }
    Feedback = TEXT("Companions assigned to gather resources");
}

ACFPlayerCharacter::ACFPlayerCharacter()
{
    FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
    FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
    FirstPersonCamera->SetRelativeLocation(FVector(-10.0f, 0.0f, 64.0f));
    FirstPersonCamera->bUsePawnControlRotation = true;

    CaptureComponent = CreateDefaultSubobject<UCaptureComponent>(TEXT("CaptureComponent"));
}

void ACFPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &ACFPlayerCharacter::MoveForward);
    PlayerInputComponent->BindAxis(TEXT("MoveRight"), this, &ACFPlayerCharacter::MoveRight);
    PlayerInputComponent->BindAxis(TEXT("Turn"), this, &APawn::AddControllerYawInput);
    PlayerInputComponent->BindAxis(TEXT("LookUp"), this, &APawn::AddControllerPitchInput);
    PlayerInputComponent->BindAction(TEXT("Jump"), IE_Pressed, this, &ACharacter::Jump);
    PlayerInputComponent->BindAction(TEXT("Jump"), IE_Released, this, &ACharacter::StopJumping);
    PlayerInputComponent->BindAction(TEXT("Attack"), IE_Pressed, this, &ACFPlayerCharacter::Attack);
    PlayerInputComponent->BindAction(TEXT("Capture"), IE_Pressed, this, &ACFPlayerCharacter::CaptureTarget);
    PlayerInputComponent->BindKey(EKeys::F, IE_Pressed, this, &ACFPlayerCharacter::CommandFollow);
    PlayerInputComponent->BindKey(EKeys::E, IE_Pressed, this, &ACFPlayerCharacter::CommandWork);
    PlayerInputComponent->BindKey(EKeys::R, IE_Pressed, this, &ACFPlayerCharacter::RestartLevel);
}

void ACFPlayerCharacter::MoveForward(float Value)
{
    if (Controller && !FMath::IsNearlyZero(Value))
    {
        AddMovementInput(FRotationMatrix(FRotator(0.0f, Controller->GetControlRotation().Yaw, 0.0f))
            .GetUnitAxis(EAxis::X), Value);
    }
}

void ACFPlayerCharacter::MoveRight(float Value)
{
    if (Controller && !FMath::IsNearlyZero(Value))
    {
        AddMovementInput(FRotationMatrix(FRotator(0.0f, Controller->GetControlRotation().Yaw, 0.0f))
            .GetUnitAxis(EAxis::Y), Value);
    }
}

ACreatureCharacter* ACFPlayerCharacter::TraceCreature() const
{
    if (!FirstPersonCamera || !GetWorld()) return nullptr;

    const FVector Start = FirstPersonCamera->GetComponentLocation();
    const FVector End = Start + FirstPersonCamera->GetForwardVector() * InteractionRange;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CreatureInteraction), false, this);
    GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
    return Cast<ACreatureCharacter>(Hit.GetActor());
}

void ACFPlayerCharacter::Attack()
{
    if (Health <= 0.0f || GetWorld()->GetTimeSeconds() < NextAttackTime) return;
    NextAttackTime = GetWorld()->GetTimeSeconds() + 0.4;
    if (ACreatureCharacter* Creature = TraceCreature())
    {
        if (Creature->Disposition == ECreatureDisposition::Tamed) return;
        UGameplayStatics::ApplyDamage(Creature, AttackDamage, GetController(), this, UDamageType::StaticClass());
        Feedback = TEXT("Hit! Right mouse: capture before health reaches zero");
    }
}

void ACFPlayerCharacter::CaptureTarget()
{
    if (Health <= 0.0f) return;
    if (ACreatureCharacter* Creature = TraceCreature())
    {
        CaptureComponent->TryCapture(Creature);
    }
}
