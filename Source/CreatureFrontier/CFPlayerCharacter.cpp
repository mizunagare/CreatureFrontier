#include "CFPlayerCharacter.h"
#include "Camera/CameraComponent.h"
#include "CaptureComponent.h"
#include "Components/CapsuleComponent.h"
#include "CreatureCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "EngineUtils.h"
#include "WorkStation.h"
#include "BaseBuildingComponent.h"
#include "FrontierSaveGame.h"
#include "CreatureDefinition.h"
#include "InventoryComponent.h"
#include "WildEncounter.h"
#include "WorldLoot.h"
#include "GameFramework/CharacterMovementComponent.h"

void ACFPlayerCharacter::QuickSave() { SaveProgress(); }
void ACFPlayerCharacter::QuickLoad() { LoadProgress(); }
void ACFPlayerCharacter::ToggleBuilding() { Building->Toggle(); }
void ACFPlayerCharacter::CycleBuilding() { Building->Cycle(); }
void ACFPlayerCharacter::DismantleBuilding() { Building->Dismantle(); }

void ACFPlayerCharacter::QuitGame()
{
    UKismetSystemLibrary::QuitGame(this, Cast<APlayerController>(GetController()), EQuitPreference::Quit, false);
}

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
        Record.Level = It->Level;
        Record.Experience = It->Experience;
        Record.Stamina = It->Stamina;
        Record.SpecialCooldown = It->SpecialCooldownRemaining;
        Record.EncounterId = It->EncounterId;
        Record.bAlpha = It->bAlpha;
        Record.AlphaCooldown = It->AlphaCooldown;
        Record.bHasHabitat = It->bHasHabitat; Record.HabitatHome = It->HabitatHome;
        Record.RoamRadius = It->RoamRadius; Record.LeashRadius = It->LeashRadius;
        Record.bOwned = It->Tamer == GetController();
        Record.Disposition = It->Disposition;
        Record.Activity = It->Activity;
        Record.WorkSiteName = IsValid(It->WorkSite) ? It->WorkSite->GetFName() : NAME_None;
        Record.WorkSiteId = IsValid(It->WorkSite) && It->WorkSite->bPlayerBuilt ? It->WorkSite->PersistentId : FGuid();
        Save->Creatures.Add(Record);
    }
    for (TActorIterator<AWildEncounter> It(GetWorld()); It; ++It)
        Save->EncounterTimers.Add(It->EncounterId, It->GetSavedRemaining());
    for (TActorIterator<AWorldLoot> It(GetWorld()); It; ++It)
    {
        FLootSaveRecord Record;
        Record.Transform = It->GetActorTransform();
        Record.Materials = It->Materials;
        Record.Medicine = It->Medicine;
        Save->Loot.Add(Record);
    }
    for (TActorIterator<AWorkStation> It(GetWorld()); It; ++It)
        if (It->bPlayerBuilt)
        {
            FFacilitySaveRecord Record;
            Record.Transform = It->GetActorTransform(); Record.Id = It->PersistentId; Record.Specialty = It->RequiredSpecialty;
            Save->Facilities.Add(Record);
        }
    const bool bSaved = UGameplayStatics::SaveGameToSlot(Save, Slot, 0);
    Feedback = bSaved ? TEXT("Saved - F9 to restore") : TEXT("Save failed");
    return bSaved;
}

