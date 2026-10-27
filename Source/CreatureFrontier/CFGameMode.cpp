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

ACFGameMode::ACFGameMode()
{
    DefaultPawnClass = ACFPlayerCharacter::StaticClass();
    HUDClass = ACFHUD::StaticClass();
}

void ACFGameMode::BeginPlay()
{
    Super::BeginPlay();
    if (!FParse::Param(FCommandLine::Get(), TEXT("CFSmokeTest"))) return;
    FTimerHandle TestStart;
    GetWorldTimerManager().SetTimer(TestStart, [this]()
    {
        auto* Hero = Cast<ACFPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
        if (!Hero) { FPlatformMisc::RequestExitWithStatus(true, 1); return; }
        auto* Pet = GetWorld()->SpawnActor<ACreatureCharacter>(Hero->GetActorLocation() + FVector(200, 0, 100), FRotator::ZeroRotator);
        if (!Pet) { FPlatformMisc::RequestExitWithStatus(true, 2); return; }
        const float Before = Hero->CaptureComponent->CalculateCaptureChance(Pet);
        UGameplayStatics::ApplyDamage(Pet, 25, Hero->GetController(), Hero, nullptr);
        const bool bCaptureScaling = Before > 0 && Hero->CaptureComponent->CalculateCaptureChance(Pet) > Before;
        Pet->Tame(Hero->GetController());
        Hero->SetActorLocation(FVector(-450, 0, 120));
        Pet->SetActorLocation(FVector(-450, 180, 120));
        Hero->CommandWork();
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

