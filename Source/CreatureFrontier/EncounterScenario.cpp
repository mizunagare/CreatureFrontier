#include "EncounterScenario.h"
#include "WildEncounter.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformMisc.h"

void RunEncounterScenario(UWorld* World, ACFPlayerCharacter* Hero)
{
    UCreatureDefinition* Stone = nullptr;
    for (TActorIterator<ACreatureCharacter> It(World); It; ++It)
        if (It->Definition && It->Definition->Form == ECreatureForm::Stone) Stone = It->Definition;
    if (!Stone) { UE_LOG(LogTemp, Error, TEXT("CF_ENCOUNTER FAIL missing stone asset")); FPlatformMisc::RequestExitWithStatus(false, 1); return; }
    Hero->SetActorLocation(FVector(-1500,-1500,40));
    auto* Site = World->SpawnActor<AWildEncounter>(FVector(-1500,1500,-50), FRotator::ZeroRotator);
    Site->EncounterId = TEXT("TestAlpha"); Site->Definition = Stone; Site->CreatureLevel = 5; Site->RespawnSeconds = 10; Site->bBoss = true;
    Site->SetActorTickEnabled(false);
    Site->Tick(0);
    auto* First = Site->GetResident();
    const bool Spawn = First && First->Level == 5 && First->Health == 240;
    if (!First) { UE_LOG(LogTemp, Error, TEXT("CF_ENCOUNTER FAIL spawn")); FPlatformMisc::RequestExitWithStatus(false, 1); return; }
    First->Tame(Hero->GetController());
    Site->Tick(1);
    const bool Capture = !Site->GetResident() && FMath::IsNearlyEqual(Site->Remaining, 9) && IsValid(First);
    Hero->SetActorLocation(FVector(-1500,1000,40));
    Site->Tick(20);
    const bool Safe = !Site->GetResident() && IsValid(First);
    Hero->SetActorLocation(FVector(-1500,-1500,40));
    // The captured companion also has to leave the spawn capsule.
    First->SetActorLocation(FVector(-1300,-1500,40));
    Site->Tick(0);
    auto* Second = Site->GetResident();
    const bool Repeat = Second && Second != First && First->Disposition == ECreatureDisposition::Tamed;
    if (!Second) { UE_LOG(LogTemp, Error, TEXT("CF_ENCOUNTER FAIL respawn")); FPlatformMisc::RequestExitWithStatus(false, 1); return; }
    UGameplayStatics::ApplyDamage(Second, 1000, Hero->GetController(), Hero, UDamageType::StaticClass());
    Site->Tick(2);
    const bool Death = Second->Health == 0 && !Site->GetResident() && FMath::IsNearlyEqual(Site->Remaining, 8);
    const FString Slot = TEXT("FrontierEncounterAutomation");
    bool Saved = Hero->SaveProgress(Slot);
    Site->Remaining = 0;
    Saved &= Hero->LoadProgress(Slot) && FMath::IsNearlyEqual(Site->Remaining, 8) && !Site->GetResident();
    Site->Tick(8);
    auto* Third = Site->GetResident();
    const bool Return = Third && Third->Health == Third->GetMaxHealth();
    Saved &= Hero->SaveProgress(Slot) && Hero->LoadProgress(Slot) && Site->GetResident();
    int32 Wild = 0, Owned = 0;
    for (TActorIterator<ACreatureCharacter> It(World); It; ++It)
        if (It->EncounterId == Site->EncounterId)
        { if (It->Disposition == ECreatureDisposition::Tamed) ++Owned; else ++Wild; }
    const bool Unique = Wild == 1 && Owned == 1;
    const bool Pass = Spawn && Capture && Safe && Repeat && Death && Saved && Return && Unique;
    UE_LOG(LogTemp, Display, TEXT("CF_ENCOUNTER %s spawn=%d capture=%d proximity=%d repeat=%d death=%d save=%d return=%d unique=%d"),
        Pass ? TEXT("PASS") : TEXT("FAIL"), Spawn, Capture, Safe, Repeat, Death, Saved, Return, Unique);
    FPlatformMisc::RequestExitWithStatus(false, Pass ? 0 : 1);
}