bool ACFPlayerCharacter::LoadProgress(const FString& Slot)
{
    if (!HasAuthority() || !GetController()) return false;
    auto* Save = Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
    if (!Save || Save->FormatVersion < 1 || Save->FormatVersion > 8 ||
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
    for (const auto& Entry : Save->EncounterTimers)
        if (Entry.Key.IsNone() || !FMath::IsFinite(Entry.Value) || Entry.Value < 0 || Entry.Value > 600)
        { Feedback = TEXT("Invalid encounter timer in save"); return false; }
    for (const auto& Record : Save->Loot)
        if (Record.Transform.ContainsNaN() || Record.Materials < 0 || Record.Materials > 999 || Record.Medicine < 0 || Record.Medicine > 999)
        { Feedback = TEXT("Invalid loot in save"); return false; }
    TSet<FGuid> FacilityIds;
    if (Save->Facilities.Num() > 20) { Feedback = TEXT("Invalid facility count in save"); return false; }
    for (const auto& Record : Save->Facilities)
    {
        if (!Record.Id.IsValid() || FacilityIds.Contains(Record.Id) || UBaseBuildingComponent::GetCost(Record.Specialty) <= 0 ||
            !UBaseBuildingComponent::IsBuildTransformValid(Record.Transform))
        { Feedback = TEXT("Invalid facility in save"); return false; }
        FacilityIds.Add(Record.Id);
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
            !FMath::IsFinite(Record.Health) || Record.Health < 0 ||
            Record.Level < 1 || Record.Level > 20 || Record.Experience < 0 ||
            (Record.Level < 20 ? Record.Experience >= Record.Level * 100 : Record.Experience != 0) ||
            !FMath::IsFinite(Record.Stamina) || Record.Stamina < 0 || Record.Stamina > 100 ||
            !FMath::IsFinite(Record.SpecialCooldown) || Record.SpecialCooldown < 0 || Record.SpecialCooldown > 60 ||
            !FMath::IsFinite(Record.AlphaCooldown) || Record.AlphaCooldown < 0 || Record.AlphaCooldown > 10 ||
            Record.HabitatHome.ContainsNaN() || !FMath::IsFinite(Record.RoamRadius) || Record.RoamRadius < 0 || Record.RoamRadius > 600 ||
            !FMath::IsFinite(Record.LeashRadius) || Record.LeashRadius < 600 || Record.LeashRadius > 1500)
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
        Pet->EncounterId = Record.EncounterId;
        Pet->Level = Record.Level;
        Pet->bAlpha = Record.bAlpha;
        if (Save->FormatVersion < 6 && !Record.EncounterId.IsNone())
            for (TActorIterator<AWildEncounter> It(GetWorld()); It; ++It)
                if (It->EncounterId == Record.EncounterId) { Pet->bAlpha = It->bBoss; break; }
        Pet->FinishSpawning(Record.Transform);
        Pet->Health = FMath::Min(Pet->GetMaxHealth(), Record.Health);
        Pet->Experience = Record.Experience;
        Pet->Stamina = Record.Stamina;
        Pet->SpecialCooldownRemaining = Record.SpecialCooldown;
        Pet->AlphaCooldown = Record.AlphaCooldown;
        Pet->bHasHabitat = Record.bHasHabitat && !Record.bOwned;
        Pet->HabitatHome = Record.HabitatHome; Pet->RoamRadius = Record.RoamRadius; Pet->LeashRadius = Record.LeashRadius;
        if (Save->FormatVersion < 8 && !Record.bOwned && !Record.EncounterId.IsNone())
            for (TActorIterator<AWildEncounter> It(GetWorld()); It; ++It)
                if (It->EncounterId == Record.EncounterId) { Pet->bHasHabitat = true; Pet->HabitatHome = It->GetActorLocation()+FVector(0,0,90); break; }
        Pet->Disposition = Record.Disposition;
        Pet->Tamer = Record.bOwned ? GetController() : nullptr;
        Pet->Activity = Record.Activity;
        for (TActorIterator<AWorkStation> It(GetWorld()); It; ++It)
            if (!Record.WorkSiteId.IsValid() && !It->bPlayerBuilt && It->GetFName() == Record.WorkSiteName) { Pet->WorkSite = *It; break; }
        Restored.Add(Pet);
    }
    TArray<AWorldLoot*> OldLoot, RestoredLoot;
    for (TActorIterator<AWorldLoot> It(GetWorld()); It; ++It) OldLoot.Add(*It);
    for (const auto& Record : Save->Loot)
    {
        auto* Drop = GetWorld()->SpawnActor<AWorldLoot>(AWorldLoot::StaticClass(), Record.Transform);
        if (!Drop)
        {
            for (auto* Staged : Restored) Staged->Destroy();
            for (auto* Staged : RestoredLoot) Staged->Destroy();
            Feedback = TEXT("Could not restore loot");
            return false;
        }
        Drop->Materials = Record.Materials;
        Drop->Medicine = Record.Medicine;
        RestoredLoot.Add(Drop);
    }
    TArray<AWorkStation*> OldBuilt, RestoredBuilt;
    for (TActorIterator<AWorkStation> It(GetWorld()); It; ++It) if (It->bPlayerBuilt) OldBuilt.Add(*It);
    for (const auto& Record : Save->Facilities)
    {
        auto* Site = GetWorld()->SpawnActorDeferred<AWorkStation>(AWorkStation::StaticClass(),Record.Transform,nullptr,nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn,ESpawnActorScaleMethod::OverrideRootScale);
        if (!Site)
        {
            for (auto* Staged : Restored) Staged->Destroy();
            for (auto* Staged : RestoredLoot) Staged->Destroy();
            for (auto* Staged : RestoredBuilt) Staged->Destroy();
            Feedback = TEXT("Could not restore facilities"); return false;
        }
        Site->bPlayerBuilt = true; Site->PersistentId = Record.Id; Site->RequiredSpecialty = Record.Specialty;
        Site->FinishSpawning(Record.Transform);
        RestoredBuilt.Add(Site);
    }
    for (int32 Index=0; Index<Restored.Num(); ++Index)
    {
        auto* Pet = Restored[Index];
        const auto& Record = Save->Creatures[Index];
        if (Record.WorkSiteId.IsValid())
            for (auto* Site : RestoredBuilt) if (Site->PersistentId == Record.WorkSiteId) { Pet->WorkSite = Site; break; }
        if (Pet->Activity == ECreatureActivity::Work && !IsValid(Pet->WorkSite))
            Pet->Activity = Record.bOwned ? ECreatureActivity::Follow : ECreatureActivity::Idle;
    }
    Building->Cancel();
    for (auto* Site : OldBuilt) Site->Destroy();
    for (auto* Drop : OldLoot) Drop->Destroy();
    for (auto* Pet : OldCreatures) Pet->Destroy();
    for (TActorIterator<AWildEncounter> It(GetWorld()); It; ++It)
        It->RestorePopulation(Save->EncounterTimers.FindRef(It->EncounterId));
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
        if (It->Tamer == GetController()) { It->ClearCombatOrders(); It->WorkSite = nullptr; It->SetActivity(ECreatureActivity::Follow); }
    Feedback = TEXT("Companions following");
}

