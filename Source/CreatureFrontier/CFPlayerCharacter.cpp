#include "CFPlayerCharacter.h"
#include "Camera/CameraComponent.h"
#include "CaptureComponent.h"
#include "Components/CapsuleComponent.h"
#include "CreatureCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "WorkStation.h"
#include "FrontierSaveGame.h"
#include "CreatureDefinition.h"
#include "InventoryComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

void ACFPlayerCharacter::QuickSave() { SaveProgress(); }
void ACFPlayerCharacter::QuickLoad() { LoadProgress(); }

bool ACFPlayerCharacter::IsNearWorkStation() const
{
    for (TActorIterator<AWorkStation> It(GetWorld()); It; ++It)
        if (GetDistanceTo(*It) <= 800.0f) return true;
    return false;
}

void ACFPlayerCharacter::CraftCaptureDevice()
{
    if (Health <= 0 || !HasAuthority()) return;
    if (!IsNearWorkStation()) { Feedback = TEXT("Crafting needs a nearby work station (8m)"); return; }
    Feedback = Inventory->Craft(EFrontierItem::CaptureDevice) ? TEXT("Crafted capture device: -3 materials") : TEXT("Need 3 materials or inventory is full");
}

void ACFPlayerCharacter::CraftMedicine()
{
    if (Health <= 0 || !HasAuthority()) return;
    if (!IsNearWorkStation()) { Feedback = TEXT("Crafting needs a nearby work station (8m)"); return; }
    Feedback = Inventory->Craft(EFrontierItem::Medicine) ? TEXT("Crafted medicine: -5 materials") : TEXT("Need 5 materials or inventory is full");
}

void ACFPlayerCharacter::UseMedicine()
{
    if (Health <= 0 || Health >= 100 || !HasAuthority()) { Feedback = TEXT("Medicine requires an injured, living player"); return; }
    if (!Inventory->Consume(EFrontierItem::Medicine, 1)) { Feedback = TEXT("No medicine - craft at work station"); return; }
    Health = FMath::Min(100.0f, Health + 40.0f);
    Feedback = TEXT("Used medicine: restored up to 40 health");
}

bool ACFPlayerCharacter::SaveProgress(const FString& Slot)
{
    if (!HasAuthority() || !GetController()) return false;
    auto* Save = Cast<UFrontierSaveGame>(UGameplayStatics::CreateSaveGameObject(UFrontierSaveGame::StaticClass()));
    Save->MapName = UGameplayStatics::GetCurrentLevelName(this, true);
    Save->PlayerTransform = GetActorTransform();
    Save->ViewRotation = GetController()->GetControlRotation();
    Save->Health = Health;
    Save->Resources = Inventory->Count(EFrontierItem::Material);
    Save->InventoryStacks = Inventory->GetStacks();
    for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
    {
        FCreatureSaveRecord Record;
        Record.CreatureClass = It->GetClass();
        Record.Definition = It->Definition.Get();
        Record.Transform = It->GetActorTransform();
        Record.Health = It->Health;
        Record.bOwned = It->Tamer == GetController();
        Record.Disposition = It->Disposition;
        Record.Activity = It->Activity;
        Record.WorkSiteName = IsValid(It->WorkSite) ? It->WorkSite->GetFName() : NAME_None;
        Save->Creatures.Add(Record);
    }
    const bool bSaved = UGameplayStatics::SaveGameToSlot(Save, Slot, 0);
    Feedback = bSaved ? TEXT("Saved - F9 to restore") : TEXT("Save failed");
    return bSaved;
}

