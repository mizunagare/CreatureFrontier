#include "CFGameMode.h"
#include "CFPlayerCharacter.h"
#include "CFHUD.h"
#include "CaptureComponent.h"
#include "CreatureCharacter.h"
#include "WorkStation.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformMisc.h"
#include "TimerManager.h"
#include "EngineUtils.h"

ACFGameMode::ACFGameMode()
{
    DefaultPawnClass = ACFPlayerCharacter::StaticClass();
    HUDClass = ACFHUD::StaticClass();
}

void ACFGameMode::BeginPlay()
{
    Super::BeginPlay();
    const bool bSaveWrite = FParse::Param(FCommandLine::Get(), TEXT("CFSaveWrite"));
    const bool bSaveRead = FParse::Param(FCommandLine::Get(), TEXT("CFSaveRead"));
    if (!bSaveWrite && !bSaveRead && !FParse::Param(FCommandLine::Get(), TEXT("CFSmokeTest"))) return;
    FTimerHandle TestStart;
    GetWorldTimerManager().SetTimer(TestStart, [this, bSaveWrite, bSaveRead]()
    {
        auto* Hero = Cast<ACFPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
        if (!Hero) { FPlatformMisc::RequestExitWithStatus(true, 1); return; }
        const FString TestSlot = TEXT("FrontierAutomationSlot");
        if (bSaveRead)
        {
            const bool bLoaded = Hero->LoadProgress(TestSlot);
            int32 Count = 0, Owned = 0, Workers = 0;
            for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
            {
                ++Count;
                if (It->Tamer == Hero->GetController())
                {
                    ++Owned;
                    if (It->WorkSite && It->Activity == ECreatureActivity::Work && It->Health == 75) ++Workers;
                }
            }
            const bool bValues = Hero->Health == 67 && Hero->Resources == 42 &&
                Hero->GetActorLocation().Equals(FVector(-450, 0, 120), 1.0f);
            const bool bSecondLoad = Hero->LoadProgress(TestSlot);
            int32 RepeatedCount = 0;
            for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It) ++RepeatedCount;
            const bool bPass = bLoaded && bValues && bSecondLoad && Count == RepeatedCount && Owned == 1 && Workers == 1;
            UE_LOG(LogTemp, Display, TEXT("CF_SAVE_READ %s values=%d owned=%d workers=%d count=%d repeated=%d"),
                bPass ? TEXT("PASS") : TEXT("FAIL"), bValues, Owned, Workers, Count, RepeatedCount);
            FPlatformMisc::RequestExitWithStatus(false, bPass ? 0 : 4);
            return;
        }
        auto* Pet = GetWorld()->SpawnActor<ACreatureCharacter>(Hero->GetActorLocation() + FVector(200, 0, 100), FRotator::ZeroRotator);
        if (!Pet) { FPlatformMisc::RequestExitWithStatus(true, 2); return; }
        const float Before = Hero->CaptureComponent->CalculateCaptureChance(Pet);
        UGameplayStatics::ApplyDamage(Pet, 25, Hero->GetController(), Hero, nullptr);
        const bool bCaptureScaling = Before > 0 && Hero->CaptureComponent->CalculateCaptureChance(Pet) > Before;
        Pet->Tame(Hero->GetController());
        Hero->SetActorLocation(FVector(-450, 0, 120));
        Pet->SetActorLocation(FVector(-450, 180, 120));
        Hero->CommandWork();
        if (bSaveWrite)
        {
            Hero->Health = 67;
            Hero->Resources = 42;
            const bool bSaved = Hero->SaveProgress(TestSlot);
            UE_LOG(LogTemp, Display, TEXT("CF_SAVE_WRITE %s"), bSaved ? TEXT("PASS") : TEXT("FAIL"));
            FPlatformMisc::RequestExitWithStatus(false, bSaved ? 0 : 5);
            return;
        }
        const int32 BeforeResources = Hero->Resources;
        FTimerHandle TestEnd;
        GetWorldTimerManager().SetTimer(TestEnd, [this, Hero, Pet, bCaptureScaling, BeforeResources]()
        {
            const bool bWork = Hero->Resources > BeforeResources;
            Hero->CommandFollow();
            const bool bFollow = Pet->Activity == ECreatureActivity::Follow && !Pet->WorkSite;
            UGameplayStatics::ApplyDamage(Hero, 1000, nullptr, nullptr, nullptr);
            const bool bPass = bCaptureScaling && bWork && bFollow && Hero->Health == 0;
            UE_LOG(LogTemp, Display, TEXT("CF_SMOKE %s capture=%d work=%d follow=%d death=%d"),
                bPass ? TEXT("PASS") : TEXT("FAIL"), bCaptureScaling, bWork, bFollow, Hero->Health == 0);
            FPlatformMisc::RequestExitWithStatus(false, bPass ? 0 : 3);
        }, 5.0f, false);
    }, 1.0f, false);
}