void ACFPlayerCharacter::CommandFocus()
{
    if (!HasAuthority() || Health <= 0) return;
    auto* Target = TraceCreature();
    if (!Target || Target->Health <= 0 || Target->Disposition == ECreatureDisposition::Tamed)
    { Feedback = TEXT("Aim at a living wild creature to focus attacks"); return; }
    int32 Count = 0;
    for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
        if (It->Tamer == GetController() && It->GetDistanceTo(this) <= 1800 && It->OrderAttack(Target)) ++Count;
    Feedback = FString::Printf(TEXT("Focus %s: %d active companions"), *Target->GetCreatureName(), Count);
}

void ACFPlayerCharacter::CommandSpecial()
{
    if (!HasAuthority() || Health <= 0) return;
    auto* Aimed = TraceCreature();
    ACreatureCharacter* BestPet = nullptr;
    ACreatureCharacter* BestTarget = nullptr;
    float BestDistance = TNumericLimits<float>::Max();
    for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
    {
        if (It->Tamer != GetController() || !It->IsSpecialReady() || It->GetDistanceTo(this) > 1800) continue;
        auto* Target = IsValid(Aimed) && Aimed->Health > 0 && Aimed->Disposition != ECreatureDisposition::Tamed ? Aimed : It->FocusTarget.Get();
        if (!IsValid(Target) || Target->Health <= 0 || Target->Disposition == ECreatureDisposition::Tamed || Target->GetDistanceTo(this) > 1500) continue;
        const float Distance = It->GetDistanceTo(Target);
        if (Distance < BestDistance) { BestDistance = Distance; BestPet = *It; BestTarget = Target; }
    }
    if (BestPet && BestPet->RequestSpecial(BestTarget)) Feedback = BestPet->GetCreatureName() + TEXT(" moving into skill range");
    else Feedback = TEXT("Need an active companion, target, 20 stamina and ready cooldown");
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
    Feedback = FString::Printf(TEXT("Assigned: %s. Matching specialties work faster"),
        *UEnum::GetValueAsString(Site->RequiredSpecialty));
}

void ACFPlayerCharacter::CommandRest()
{
    if (!HasAuthority() || Health <= 0) return;
    AWorkStation* Site = nullptr;
    float Best = 800;
    for (TActorIterator<AWorkStation> It(GetWorld()); It; ++It)
        if (GetDistanceTo(*It) < Best) { Best = GetDistanceTo(*It); Site = *It; }
    if (!Site) { Feedback = TEXT("Rest command requires a nearby base work station (8m)"); return; }
    int32 Count = 0;
    for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
        if (It->Tamer == GetController() && It->Health > 0)
        { It->WorkSite = Site; It->SetActivity(ECreatureActivity::Rest); ++Count; }
    Feedback = FString::Printf(TEXT("%d companions returning to base to recover health and stamina"), Count);
}