bool ACFPlayerCharacter::LoadProgress(const FString& Slot)
{
    if (!HasAuthority() || !GetController()) return false;
    auto* Save = Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
    if (!Save || Save->FormatVersion < 1 || Save->FormatVersion > 2 ||
        Save->MapName != UGameplayStatics::GetCurrentLevelName(this, true) ||
        !FMath::IsFinite(Save->Health) || Save->Health < 0 || Save->Health > 100 ||
        Save->Resources < 0 || Save->PlayerTransform.ContainsNaN() || Save->ViewRotation.ContainsNaN())
    {
        Feedback = TEXT("No compatible save for this map");
        return false;
    }
    if (Save->FormatVersion == 1)
    {
        Save->InventoryStacks = {{EFrontierItem::Material, Save->Resources}, {EFrontierItem::CaptureDevice, 5}, {EFrontierItem::Medicine, 1}};
    }
    if (!UInventoryComponent::Validate(Save->InventoryStacks))
    {
        Feedback = TEXT("Invalid inventory in save");
        return false;
    }
    TArray<ACreatureCharacter*> OldCreatures;
    for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It) OldCreatures.Add(*It);
    TArray<ACreatureCharacter*> Restored;
    // Stage every actor before replacing the live world; a missing asset leaves it intact.
    for (const auto& Record : Save->Creatures)
    {
        UClass* CreatureClass = Record.CreatureClass.LoadSynchronous();
        UCreatureDefinition* LoadedDefinition = Record.Definition.LoadSynchronous();
        if (!CreatureClass || CreatureClass->HasAnyClassFlags(CLASS_Abstract) ||
            (!Record.Definition.IsNull() && !LoadedDefinition) || Record.Transform.ContainsNaN() ||
            !FMath::IsFinite(Record.Health) || Record.Health < 0)
        {
            for (auto* Pet : Restored) Pet->Destroy();
            Feedback = TEXT("Save references unavailable or invalid creature data");
            return false;
        }
        auto* Pet = GetWorld()->SpawnActorDeferred<ACreatureCharacter>(CreatureClass, Record.Transform,
            nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Pet)
        {
            for (auto* Staged : Restored) Staged->Destroy();
            Feedback = TEXT("Could not restore creatures");
            return false;
        }
        Pet->Definition = LoadedDefinition;
        Pet->FinishSpawning(Record.Transform);
        Pet->Health = Record.Health;
        Pet->Disposition = Record.Disposition;
        Pet->Tamer = Record.bOwned ? GetController() : nullptr;
        Pet->Activity = Record.Activity;
        for (TActorIterator<AWorkStation> It(GetWorld()); It; ++It)
            if (It->GetFName() == Record.WorkSiteName) { Pet->WorkSite = *It; break; }
        if (Pet->Activity == ECreatureActivity::Work && !Pet->WorkSite)
            Pet->Activity = Record.bOwned ? ECreatureActivity::Follow : ECreatureActivity::Idle;
        Restored.Add(Pet);
    }
    for (auto* Pet : OldCreatures) Pet->Destroy();
    SetActorTransform(Save->PlayerTransform, false, nullptr, ETeleportType::TeleportPhysics);
    GetController()->SetControlRotation(Save->ViewRotation);
    GetCharacterMovement()->StopMovementImmediately();
    Health = Save->Health;
    Inventory->Restore(Save->InventoryStacks);
    GetCharacterMovement()->SetMovementMode(Health > 0 ? MOVE_Walking : MOVE_None);
    Feedback = TEXT("Save restored");
    return true;
}

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
    Inventory = CreateDefaultSubobject<UInventoryComponent>(TEXT("Inventory"));
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
    PlayerInputComponent->BindKey(EKeys::F5, IE_Pressed, this, &ACFPlayerCharacter::QuickSave);
    PlayerInputComponent->BindKey(EKeys::F9, IE_Pressed, this, &ACFPlayerCharacter::QuickLoad);
    PlayerInputComponent->BindKey(EKeys::One, IE_Pressed, this, &ACFPlayerCharacter::CraftCaptureDevice);
    PlayerInputComponent->BindKey(EKeys::Two, IE_Pressed, this, &ACFPlayerCharacter::CraftMedicine);
    PlayerInputComponent->BindKey(EKeys::H, IE_Pressed, this, &ACFPlayerCharacter::UseMedicine);
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
