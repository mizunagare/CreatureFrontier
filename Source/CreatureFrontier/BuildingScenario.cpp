#include "BuildingScenario.h"
#include "BaseBuildingComponent.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "WorkStation.h"
#include "FrontierSaveGame.h"
#include "InventoryComponent.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavigationPath.h"
#include "TimerManager.h"
#include "HAL/PlatformMisc.h"

void RunBuildingScenario(UWorld* World, ACFPlayerCharacter* Hero)
{
    UCreatureDefinition* Stone = nullptr;
    for (TActorIterator<ACreatureCharacter> It(World); It; ++It)
    { if (It->Definition && It->Definition->Form == ECreatureForm::Stone) Stone = It->Definition; It->Destroy(); }
    if (!Stone) { UE_LOG(LogTemp,Error,TEXT("CF_BUILD FAIL definition")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Hero->GetCharacterMovement()->DisableMovement();
    Hero->SetActorLocation(FVector(-350,-1050,40));
    Hero->Inventory->Restore({{EFrontierItem::Material,100},{EFrontierItem::CaptureDevice,5}});
    auto* Builder = Hero->Building.Get();
    Hero->FirstPersonCamera->SetWorldRotation((FVector(0,-700,-50)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());
    Builder->Toggle();
    const bool Preview = Builder->IsActive();
    Builder->Cycle(); Builder->Cycle(); Builder->Cycle();
    Hero->Attack(); // Real LMB route must place rather than fire.
    Hero->CaptureTarget(); // Real RMB route cancels without consuming a capture device.
    AWorkStation* Gather = nullptr;
    for (TActorIterator<AWorkStation> It(World); It; ++It) if (It->bPlayerBuilt) Gather = *It;
    const bool Input = Preview && !Builder->IsActive() && Builder->Selected == EWorkSpecialty::Gathering &&
        Gather && Hero->Inventory->Count(EFrontierItem::Material) == 90 && Hero->Inventory->Count(EFrontierItem::CaptureDevice) == 5;
    if (!Gather) { UE_LOG(LogTemp,Error,TEXT("CF_BUILD FAIL preview placement: %s"),*Hero->Feedback); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    auto At = [](double X,double Y) { return FTransform(FRotator::ZeroRotator,FVector(X,Y,-15),FVector(2,1,.7)); };
    bool Reject = !Builder->BuildAt(Gather->GetActorTransform()) && !Builder->BuildAt(At(-1900,-1000)) &&
        !Builder->BuildAt(FTransform(FRotator::ZeroRotator,FVector(-300,-700,200),FVector(2,1,.7))) && Hero->Inventory->Count(EFrontierItem::Material) == 90;
    Hero->Inventory->Restore({{EFrontierItem::Material,2}});
    Reject &= !Builder->BuildAt(At(-300,-700)) && Hero->Inventory->Count(EFrontierItem::Material) == 2;
    Hero->Inventory->Restore({{EFrontierItem::Material,90}});
    // An actual pawn occupying the footprint prevents placement.
    auto* Blocker = World->SpawnActor<ACreatureCharacter>(FVector(-300,-700,40),FRotator::ZeroRotator);
    Reject &= !Builder->BuildAt(At(-300,-700));
    Blocker->Destroy();
    Builder->Selected = EWorkSpecialty::Mining;
    auto* Mine = Builder->BuildAt(At(-300,-700));
    Builder->Selected = EWorkSpecialty::Crafting;
    auto* Craft = Builder->BuildAt(At(-700,-700));
    const bool Costs = Mine && Craft && Hero->Inventory->Count(EFrontierItem::Material) == 63;
    if (!Mine || !Craft) { UE_LOG(LogTemp,Error,TEXT("CF_BUILD FAIL secondary facilities: %s"),*Hero->Feedback); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    const FGuid MineId = Mine->PersistentId;
    const FGuid GatherId = Gather->PersistentId;
    const FTransform PetTransform(FRotator::ZeroRotator,FVector(-300,-900,40));
    auto* Pet = World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),PetTransform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    Pet->Definition = Stone; Pet->FinishSpawning(PetTransform); Pet->Tame(Hero->GetController());
    Pet->WorkSite = Mine; Pet->SetActivity(ECreatureActivity::Work); Pet->Tick(1.6f);
    const bool Work = Hero->Inventory->Count(EFrontierItem::Material) == 64;
    const FString Slot = TEXT("FrontierBuildingAutomation");
    bool Saved = Hero->SaveProgress(Slot) && Hero->LoadProgress(Slot) && Hero->LoadProgress(Slot);
    int32 Count = 0; bool Scale = true, Link = false;
    for (TActorIterator<AWorkStation> It(World); It; ++It) if (It->bPlayerBuilt)
    { ++Count; Scale &= It->GetActorScale3D().Equals(FVector(2,1,.7),.001); }
    for (TActorIterator<ACreatureCharacter> It(World); It; ++It)
        if (It->Tamer == Hero->GetController()) Link = It->WorkSite && It->WorkSite->PersistentId == MineId && It->Activity == ECreatureActivity::Work;
    Saved &= Count == 3 && Scale && Link;
    auto* Bad = Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0));
    if (Bad && Bad->Facilities.Num() > 1)
    {
        Bad->Facilities[1].Id = Bad->Facilities[0].Id;
        const FString BadSlot = TEXT("FrontierInvalidBuildingAutomation");
        UGameplayStatics::SaveGameToSlot(Bad,BadSlot,0);
        Reject &= !Hero->LoadProgress(BadSlot);
    }
    else Reject = false;
    // Let real runtime nav tiles rebuild after placement and two snapshot replacements.
    FTimerHandle CheckNav;
    World->GetTimerManager().SetTimer(CheckNav,[World,Hero,MineId,GatherId,Slot,Input,Reject,Costs,Work,Saved]()
    {
        auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
        auto* Data = Nav ? Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
        auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World,FVector(0,-1000,40),FVector(0,-400,40),Hero);
        float Detour = 0;
        if (Path) for (const FVector& Point : Path->PathPoints) Detour = FMath::Max(Detour,static_cast<float>(FMath::Abs(Point.X)));
        const bool NavAdd = Data && Data->GetRuntimeGenerationMode() == ERuntimeGenerationType::Dynamic && Path && Path->IsValid() && !Path->IsPartial() && Detour > 110;
        AWorkStation* Mine = nullptr; AWorkStation* Original = nullptr;
        for (TActorIterator<AWorkStation> It(World); It; ++It)
        { if (It->PersistentId == MineId) Mine = *It; if (!It->bPlayerBuilt) Original = *It; }
        auto* Builder = Hero->Building.Get();
        const int32 Before = Hero->Inventory->Count(EFrontierItem::Material);
        bool Remove = Mine && Original && !Builder->DismantleStation(Original);
        Hero->Inventory->Restore({{EFrontierItem::Material,999}});
        Remove &= !Builder->DismantleStation(Mine) && IsValid(Mine);
        Hero->Inventory->Restore({{EFrontierItem::Material,Before}});
        Remove &= Builder->DismantleStation(Mine) && Hero->Inventory->Count(EFrontierItem::Material) == Before+6;
        for (TActorIterator<ACreatureCharacter> It(World); It; ++It)
            if (It->Tamer == Hero->GetController()) Remove &= !It->WorkSite && It->Activity == ECreatureActivity::Follow;
        Remove &= Hero->SaveProgress(Slot) && Hero->LoadProgress(Slot);
        int32 Left = 0;
        for (TActorIterator<AWorkStation> It(World); It; ++It) if (It->bPlayerBuilt)
        { ++Left; if (It->PersistentId == GatherId) Remove &= Builder->DismantleStation(*It); }
        Remove &= Left == 2;
        FTimerHandle CheckRemoval;
        World->GetTimerManager().SetTimer(CheckRemoval,[World,Hero,Input,Reject,Costs,Work,Saved,NavAdd,Remove]()
        {
            auto* OpenPath = UNavigationSystemV1::FindPathToLocationSynchronously(World,FVector(0,-1000,40),FVector(0,-400,40),Hero);
            bool NavRemove = OpenPath && OpenPath->IsValid() && !OpenPath->IsPartial() && OpenPath->PathPoints.Num() >= 2;
            if (NavRemove)
            {
                FVector Hit;
                NavRemove = !UNavigationSystemV1::NavigationRaycast(World,OpenPath->PathPoints[0],OpenPath->PathPoints.Last(),Hit);
                double Length = 0;
                for (int32 Index=0; Index<OpenPath->PathPoints.Num(); ++Index)
                {
                    NavRemove &= FMath::Abs(OpenPath->PathPoints[Index].X) < 1;
                    if (Index > 0) Length += FVector::Dist(OpenPath->PathPoints[Index-1],OpenPath->PathPoints[Index]);
                }
                NavRemove &= Length <= 601;
            }
            if (!NavRemove && OpenPath)
                for (const FVector& Point : OpenPath->PathPoints) UE_LOG(LogTemp,Display,TEXT("CF_BUILD_REMOVAL_POINT %s"),*Point.ToString());
            const bool Pass = Input && Reject && Costs && Work && Saved && NavAdd && Remove && NavRemove;
            UE_LOG(LogTemp,Display,TEXT("CF_BUILD %s input=%d reject=%d costs=%d work=%d save=%d navAdd=%d dismantle=%d navRemove=%d"),
                Pass ? TEXT("PASS") : TEXT("FAIL"),Input,Reject,Costs,Work,Saved,NavAdd,Remove,NavRemove);
            FPlatformMisc::RequestExitWithStatus(false,Pass ? 0 : 1);
        },3.0f,false);
    },3.0f,false);
}