void ACFPlayerCharacter::HealCompanion()
{
    if (!HasAuthority() || Health <= 0) return;
    auto* Pet = TraceCreature();
    if (!Pet || Pet->Tamer != GetController()) { Feedback = TEXT("Aim at your companion to use medicine"); return; }
    if (Pet->Health >= Pet->GetMaxHealth()) { Feedback = TEXT("Companion is already healthy"); return; }
    if (!Inventory->Consume(EFrontierItem::Medicine, 1)) { Feedback = TEXT("No medicine - craft with 2 at base"); return; }
    Pet->Health = FMath::Min(Pet->GetMaxHealth(), Pet->Health + 40.0f);
    Feedback = TEXT("Companion treated (+40 health). T: rest at base, F: follow");
}

ACFPlayerCharacter::ACFPlayerCharacter()
{
    FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
    FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
    FirstPersonCamera->SetRelativeLocation(FVector(-10.0f, 0.0f, 64.0f));
    FirstPersonCamera->bUsePawnControlRotation = true;

    CaptureComponent = CreateDefaultSubobject<UCaptureComponent>(TEXT("CaptureComponent"));
    Inventory = CreateDefaultSubobject<UInventoryComponent>(TEXT("Inventory"));
    Building = CreateDefaultSubobject<UBaseBuildingComponent>(TEXT("Building"));
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
    PlayerInputComponent->BindKey(EKeys::T, IE_Pressed, this, &ACFPlayerCharacter::CommandRest);
    PlayerInputComponent->BindKey(EKeys::P, IE_Pressed, this, &ACFPlayerCharacter::HealCompanion);
    PlayerInputComponent->BindKey(EKeys::Q, IE_Pressed, this, &ACFPlayerCharacter::CommandFocus);
    PlayerInputComponent->BindKey(EKeys::G, IE_Pressed, this, &ACFPlayerCharacter::CommandSpecial);
    PlayerInputComponent->BindKey(EKeys::R, IE_Pressed, this, &ACFPlayerCharacter::RestartLevel);
    PlayerInputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ACFPlayerCharacter::QuitGame);
    PlayerInputComponent->BindKey(EKeys::F5, IE_Pressed, this, &ACFPlayerCharacter::QuickSave);
    PlayerInputComponent->BindKey(EKeys::F9, IE_Pressed, this, &ACFPlayerCharacter::QuickLoad);
    PlayerInputComponent->BindKey(EKeys::One, IE_Pressed, this, &ACFPlayerCharacter::CraftCaptureDevice);
    PlayerInputComponent->BindKey(EKeys::Two, IE_Pressed, this, &ACFPlayerCharacter::CraftMedicine);
    PlayerInputComponent->BindKey(EKeys::H, IE_Pressed, this, &ACFPlayerCharacter::UseMedicine);
    PlayerInputComponent->BindKey(EKeys::B, IE_Pressed, this, &ACFPlayerCharacter::ToggleBuilding);
    PlayerInputComponent->BindKey(EKeys::V, IE_Pressed, this, &ACFPlayerCharacter::CycleBuilding);
    PlayerInputComponent->BindKey(EKeys::X, IE_Pressed, this, &ACFPlayerCharacter::DismantleBuilding);
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
    if (Building->IsActive()) { Building->Confirm(); return; }
    if (Health <= 0.0f || GetWorld()->GetTimeSeconds() < NextAttackTime) return;
    NextAttackTime = GetWorld()->GetTimeSeconds() + 0.4;
    if (ACreatureCharacter* Creature = TraceCreature())
    {
        if (Creature->Disposition == ECreatureDisposition::Tamed) return;
        const float Applied = UGameplayStatics::ApplyDamage(Creature, AttackDamage, GetController(), this, UDamageType::StaticClass());
        Feedback = Applied > 0 ? TEXT("Hit! Right mouse: capture before health reaches zero") : TEXT("Creature retreating - wait for it to return home");
    }
}

void ACFPlayerCharacter::CaptureTarget()
{
    if (Building->IsActive()) { Building->Cancel(); return; }
    if (Health <= 0.0f) return;
    if (ACreatureCharacter* Creature = TraceCreature())
    {
        CaptureComponent->TryCapture(Creature);
    }
}
