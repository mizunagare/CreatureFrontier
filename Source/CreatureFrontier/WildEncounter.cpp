#include "WildEncounter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

AWildEncounter::AWildEncounter()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    auto* Sign = CreateDefaultSubobject<UTextRenderComponent>(TEXT("HabitatSign"));
    Sign->SetupAttachment(RootComponent);
    Sign->SetRelativeLocation(FVector(0,0,200));
    Sign->SetRelativeRotation(FRotator(0,180,0));
    Sign->SetHorizontalAlignment(EHTA_Center);
    Sign->SetWorldSize(26);
    Sign->SetText(FText::FromString(TEXT("Wild habitat")));
}

void AWildEncounter::BeginPlay()
{
    Super::BeginPlay();
    // Existing fixture scenarios own their entire population. The encounter scenario
    // explicitly ticks its own slot, keeping all regression worlds isolated.
    for (const TCHAR* Flag : {TEXT("CFSmokeTest"), TEXT("CFInventoryTest"), TEXT("CFCareTest"),
        TEXT("CFSpeciesTest"), TEXT("CFCombatTest"), TEXT("CFSaveWrite"), TEXT("CFSaveRead"), TEXT("CFNavTest"), TEXT("CFEncounterTest"), TEXT("CFBossTest"), TEXT("CFBuildingTest"), TEXT("CFWorldTest"), TEXT("CFWorldPreview"), TEXT("CFFarmingTest"), TEXT("CFFarmPreview"), TEXT("CFFeedingTest"), TEXT("CFWorkCycleTest"), TEXT("CFSquadTest"), TEXT("CFJournalTest")})
        if (FParse::Param(FCommandLine::Get(), Flag)) { SetActorTickEnabled(false); break; }
    if (auto* Sign = FindComponentByClass<UTextRenderComponent>())
    {
        Sign->SetText(FText::FromString(RegionName + (bBoss ? TEXT(" - ALPHA Lv") : TEXT(" - Lv")) + FString::FromInt(CreatureLevel)));
        const FVector Approach = FVector(-450,0,GetActorLocation().Z)-GetActorLocation();
        Sign->SetWorldRotation(Approach.Rotation());
    }
}

ACreatureCharacter* AWildEncounter::GetResident() const
{
    auto* Pet = Resident.Get();
    return Pet && Pet->Health > 0 && Pet->Disposition != ECreatureDisposition::Tamed ? Pet : nullptr;
}

bool AWildEncounter::IsSpawnAreaClear() const
{
    // Never pop an enemy into the player's view or on top of another creature.
    for (TActorIterator<APawn> It(GetWorld()); It; ++It)
    {
        if (auto* Creature = Cast<ACreatureCharacter>(*It); Creature && Creature->Health <= 0) continue;
        if (IsValid(*It) && GetDistanceTo(*It) < (It->IsPlayerControlled() ? 1000.0f : 200.0f)) return false;
    }
    return true;
}

void AWildEncounter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!HasAuthority() || EncounterId.IsNone() || !Definition) return;
    if (GetResident()) return;
    if (!bWaiting)
    {
        if (Resident.IsValid() && Resident->Health <= 0) Corpse = Resident;
        Resident.Reset();
        bWaiting = true;
        Remaining = RespawnSeconds;
    }
    Remaining = FMath::Max(0.0f, Remaining - DeltaSeconds);
    if (Remaining > 0 || !IsSpawnAreaClear()) return;
    if (Corpse.IsValid()) Corpse->Destroy();
    Corpse.Reset();
    const FTransform Spawn(GetActorRotation(), GetActorLocation() + FVector(0,0,90));
    auto* Pet = GetWorld()->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(), Spawn,
        nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding);
    if (!Pet) return;
    Pet->Definition = Definition;
    Pet->Level = FMath::Clamp(CreatureLevel, 1, 20);
    Pet->EncounterId = EncounterId;
    Pet->bAlpha = bBoss;
    Pet->bHasHabitat = true;
    Pet->HabitatHome = Spawn.GetLocation();
    Pet->FinishSpawning(Spawn);
    Resident = Pet;
    bWaiting = false;
}

float AWildEncounter::GetSavedRemaining() const
{
    return GetResident() ? 0.0f : (bWaiting ? Remaining : RespawnSeconds);
}

void AWildEncounter::RestorePopulation(float SavedRemaining)
{
    Resident.Reset(); Corpse.Reset();
    Remaining = FMath::Clamp(SavedRemaining, 0.0f, 600.0f);
    bWaiting = true;
    for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
        if (It->EncounterId == EncounterId && It->Disposition != ECreatureDisposition::Tamed)
        {
            if (It->Health > 0) { Resident = *It; bWaiting = false; }
            else Corpse = *It;
        }
}

FString AWildEncounter::GetStatus() const
{
    if (auto* Pet = GetResident())
        return FString::Printf(TEXT("%s | %s Lv%d HP %.0f/%.0f"), *RegionName,
            bBoss ? TEXT("ALPHA") : *Pet->GetCreatureName(), Pet->Level, Pet->Health, Pet->GetMaxHealth());
    return FString::Printf(TEXT("%s | returns in %.0fs%s"), *RegionName, GetSavedRemaining(),
        GetSavedRemaining() <= 0 ? TEXT(" (leave 10m)") : TEXT(""));
}
