#include "WorldScenario.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "CFPlayerCharacter.h"
#include "WildEncounter.h"
#include "CaptureComponent.h"
#include "InventoryComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/HUD.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "AIController.h"

void RunEcologyScenario(UWorld* World, ACFPlayerCharacter* Hero)
{
    for (TActorIterator<ACreatureCharacter> It(World); It; ++It) It->Destroy();
    Hero->SetActorLocation(FVector(-500,-1500,40)); Hero->GetCharacterMovement()->DisableMovement();
    AWildEncounter* Site = nullptr;
    for (TActorIterator<AWildEncounter> It(World); It; ++It) if (It->EncounterId == TEXT("MeadowGrove")) Site = *It;
    if (!Site) { UE_LOG(LogTemp,Error,TEXT("CF_WORLD FAIL missing grove")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Site->Tick(0);
    auto* Resident = Site->GetResident();
    if (!Resident) { UE_LOG(LogTemp,Error,TEXT("CF_WORLD FAIL spawn")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    int32 RoamSeed=0;
    if(FParse::Value(FCommandLine::Get(),TEXT("CFRoamSeed="),RoamSeed))
    {
        FMath::RandInit(RoamSeed);
        UE_LOG(LogTemp,Display,TEXT("CF_WORLD_SEED %d"),RoamSeed);
    }
    const bool FrozenControl=FParse::Param(FCommandLine::Get(),TEXT("CFWorldFreeze"));
    if(FrozenControl) Resident->GetCharacterMovement()->DisableMovement();
    UE_LOG(LogTemp,Display,TEXT("CF_WORLD_FIXTURE frozen=%d"),FrozenControl);
    int32 Props = 0;
    for (TActorIterator<AActor> It(World); It; ++It) if (It->ActorHasTag(TEXT("CF_Valley"))) ++Props;
    bool Routes = true;
    for (FVector End : {FVector(-1500,1400,40),FVector(1500,1400,40)})
    {
        // Start at the playable base entrance, not inside the original gathering bench.
        auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World,FVector(0,0,40),End,Hero);
        Routes &= Path && Path->IsValid() && !Path->IsPartial();
        UE_LOG(LogTemp,Display,TEXT("CF_WORLD_ROUTE %s valid=%d partial=%d points=%d"),*End.ToString(),Path && Path->IsValid(),Path && Path->IsPartial(),Path ? Path->PathPoints.Num() : 0);
    }
    FHitResult Edge;
    const bool Boundary = World->LineTraceSingleByChannel(Edge,FVector(1900,0,100),FVector(2300,0,100),ECC_Visibility);
    const bool Scene = Props >= 100 && Routes && Boundary;
    UE_LOG(LogTemp,Display,TEXT("CF_WORLD_SCENE props=%d routes=%d boundary=%d"),Props,Routes,Boundary);
    const FVector Home = Resident->HabitatHome;
    const double Started = World->GetTimeSeconds();
    TSharedRef<FTimerHandle> Poll = MakeShared<FTimerHandle>();
    World->GetTimerManager().SetTimer(*Poll,[World,Hero,Site,Resident,Home,Scene,Started,Poll,FrozenControl,
        MaxTravel=0.0f,Within=true,Previous=Resident->GetActorLocation(),PreviousTime=Started,
        PathTravel=0.0f,CompletedLegs=0,InMotionLeg=false,SawWander=false,Continuous=true]() mutable
    {
        const double Now=World->GetTimeSeconds();
        const float Step=FVector::Dist2D(Previous,Resident->GetActorLocation());
        const float SpeedLimit=Resident->Definition?Resident->Definition->MoveSpeed:320.f;
        Continuous &= Step<=SpeedLimit*(Now-PreviousTime)+2.f;
        PathTravel+=Step;
        if(Resident->Activity==ECreatureActivity::Wander && Step>.01f) { InMotionLeg=true; SawWander=true; }
        if(Resident->Activity==ECreatureActivity::Idle && InMotionLeg) { ++CompletedLegs; InMotionLeg=false; }
        Previous=Resident->GetActorLocation(); PreviousTime=Now;
        const float Travel = FVector::Dist2D(Resident->GetActorLocation(),Home);
        MaxTravel = FMath::Max(MaxTravel,Travel); Within &= Travel < Resident->RoamRadius+100;
        if(FParse::Param(FCommandLine::Get(),TEXT("CFRoamTrace")))
        {
            const auto* AI=Cast<AAIController>(Resident->GetController());
            UE_LOG(LogTemp,Display,TEXT("CF_WORLD_SAMPLE elapsed=%.3f travel=%.3f speed=%.3f activity=%d move=%d location=%s next=%s"),
                World->GetTimeSeconds()-Started,Travel,Resident->GetVelocity().Size2D(),static_cast<int32>(Resident->Activity),
                AI?static_cast<int32>(AI->GetMoveStatus()):-1,*Resident->GetActorLocation().ToString(),
                AI?*AI->GetImmediateMoveDestination().ToString():TEXT("none"));
        }
        // Net displacement alone rejects valid short out-and-back patrols.
        // Keep the original 35cm gate, or require TWO observed moving->idle
        // legs with >=35cm continuous travel. An immobile Wander cannot pass.
        const bool Movement=MaxTravel>=35 || (CompletedLegs>=2 && PathTravel>=35);
        if (!Movement && Now-Started < 8) return;
        const bool Roam = Movement && Within && Continuous && SawWander;
        UE_LOG(LogTemp,Display,TEXT("CF_WORLD_ROAM pass=%d travel=%.2f path=%.2f legs=%d continuous=%d within=%d radius=%.1f location=%s home=%s"),Roam,MaxTravel,PathTravel,CompletedLegs,Continuous,Within,Resident->RoamRadius,*Resident->GetActorLocation().ToString(),*Home.ToString());
        if(FrozenControl) Resident->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        const FString Slot = TEXT("FrontierWorldAutomation");
        bool Saved = Hero->SaveProgress(Slot) && Hero->LoadProgress(Slot);
        auto* Returning = Site->GetResident();
        Saved &= Returning && Returning->bHasHabitat && Returning->HabitatHome.Equals(Home,.01);
        if (!Returning)
        {
            UE_LOG(LogTemp,Error,TEXT("CF_WORLD FAIL restore"));
            World->GetTimerManager().ClearTimer(*Poll); FPlatformMisc::RequestExitWithStatus(false,1); return;
        }
        Returning->Health = 50;
        Returning->SetActorLocation(Home+FVector(1100,0,0));
        Returning->Tick(0);
        Hero->SetActorLocation(Home+FVector(1500,0,0));
        const int32 Devices = Hero->Inventory->Count(EFrontierItem::CaptureDevice);
        const float Damage = UGameplayStatics::ApplyDamage(Returning,20,Hero->GetController(),Hero,nullptr);
        Hero->CaptureComponent->TryCapture(Returning);
        Returning->Tame(Hero->GetController());
        const bool Immune = Returning->Activity == ECreatureActivity::ReturnHome && Damage == 0 && Returning->Health == 50 &&
            Hero->CaptureComponent->CalculateCaptureChance(Returning) == 0 && Returning->Disposition != ECreatureDisposition::Tamed &&
            Hero->Inventory->Count(EFrontierItem::CaptureDevice) == Devices;
        Saved &= Hero->SaveProgress(Slot) && Hero->LoadProgress(Slot);
        FTimerHandle Returned;
        World->GetTimerManager().SetTimer(Returned,[World,Hero,Site,Home,Slot,Scene,Roam,Saved,Immune]()
        {
            auto* Wild = Site->GetResident();
            const bool Reset = Wild && FVector::Dist2D(Wild->GetActorLocation(),Home) < Wild->RoamRadius+100 && Wild->Health == Wild->GetMaxHealth() && Wild->Activity != ECreatureActivity::ReturnHome;
            if (Wild) UE_LOG(LogTemp,Display,TEXT("CF_WORLD_RETURN distance=%.1f health=%.1f activity=%d"),FVector::Dist2D(Wild->GetActorLocation(),Home),Wild->Health,static_cast<int32>(Wild->Activity));
            bool Tamed = false, Persisted = Saved;
            if (Wild)
            {
                Wild->Tame(Hero->GetController());
                Wild->SetActorLocation(Home+FVector(0,-1800,0)); Wild->Tick(0);
                Tamed = !Wild->bHasHabitat && Wild->Disposition == ECreatureDisposition::Tamed && Wild->Activity != ECreatureActivity::ReturnHome;
                Persisted &= Hero->SaveProgress(Slot) && Hero->LoadProgress(Slot);
                for (TActorIterator<ACreatureCharacter> It(World); It; ++It)
                    if (It->Tamer == Hero->GetController()) Tamed &= !It->bHasHabitat;
            }
            const bool Pass = Scene && Roam && Saved && Immune && Reset && Tamed && Persisted;
            UE_LOG(LogTemp,Display,TEXT("CF_WORLD %s scene=%d roam=%d save=%d immune=%d reset=%d tame=%d"),
                Pass ? TEXT("PASS") : TEXT("FAIL"),Scene,Roam,Persisted,Immune,Reset,Tamed);
            FPlatformMisc::RequestExitWithStatus(false,Pass ? 0 : 1);
        },7.0f,false);
        // Last capture access: clearing a repeating timer destroys its running delegate.
        World->GetTimerManager().ClearTimer(*Poll);
    },.05f,true);
}

void RunValleyPreview(UWorld* World, ACFPlayerCharacter* Hero)
{
    for (TActorIterator<AWildEncounter> It(World); It; ++It) It->Tick(0);
    for (TActorIterator<ACreatureCharacter> It(World); It; ++It)
    { It->SetActorTickEnabled(false); It->GetCharacterMovement()->DisableMovement(); }
    Hero->GetCharacterMovement()->DisableMovement();
    auto* PC = Cast<APlayerController>(Hero->GetController());
    if (!PC) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    if (PC->GetHUD()) PC->GetHUD()->bShowHUD = false;
    auto* Camera = World->SpawnActor<ACameraActor>();
    Camera->GetCameraComponent()->FieldOfView = 75;
    PC->SetViewTarget(Camera);
    const TArray<FVector> Positions = {FVector(450,-1400,850),FVector(-450,700,330),FVector(400,500,400)};
    const TArray<FVector> Targets = {FVector(-550,0,80),FVector(-1500,1400,100),FVector(1500,1400,120)};
    const TArray<FString> Names = {TEXT("valley_base"),TEXT("valley_meadow"),TEXT("valley_crystal")};
    const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("WorldPreview"));
    IFileManager::Get().MakeDirectory(*Root,true);
    for (int32 Index=0; Index<3; ++Index)
    {
        FTimerHandle View, Shot;
        World->GetTimerManager().SetTimer(View,[Camera,Position=Positions[Index],Target=Targets[Index]]()
        { Camera->SetActorLocation(Position); Camera->SetActorRotation((Target-Position).Rotation()); },1.0f+Index*4,false);
        World->GetTimerManager().SetTimer(Shot,[Root,Name=Names[Index]]()
        { FScreenshotRequest::RequestScreenshot(Root/(Name+TEXT(".png")),false,false); },3.0f+Index*4,false);
    }
    FTimerHandle Finished;
    World->GetTimerManager().SetTimer(Finished,[Root,Names]()
    {
        bool Pass = true;
        for (const auto& Name : Names) Pass &= IFileManager::Get().FileSize(*(Root/(Name+TEXT(".png")))) > 1000;
        UE_LOG(LogTemp,Display,TEXT("CF_WORLD_PREVIEW %s %s"),Pass ? TEXT("PASS") : TEXT("FAIL"),*Root);
        FPlatformMisc::RequestExitWithStatus(false,Pass ? 0 : 1);
    },13.0f,false);
}
