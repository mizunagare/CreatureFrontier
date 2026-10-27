#include "CFGameMode.h"
#include "ResourceNode.h"
#include "ResourceScenario.h"
#include "FieldWorkScenario.h"
#include "FieldRecoveryScenario.h"
#include "StorageScenario.h"
#include "CargoScenario.h"
#include "MaterialsScenario.h"
#include "CropScenario.h"
#include "HomeScenario.h"
#include "ExpeditionScenario.h"
#include "CFPlayerCharacter.h"
#include "CFHUD.h"
#include "CombatScenario.h"
#include "EncounterScenario.h"
#include "BossScenario.h"
#include "BuildingScenario.h"
#include "WorldScenario.h"
#include "FarmingScenario.h"
#include "FeedingScenario.h"
#include "WorkCycleScenario.h"
#include "SquadScenario.h"
#include "JournalScenario.h"
#include "ObjectiveScenario.h"
#include "RespawnScenario.h"
#include "EquipmentScenario.h"
#include "MeleeScenario.h"
#include "LogisticsScenario.h"
#include "SessionScenario.h"
#include "BackpackScenario.h"
#include "AnimationScenario.h"
#include "AffinityScenario.h"
#include "AffectionScenario.h"
#include "FormationScenario.h"
#include "CaptureComponent.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
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
#include "Camera/CameraComponent.h"

ACFGameMode::ACFGameMode()
{
    DefaultPawnClass = ACFPlayerCharacter::StaticClass();
    HUDClass = ACFHUD::StaticClass();
}

