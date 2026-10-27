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
#include "InventoryComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "GameFramework/CharacterMovementComponent.h"

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
    const bool bInventoryTest = FParse::Param(FCommandLine::Get(), TEXT("CFInventoryTest"));
    const bool bNavTest = FParse::Param(FCommandLine::Get(), TEXT("CFNavTest"));
    if (!bSaveWrite && !bSaveRead && !bInventoryTest && !bNavTest && !FParse::Param(FCommandLine::Get(), TEXT("CFSmokeTest"))) return;
    FTimerHandle TestStart;
    GetWorldTimerManager().SetTimer(TestStart, [this, bSaveWrite, bSaveRead, bInventoryTest, bNavTest]()
    {
        auto* Hero = Cast<ACFPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
        if (!Hero) { FPlatformMisc::RequestExitWithStatus(true, 1); return; }
        if (bNavTest)
        {
            for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It) It->Destroy();
            Hero->GetCharacterMovement()->DisableMovement();
            Hero->SetActorLocation(FVector(910,500,40));
            auto* Attacker = GetWorld()->SpawnActor<ACreatureCharacter>(FVector(690,500,40), FRotator::ZeroRotator);
            if (!Attacker) { FPlatformMisc::RequestExitWithStatus(true, 2); return; }
            Attacker->AttackRange = 400;
            Attacker->Tick(0.1f);
            const bool bWallBlocked = Hero->Health == 100;
            Hero->SetActorLocation(FVector(690,800,40));
            Attacker->Tick(0.1f);
            const bool bOpenHit = Hero->Health == 90;
            Attacker->Destroy();
            Hero->Health = 100;
            const bool bMelee = bWallBlocked && bOpenHit;
            UE_LOG(LogTemp, Display, TEXT("CF_MELEE %s wallBlocked=%d openHit=%d"), bMelee ? TEXT("PASS") : TEXT("FAIL"), bWallBlocked, bOpenHit);
            Hero->SetActorLocation(FVector(1100,500,40));
            auto* Pet = GetWorld()->SpawnActor<ACreatureCharacter>(FVector(500,500,40), FRotator::ZeroRotator);
            if (!Pet) { FPlatformMisc::RequestExitWithStatus(true, 2); return; }
            Pet->Tame(Hero->GetController());
            auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(this, Pet->GetActorLocation(), Hero->GetActorLocation(), Pet);
            const bool bPath = Path && Path->IsValid() && !Path->IsPartial() && Path->PathPoints.Num() >= 3;
            FHitResult Hit;
            FCollisionQueryParams Params(SCENE_QUERY_STAT(NavTestBarrier), false, Pet);
            const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, Pet->GetActorLocation(), Hero->GetActorLocation(), ECC_Visibility, Params) && Hit.GetActor() != Hero;
            const double Started = GetWorld()->GetTimeSeconds();
            TSharedRef<FTimerHandle> Poll = MakeShared<FTimerHandle>();
            GetWorldTimerManager().SetTimer(*Poll, [this, Hero, Pet, bPath, bBlocked, bMelee, Started, Poll, MaxDetour=0.0f]() mutable
            {
                MaxDetour = FMath::Max(MaxDetour, FMath::Abs(static_cast<float>(Pet->GetActorLocation().Y)-500.0f));
                const bool bArrived = Pet->GetDistanceTo(Hero) <= 240.0f;
                if (!bArrived && GetWorld()->GetTimeSeconds()-Started < 12.0) return;
                GetWorldTimerManager().ClearTimer(*Poll);
                const bool bPass = bPath && bBlocked && bArrived && MaxDetour > 100.0f;
                UE_LOG(LogTemp, Display, TEXT("CF_NAV %s path=%d barrier=%d arrived=%d detour=%.1f"), bPass ? TEXT("PASS") : TEXT("FAIL"), bPath, bBlocked, bArrived, MaxDetour);
                if (!bPass || !bMelee) { FPlatformMisc::RequestExitWithStatus(false, 7); return; }
                Hero->SetActorLocation(FVector(-450,0,40));
                Hero->CommandWork();
                const int32 MaterialsBefore = Hero->Inventory->Count(EFrontierItem::Material);
                FTimerHandle WorkCheck;
                GetWorldTimerManager().SetTimer(WorkCheck, [Hero, Pet, MaterialsBefore]()
                {
                    const bool bWorked = Pet->Activity == ECreatureActivity::Work && Hero->Inventory->Count(EFrontierItem::Material) > MaterialsBefore;
                    UE_LOG(LogTemp, Display, TEXT("CF_NAV_WORK %s materials=%d"), bWorked ? TEXT("PASS") : TEXT("FAIL"), Hero->Inventory->Count(EFrontierItem::Material));
                    FPlatformMisc::RequestExitWithStatus(false, bWorked ? 0 : 8);
                }, 12.0f, false);
            }, 0.1f, true);
            return;
        }
        if (bInventoryTest)
        {
            auto* Bag = Hero->Inventory.Get();
            Bag->Restore({{EFrontierItem::Material, 10}, {EFrontierItem::CaptureDevice, 5}, {EFrontierItem::Medicine, 1}});
            Hero->SetActorLocation(FVector(-450, 0, 120));
            Hero->CraftCaptureDevice();
            Hero->CraftMedicine();
            const bool bCraft = Bag->Count(EFrontierItem::Material) == 2 && Bag->Count(EFrontierItem::CaptureDevice) == 6 && Bag->Count(EFrontierItem::Medicine) == 2;
            Hero->CraftMedicine();
            const bool bNoOverdraft = Bag->Count(EFrontierItem::Material) == 2 && Bag->Count(EFrontierItem::Medicine) == 2 && !Bag->Consume(EFrontierItem::Material, -1);
            Hero->Health = 70;
            Hero->UseMedicine();
            Hero->UseMedicine();
            const bool bHeal = Hero->Health == 100 && Bag->Count(EFrontierItem::Medicine) == 1;
            auto* Wild = GetWorld()->SpawnActor<ACreatureCharacter>(Hero->GetActorLocation()+FVector(300,0,0), FRotator::ZeroRotator);
            if (!Wild) { FPlatformMisc::RequestExitWithStatus(true, 2); return; }
            Hero->CaptureComponent->TryCapture(Wild);
            const bool bConsume = Bag->Count(EFrontierItem::CaptureDevice) == 5;
            const FString Slot = TEXT("FrontierInventoryAutomation");
            const bool bSaved = Hero->SaveProgress(Slot);
            Bag->Restore({});
            const bool bLoaded = Hero->LoadProgress(Slot);
            const bool bPersist = bSaved && bLoaded && Bag->Count(EFrontierItem::CaptureDevice) == 5 && Bag->Count(EFrontierItem::Material) == 2 && Bag->Count(EFrontierItem::Medicine) == 1;
            const bool bPass = bCraft && bNoOverdraft && bHeal && bConsume && bPersist;
            UE_LOG(LogTemp, Display, TEXT("CF_INVENTORY %s craft=%d noOverdraft=%d heal=%d consume=%d persist=%d"), bPass ? TEXT("PASS") : TEXT("FAIL"), bCraft, bNoOverdraft, bHeal, bConsume, bPersist);
            FPlatformMisc::RequestExitWithStatus(false, bPass ? 0 : 6);
            return;
        }
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
            const bool bValues = Hero->Health == 67 && Hero->Inventory->Count(EFrontierItem::Material) == 42 &&
                Hero->Inventory->Count(EFrontierItem::CaptureDevice) == 7 && Hero->Inventory->Count(EFrontierItem::Medicine) == 3 &&
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
            Hero->Inventory->Restore({{EFrontierItem::Material, 42}, {EFrontierItem::CaptureDevice, 7}, {EFrontierItem::Medicine, 3}});
            const bool bSaved = Hero->SaveProgress(TestSlot);
            UE_LOG(LogTemp, Display, TEXT("CF_SAVE_WRITE %s"), bSaved ? TEXT("PASS") : TEXT("FAIL"));
            FPlatformMisc::RequestExitWithStatus(false, bSaved ? 0 : 5);
            return;
        }
        const int32 BeforeResources = Hero->Inventory->Count(EFrontierItem::Material);
        FTimerHandle TestEnd;
        GetWorldTimerManager().SetTimer(TestEnd, [this, Hero, Pet, bCaptureScaling, BeforeResources]()
        {
            const bool bWork = Hero->Inventory->Count(EFrontierItem::Material) > BeforeResources;
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

