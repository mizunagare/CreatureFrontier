#include "CFPlayerCharacter.h"
#include "Camera/CameraComponent.h"
#include "CaptureComponent.h"
#include "Components/CapsuleComponent.h"
#include "CreatureCharacter.h"
#include "Kismet/GameplayStatics.h"

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
    if (ACreatureCharacter* Creature = TraceCreature())
    {
        UGameplayStatics::ApplyDamage(Creature, AttackDamage, GetController(), this, UDamageType::StaticClass());
    }
}

void ACFPlayerCharacter::CaptureTarget()
{
    if (ACreatureCharacter* Creature = TraceCreature())
    {
        CaptureComponent->TryCapture(Creature);
    }
}