void ACFGameMode::BeginPlay()
{
    Super::BeginPlay();
    SeedFrontierResources(GetWorld());
    const int32 CropMode=FParse::Param(FCommandLine::Get(),TEXT("CFCropWrite"))?1:FParse::Param(FCommandLine::Get(),TEXT("CFCropRead"))?2:FParse::Param(FCommandLine::Get(),TEXT("CFCropLegacyRead"))?3:0;
    if(CropMode || FParse::Param(FCommandLine::Get(),TEXT("CFCropTest")))
    {
        FTimerHandle Start;GetWorldTimerManager().SetTimer(Start,[this,CropMode](){if(auto* Hero=Cast<ACFPlayerCharacter>(UGameplayStatics::GetPlayerCharacter(this,0))) RunCropScenario(GetWorld(),Hero,CropMode);},1.f,false);return;
    }
    const int32 MaterialsMode=FParse::Param(FCommandLine::Get(),TEXT("CFMaterialsWrite"))?1:FParse::Param(FCommandLine::Get(),TEXT("CFMaterialsRead"))?2:0;
    if(MaterialsMode || FParse::Param(FCommandLine::Get(),TEXT("CFMaterialsTest")))
    {
        FTimerHandle Start;
        GetWorldTimerManager().SetTimer(Start,[this,MaterialsMode](){if(auto* Hero=Cast<ACFPlayerCharacter>(UGameplayStatics::GetPlayerCharacter(this,0))) RunMaterialsScenario(GetWorld(),Hero,MaterialsMode);},1.f,false);
        return;
    }
    const int32 CargoMode=FParse::Param(FCommandLine::Get(),TEXT("CFCargoWrite"))?1:FParse::Param(FCommandLine::Get(),TEXT("CFCargoRead"))?2:FParse::Param(FCommandLine::Get(),TEXT("CFCargoCrystalTest"))?3:FParse::Param(FCommandLine::Get(),TEXT("CFCargoOreTest"))?4:0;
    if(CargoMode || FParse::Param(FCommandLine::Get(),TEXT("CFCargoTest")))
    {
        FTimerHandle Start;
        GetWorldTimerManager().SetTimer(Start,[this,CargoMode](){if(auto* Hero=Cast<ACFPlayerCharacter>(UGameplayStatics::GetPlayerCharacter(this,0))) RunCargoScenario(GetWorld(),Hero,CargoMode);},1.f,false);
        return;
    }
    const int32 StorageMode=FParse::Param(FCommandLine::Get(),TEXT("CFStorageWrite"))?1:FParse::Param(FCommandLine::Get(),TEXT("CFStorageRead"))?2:0;
    if(StorageMode || FParse::Param(FCommandLine::Get(),TEXT("CFStorageTest")))
    {
        FTimerHandle Start;
        GetWorldTimerManager().SetTimer(Start,[this,StorageMode](){if(auto* Hero=Cast<ACFPlayerCharacter>(UGameplayStatics::GetPlayerCharacter(this,0))) RunStorageScenario(GetWorld(),Hero,StorageMode);},1.f,false);
        return;
    }
    const int32 RecoveryMode=FParse::Param(FCommandLine::Get(),TEXT("CFFieldRecoveryWrite"))?1:FParse::Param(FCommandLine::Get(),TEXT("CFFieldRecoveryRead"))?2:0;
    if(RecoveryMode || FParse::Param(FCommandLine::Get(),TEXT("CFFieldRecoveryTest")))
    {
        FTimerHandle Start;
        GetWorldTimerManager().SetTimer(Start,[this,RecoveryMode](){if(auto* Hero=Cast<ACFPlayerCharacter>(UGameplayStatics::GetPlayerCharacter(this,0))) RunFieldRecoveryScenario(GetWorld(),Hero,RecoveryMode);},1.f,false);
        return;
    }
    const int32 FieldMode=FParse::Param(FCommandLine::Get(),TEXT("CFFieldWrite"))?1:FParse::Param(FCommandLine::Get(),TEXT("CFFieldRead"))?2:0;
    if(FieldMode || FParse::Param(FCommandLine::Get(),TEXT("CFFieldTest")))
    {
        FTimerHandle Start;
        GetWorldTimerManager().SetTimer(Start,[this,FieldMode](){if(auto* Hero=Cast<ACFPlayerCharacter>(UGameplayStatics::GetPlayerCharacter(this,0))) RunFieldWorkScenario(GetWorld(),Hero,FieldMode);},1.f,false);
        return;
    }
    const int32 ExpeditionMode=FParse::Param(FCommandLine::Get(),TEXT("CFExpeditionWrite"))?1:FParse::Param(FCommandLine::Get(),TEXT("CFExpeditionRead"))?2:0;
    if(ExpeditionMode || FParse::Param(FCommandLine::Get(),TEXT("CFExpeditionTest")))
    {
        FTimerHandle ExpeditionStart; GetWorldTimerManager().SetTimer(ExpeditionStart,[this,ExpeditionMode](){if(auto* Hero=Cast<ACFPlayerCharacter>(UGameplayStatics::GetPlayerCharacter(this,0))) RunExpeditionScenario(GetWorld(),Hero,ExpeditionMode);},1.f,false); return;
    }
    const int32 HomeMode=FParse::Param(FCommandLine::Get(),TEXT("CFHomeWrite"))?1:FParse::Param(FCommandLine::Get(),TEXT("CFHomeRead"))?2:0;
    if(HomeMode || FParse::Param(FCommandLine::Get(),TEXT("CFHomeTest")))
    {
        FTimerHandle HomeStart;
        GetWorldTimerManager().SetTimer(HomeStart,[this,HomeMode](){if(auto* Hero=Cast<ACFPlayerCharacter>(UGameplayStatics::GetPlayerCharacter(this,0))) RunHomeScenario(GetWorld(),Hero,HomeMode);},1.f,false); return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("CFResourceTest")))
    {
        FTimerHandle ResourceStart;
        GetWorldTimerManager().SetTimer(ResourceStart,[this]() { if(auto* Hero=Cast<ACFPlayerCharacter>(UGameplayStatics::GetPlayerCharacter(this,0))) RunResourceScenario(GetWorld(),Hero); },1.f,false);
        return;
    }
    const bool bSaveWrite = FParse::Param(FCommandLine::Get(), TEXT("CFSaveWrite"));
    const bool bSaveRead = FParse::Param(FCommandLine::Get(), TEXT("CFSaveRead"));
    const bool bInventoryTest = FParse::Param(FCommandLine::Get(), TEXT("CFInventoryTest"));
    const bool bNavTest = FParse::Param(FCommandLine::Get(), TEXT("CFNavTest"));
    const bool bCareTest = FParse::Param(FCommandLine::Get(), TEXT("CFCareTest"));
    const bool bSpeciesTest = FParse::Param(FCommandLine::Get(), TEXT("CFSpeciesTest"));
    const bool bCombatTest = FParse::Param(FCommandLine::Get(), TEXT("CFCombatTest"));
    const bool bEncounterTest = FParse::Param(FCommandLine::Get(), TEXT("CFEncounterTest"));
    const bool bBossTest = FParse::Param(FCommandLine::Get(), TEXT("CFBossTest"));
    const bool bBuildingTest = FParse::Param(FCommandLine::Get(), TEXT("CFBuildingTest"));
    const bool bWorldTest = FParse::Param(FCommandLine::Get(), TEXT("CFWorldTest"));
    const bool bWorldPreview = FParse::Param(FCommandLine::Get(), TEXT("CFWorldPreview"));
    const bool bFarmingTest = FParse::Param(FCommandLine::Get(), TEXT("CFFarmingTest"));
    const bool bFarmPreview = FParse::Param(FCommandLine::Get(), TEXT("CFFarmPreview"));
    const bool bFeedingTest = FParse::Param(FCommandLine::Get(), TEXT("CFFeedingTest"));
    const bool bWorkCycleTest = FParse::Param(FCommandLine::Get(), TEXT("CFWorkCycleTest"));
    const bool bSquadTest = FParse::Param(FCommandLine::Get(), TEXT("CFSquadTest"));
    const bool bJournalTest = FParse::Param(FCommandLine::Get(), TEXT("CFJournalTest"));
    const bool bObjectiveTest = FParse::Param(FCommandLine::Get(), TEXT("CFObjectiveTest"));
    const bool bRespawnTest = FParse::Param(FCommandLine::Get(), TEXT("CFRespawnTest"));
    const bool bEquipmentTest = FParse::Param(FCommandLine::Get(), TEXT("CFEquipmentTest"));
    const bool bMeleeTest = FParse::Param(FCommandLine::Get(), TEXT("CFMeleeTest"));
    if(FParse::Param(FCommandLine::Get(),TEXT("CFFormationTest")))
    {
        FTimerHandle FormationStart;
        GetWorldTimerManager().SetTimer(FormationStart,[this]() { if(auto* Hero=Cast<ACFPlayerCharacter>(UGameplayStatics::GetPlayerCharacter(this,0))) RunFormationScenario(GetWorld(),Hero); },1.f,false);
        return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("CFAffectionTest")))
    {
        FTimerHandle AffectionStart;
        GetWorldTimerManager().SetTimer(AffectionStart,[this]() { if(auto* Hero=Cast<ACFPlayerCharacter>(UGameplayStatics::GetPlayerCharacter(this,0))) RunAffectionScenario(GetWorld(),Hero); },1.f,false);
        return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("CFAffinityTest")))
    {
        FTimerHandle AffinityStart;
        GetWorldTimerManager().SetTimer(AffinityStart,[this]() { if(auto* Hero=Cast<ACFPlayerCharacter>(UGameplayStatics::GetPlayerCharacter(this,0))) RunAffinityScenario(GetWorld(),Hero); },1.f,false);
        return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("CFAnimationTest")))
    {
        FTimerHandle AnimationStart;
        GetWorldTimerManager().SetTimer(AnimationStart,[this]() { if(auto* Hero=Cast<ACFPlayerCharacter>(UGameplayStatics::GetPlayerCharacter(this,0))) RunAnimationScenario(GetWorld(),Hero); },1.f,false);
        return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("CFBackpackTest")))
    {
        FTimerHandle BackpackStart;
        GetWorldTimerManager().SetTimer(BackpackStart,[this]() { if(auto* Hero=Cast<ACFPlayerCharacter>(UGameplayStatics::GetPlayerCharacter(this,0))) RunBackpackScenario(GetWorld(),Hero); },1.f,false);
        return;
    }
    const bool bSessionTest = FParse::Param(FCommandLine::Get(), TEXT("CFSessionTest"));
    if(bSessionTest)
    {
        FTimerHandle SessionStart;
        GetWorldTimerManager().SetTimer(SessionStart,[this]() { if(auto* Hero=Cast<ACFPlayerCharacter>(UGameplayStatics::GetPlayerCharacter(this,0))) RunSessionScenario(GetWorld(),Hero); },1.f,false);
        return;
    }
    const bool bLogisticsTest = FParse::Param(FCommandLine::Get(), TEXT("CFLogisticsTest"));
    if (!bSaveWrite && !bSaveRead && !bInventoryTest && !bNavTest && !bCareTest && !bSpeciesTest && !bCombatTest && !bEncounterTest && !bBossTest && !bBuildingTest && !bWorldTest && !bWorldPreview && !bFarmingTest && !bFarmPreview && !bFeedingTest && !bWorkCycleTest && !bSquadTest && !bJournalTest && !bObjectiveTest && !bRespawnTest && !bEquipmentTest && !bMeleeTest && !bLogisticsTest && !FParse::Param(FCommandLine::Get(), TEXT("CFSmokeTest"))) return;
    FTimerHandle TestStart;
    GetWorldTimerManager().SetTimer(TestStart, [this, bSaveWrite, bSaveRead, bInventoryTest, bNavTest, bCareTest, bSpeciesTest, bCombatTest, bEncounterTest, bBossTest, bBuildingTest, bWorldTest, bWorldPreview, bFarmingTest, bFarmPreview, bFeedingTest, bWorkCycleTest, bSquadTest, bJournalTest, bObjectiveTest, bRespawnTest, bEquipmentTest, bMeleeTest, bLogisticsTest]()
    {
        auto* Hero = Cast<ACFPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
        if (!Hero) { FPlatformMisc::RequestExitWithStatus(true, 1); return; }
        if (bLogisticsTest) { RunLogisticsScenario(GetWorld(),Hero); return; }
        if (bMeleeTest) { RunMeleeScenario(GetWorld(),Hero); return; }
        if (bEquipmentTest) { RunEquipmentScenario(GetWorld(),Hero); return; }
        if (bRespawnTest) { RunRespawnScenario(GetWorld(),Hero); return; }
        if (bObjectiveTest) { RunObjectiveScenario(GetWorld(),Hero); return; }
        if (bJournalTest) { RunJournalScenario(GetWorld(),Hero); return; }
        if (bSquadTest) { RunSquadScenario(GetWorld(),Hero); return; }
        if (bWorkCycleTest) { RunWorkCycleScenario(GetWorld(),Hero); return; }
        if (bFeedingTest) { RunFeedingScenario(GetWorld(),Hero); return; }
        if (bFarmPreview) { RunFarmPreview(GetWorld(),Hero); return; }
        if (bFarmingTest) { RunFarmingScenario(GetWorld(),Hero); return; }
        if (bWorldPreview) { RunValleyPreview(GetWorld(),Hero); return; }
        if (bWorldTest) { RunEcologyScenario(GetWorld(),Hero); return; }
        if (bBuildingTest) { RunBuildingScenario(GetWorld(),Hero); return; }
        if (bBossTest) { RunBossScenario(GetWorld(), Hero); return; }
        if (bEncounterTest) { RunEncounterScenario(GetWorld(), Hero); return; }
        if (bCombatTest) { RunCompanionCombatScenario(GetWorld(), Hero); return; }
        if (bSpeciesTest)
        {
            TArray<ACreatureCharacter*> Pets;
            TSet<FString> Names;
            bool bData = true, bAppearance = true, bSpecialty = true;
            Hero->SetActorLocation(FVector(-450,0,40));
            Hero->GetCharacterMovement()->DisableMovement();
            for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
            {
                auto* Pet = *It;
                if (!Pet->Definition) { bData = false; continue; }
                Names.Add(Pet->GetCreatureName());
                bData &= Pet->Health == Pet->GetMaxHealth() && Pet->GetCharacterMovement()->MaxWalkSpeed == Pet->Definition->MoveSpeed &&
                    FMath::IsNearlyEqual(Hero->CaptureComponent->CalculateCaptureChance(Pet), Pet->Definition->BaseCaptureRate);
                auto* Material = Cast<UMaterialInstanceDynamic>(Pet->CreatureVisual->GetMaterial(0));
                TArray<UStaticMeshComponent*> Pieces;
                Pet->GetComponents(Pieces);
                int32 Visible = 0;
                for (const auto* Piece : Pieces) if (Piece->IsVisible()) ++Visible;
                bAppearance &= Material && Material->K2_GetVectorParameterValue(TEXT("Tint")).Equals(Pet->Definition->BodyColor, 0.01f) && Visible >= 11;
                AWorkStation* Matching = nullptr;
                AWorkStation* Other = nullptr;
                for (TActorIterator<AWorkStation> Site(GetWorld()); Site; ++Site)
                    if (Pet->Definition->WorkSpecialties.Contains(Site->RequiredSpecialty)) Matching = *Site;
                    else Other = *Site;
                bSpecialty &= Matching && Other && Pet->GetWorkEfficiency(Matching) > Pet->GetWorkEfficiency(Other);
                if (!Matching) continue;
                Pet->Tame(Hero->GetController());
                Pet->SetActorLocation(Matching->GetActorLocation()+FVector(0,180,40));
                Pet->WorkSite = Matching;
                Pet->SetActivity(ECreatureActivity::Work);
                Pets.Add(Pet);
            }
            bData &= Names.Num() == 3 && Pets.Num() == 3;
            FTimerHandle SpeciesCheck;
            GetWorldTimerManager().SetTimer(SpeciesCheck, [Hero, Pets, bData, bAppearance, bSpecialty]()
            {
                bool bWork = Pets.Num() == 3;
                for (const auto* Pet : Pets) bWork &= Pet->Experience >= 2;
                const FString Slot = TEXT("FrontierSpeciesAutomation");
                const bool bSaved = Hero->SaveProgress(Slot);
                const bool bLoaded = Hero->LoadProgress(Slot);
                TSet<FString> RestoredNames;
                bool bMaterials = true;
                for (TActorIterator<ACreatureCharacter> It(Hero->GetWorld()); It; ++It)
                {
                    if (!It->Definition || It->Tamer != Hero->GetController()) continue;
                    RestoredNames.Add(It->GetCreatureName());
                    auto* Material = Cast<UMaterialInstanceDynamic>(It->CreatureVisual->GetMaterial(0));
                    bMaterials &= Material && Material->K2_GetVectorParameterValue(TEXT("Tint")).Equals(It->Definition->BodyColor, 0.01f);
                }
                const bool bPersist = bSaved && bLoaded && RestoredNames.Num() == 3 && bMaterials;
                const bool bPass = bData && bAppearance && bSpecialty && bWork && bPersist;
                UE_LOG(LogTemp, Display, TEXT("CF_SPECIES %s data=%d appearance=%d specialty=%d work=%d persist=%d"), bPass ? TEXT("PASS") : TEXT("FAIL"), bData, bAppearance, bSpecialty, bWork, bPersist);
                FPlatformMisc::RequestExitWithStatus(false, bPass ? 0 : 10);
            }, 6.5f, false);
            return;
        }
        if (bCareTest)
        {
            for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It) It->Destroy();
            Hero->SetActorLocation(FVector(-450,0,40));
            Hero->GetCharacterMovement()->DisableMovement();
            auto* Pet = GetWorld()->SpawnActor<ACreatureCharacter>(FVector(-450,180,40), FRotator::ZeroRotator);
            auto* Enemy = GetWorld()->SpawnActor<ACreatureCharacter>(FVector(-340,180,40), FRotator::ZeroRotator);
            if (!Pet || !Enemy) { FPlatformMisc::RequestExitWithStatus(true, 2); return; }
            Pet->Tame(Hero->GetController());
            Pet->AddExperience(75);
            UGameplayStatics::ApplyDamage(Enemy, 1000, Hero->GetController(), Pet, nullptr);
            UGameplayStatics::ApplyDamage(Enemy, 1000, Hero->GetController(), Pet, nullptr);
            const bool bGrowth = Pet->Level == 2 && Pet->Experience == 25 && Pet->GetMaxHealth() == 120 && Pet->GetAttackDamage() == 12;
            Enemy->Destroy();
            auto* Attacker = GetWorld()->SpawnActor<ACreatureCharacter>(FVector(-340,180,40), FRotator::ZeroRotator);
            if (!Attacker) { FPlatformMisc::RequestExitWithStatus(true, 2); return; }
            Attacker->Tick(0.1f);
            const bool bPetDamaged = Pet->Health == 110;
            UGameplayStatics::ApplyDamage(Pet, 1000, Attacker->GetController(), Attacker, nullptr);
            Attacker->Destroy();
            Hero->Inventory->Restore({{EFrontierItem::Material,6},{EFrontierItem::CaptureDevice,5},{EFrontierItem::Medicine,3}});
            Hero->FirstPersonCamera->SetWorldRotation((Pet->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());
            Hero->HealCompanion();
            const bool bTreatment = Pet->Health == 40 && Hero->Inventory->Count(EFrontierItem::Medicine) == 2;
            Pet->Stamina = 0;
            Hero->CommandWork();
            Pet->Tick(0.1f);
            const bool bFatigue = Pet->Activity == ECreatureActivity::Rest && Pet->Stamina == 0;
            Hero->CommandRest();
            const FString Slot = TEXT("FrontierCareAutomation");
            const bool bSaved = Hero->SaveProgress(Slot);
            Pet->Level = 1; Pet->Experience = 0; Pet->Stamina = 100; Pet->Health = 10;
            const bool bLoaded = Hero->LoadProgress(Slot);
            ACreatureCharacter* Restored = nullptr;
            for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
                if (It->Tamer == Hero->GetController()) { Restored = *It; break; }
            const bool bPersist = bSaved && bLoaded && Restored && Restored->Level == 2 && Restored->Experience == 25 && Restored->Stamina == 0 && Restored->Health == 40 && Restored->WorkSite && Restored->Activity == ECreatureActivity::Rest;
            if (!Restored) { FPlatformMisc::RequestExitWithStatus(true, 9); return; }
            FTimerHandle RestCheck;
            GetWorldTimerManager().SetTimer(RestCheck, [Hero, Restored, bGrowth, bPetDamaged, bTreatment, bPersist, bFatigue]()
            {
                const bool bRest = Restored->Health > 50 && Restored->Stamina > 30 && Restored->Health <= Restored->GetMaxHealth();
                Hero->CommandFollow();
                const bool bResume = Restored->Activity == ECreatureActivity::Follow;
                const bool bPass = bGrowth && bPetDamaged && bTreatment && bPersist && bRest && bResume && bFatigue;
                UE_LOG(LogTemp, Display, TEXT("CF_CARE %s growth=%d petDamage=%d treatment=%d persist=%d rest=%d resume=%d fatigue=%d"), bPass ? TEXT("PASS") : TEXT("FAIL"), bGrowth, bPetDamaged, bTreatment, bPersist, bRest, bResume, bFatigue);
                FPlatformMisc::RequestExitWithStatus(false, bPass ? 0 : 9);
            }, 3.0f, false);
            return;
        }
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
                const bool bPass = bPath && bBlocked && bArrived && MaxDetour > 100.0f;
                UE_LOG(LogTemp, Display, TEXT("CF_NAV %s path=%d barrier=%d arrived=%d detour=%.1f"), bPass ? TEXT("PASS") : TEXT("FAIL"), bPath, bBlocked, bArrived, MaxDetour);
                if (!bPass || !bMelee)
                {
                    GetWorldTimerManager().ClearTimer(*Poll);
                    FPlatformMisc::RequestExitWithStatus(false, 7);
                    return;
                }
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
                // Clear only after the final capture access: clearing destroys this delegate.
                GetWorldTimerManager().ClearTimer(*Poll);
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
                    if (It->WorkSite && It->Activity == ECreatureActivity::Work && It->Health == 75 &&
                        It->Level == 4 && It->Experience == 123 && It->Stamina == 62 && It->SpecialCooldownRemaining == 5.5f) ++Workers;
                }
            }
            const bool bValues = Hero->Health == 67 && Hero->Inventory->Count(EFrontierItem::Material) == 42 &&
                Hero->Inventory->Count(EFrontierItem::CaptureDevice) == 7 && Hero->Inventory->Count(EFrontierItem::Medicine) == 3 &&
                Hero->GetActorLocation().Equals(FVector(-450, 0, 120), 1.0f);
            const bool bSecondLoad = Hero->LoadProgress(TestSlot);
            int32 ResourceCount=0,Depleted=0;
            for(TActorIterator<AResourceNode> It(GetWorld());It;++It)
            {
                ++ResourceCount;
                if(It->PersistentId==FGuid(0xCF170000,0,0,1) && It->Remaining==0 && It->RegrowRemaining==123 && It->HarvestCooldown==.6f) ++Depleted;
            }
            int32 RepeatedCount = 0;
            for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It) ++RepeatedCount;
            const bool bPass = bLoaded && bValues && bSecondLoad && Count == RepeatedCount && Owned == 1 && Workers == 1 && ResourceCount==10 && Depleted==1;
            UE_LOG(LogTemp, Display, TEXT("CF_SAVE_READ %s loaded=%d second=%d values=%d owned=%d workers=%d count=%d repeated=%d resources=%d depleted=%d"),
                bPass ? TEXT("PASS") : TEXT("FAIL"), bLoaded,bSecondLoad,bValues,Owned,Workers,Count,RepeatedCount,ResourceCount,Depleted);
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
            Pet->Level = 4; Pet->Experience = 123; Pet->Stamina = 62;
            Pet->SpecialCooldownRemaining = 5.5f;
            for(TActorIterator<AResourceNode> It(GetWorld());It;++It)
                if(It->PersistentId==FGuid(0xCF170000,0,0,1)) { It->Remaining=0; It->RegrowRemaining=123; It->HarvestCooldown=.6f; }
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

