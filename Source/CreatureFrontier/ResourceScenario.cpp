#include "ResourceScenario.h"
#include "ResourceNode.h"
#include "ResourceDefinition.h"
#include "CreatureCharacter.h"
#include "CFPlayerCharacter.h"
#include "CreatureDefinition.h"
#include "WorkStation.h"
#include "InventoryComponent.h"
#include "FrontierSaveGame.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Components/BoxComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

void RunResourceScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    UCreatureDefinition* Leaf=nullptr;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)
    { if(It->Definition && It->Definition->Form==ECreatureForm::Leaf) Leaf=It->Definition; It->Destroy(); }
    AResourceNode *Brush=nullptr,*Stone=nullptr; AWorkStation* Post=nullptr;
    auto CountNodes=[World]() { int32 Count=0; for(TActorIterator<AResourceNode> It(World);It;++It) ++Count; return Count; };
    for(TActorIterator<AResourceNode> It(World);It;++It)
    {
        It->SetActorTickEnabled(false);
        if(It->PersistentId==FGuid(0xCF170000,0,0,1)) Brush=*It;
        if(It->PersistentId==FGuid(0xCF170000,0,0,2)) Stone=*It;
    }
    for(TActorIterator<AWorkStation> It(World);It;++It) if(!It->bPlayerBuilt && It->RequiredSpecialty==EWorkSpecialty::Gathering) Post=*It;
    if(!Brush || !Stone || !Post || !Leaf || CountNodes()!=7) { UE_LOG(LogTemp,Error,TEXT("CF_RESOURCE FAIL fixture")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Hero->GetCharacterMovement()->DisableMovement(); Hero->Inventory->Restore({{EFrontierItem::Material,0}});
    const FVector Home=Brush->GetActorLocation(); const int32 Capacity=Brush->Definition->Capacity;
    Hero->SetActorLocation(Home+FVector(0,-180,90));
    Hero->FirstPersonCamera->SetWorldRotation((Home-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());
    Hero->GatherAimedResource();
    bool Manual=Hero->Inventory->Count(EFrontierItem::Material)==1 && Brush->Remaining==Capacity-1;
    Hero->GatherAimedResource(); Manual &= Hero->Inventory->Count(EFrontierItem::Material)==1 && Brush->Remaining==Capacity-1;
    Brush->Tick(1); Hero->Health=0; Manual &= !Brush->TryHarvest(Hero,Hero); Hero->Health=100;
    Hero->SetActorLocation(Home+FVector(0,-500,90)); Manual &= !Brush->TryHarvest(Hero,Hero);
    Hero->SetActorLocation(Home+FVector(0,-180,90));
    auto* Blocker=World->SpawnActor<AActor>();
    auto* Box=NewObject<UBoxComponent>(Blocker); Blocker->SetRootComponent(Box); Box->SetBoxExtent(FVector(100,20,150));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Box->SetCollisionResponseToAllChannels(ECR_Ignore); Box->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    Box->SetCanEverAffectNavigation(false); Box->RegisterComponent(); Blocker->SetActorLocation(Home+FVector(0,-90,50));
    Manual &= !Brush->TryHarvest(Hero,Hero); Blocker->Destroy();
    Hero->Inventory->Restore({{EFrontierItem::Material,999}});
    const int32 Before=Brush->Remaining;
    bool Atomic=!Brush->TryHarvest(Hero,Hero) && Brush->Remaining==Before && Brush->HarvestCooldown==0;
    Hero->Inventory->Restore({{EFrontierItem::Material,1}}); Hero->SetActorLocation(Home+FVector(0,-400,90));
    auto MakePet=[World,Hero,Post,Leaf](FVector Position)
    {
        const FTransform Transform(Position);
        auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Pet->Definition=Leaf; Pet->FinishSpawning(Transform); Pet->Tame(Hero->GetController()); Pet->WorkSite=Post;
        Pet->SetActorTickEnabled(false); Pet->GetCharacterMovement()->DisableMovement(); Pet->SetActivity(ECreatureActivity::Work); return Pet;
    };
    auto* Pet=MakePet(Home+FVector(160,0,90));
    auto* Second=MakePet(Home+FVector(-160,0,90));
    Brush->Remaining=2; Brush->HarvestCooldown=0; Brush->RegrowRemaining=0;
    Second->Tamer=nullptr;
    bool Ownership=!Brush->TryHarvest(Hero,Second);
    Second->Tamer=Hero->GetController();
    Pet->Tick(2); Second->Tick(2);
    bool Shared=Ownership && Brush->Remaining==1 && Hero->Inventory->Count(EFrontierItem::Material)==2 && Pet->Experience+Second->Experience==1;
    Brush->Tick(1); Second->Tick(0);
    Shared &= Brush->Remaining==0 && Hero->Inventory->Count(EFrontierItem::Material)==3 && Pet->Experience+Second->Experience==2;
    Pet->Tick(5); Second->Tick(5);
    Shared &= Brush->Remaining==0 && Hero->Inventory->Count(EFrontierItem::Material)==3;
    for(TActorIterator<AResourceNode> It(World);It;++It) if(It->Definition->Specialty==EWorkSpecialty::Gathering)
    { It->Remaining=0; It->RegrowRemaining=It->Definition->RegrowSeconds; }
    Pet->Tick(3); Shared &= Hero->Inventory->Count(EFrontierItem::Material)==3;
    Pet->SetActivity(ECreatureActivity::Follow); Pet->SetActivity(ECreatureActivity::Work);
    Brush->Remaining=5; Brush->HarvestCooldown=0; Brush->RegrowRemaining=0;
    Pet->Tick(1); Pet->SetActivity(ECreatureActivity::Rest); Pet->SetActivity(ECreatureActivity::Work); Pet->Tick(1);
    bool Interrupt=Brush->Remaining==5;
    Pet->Tick(1); Interrupt &= Brush->Remaining==4;
    Brush->Tick(1); Hero->Inventory->Restore({{EFrontierItem::Material,999}}); Pet->Tick(2);
    Atomic &= Brush->Remaining==4 && Pet->Experience==2;
    Hero->Inventory->Consume(EFrontierItem::Material,1); Pet->Tick(0);
    Atomic &= Brush->Remaining==3 && Hero->Inventory->Count(EFrontierItem::Material)==999 && Pet->Experience==3;
    Second->Destroy(); Hero->Inventory->Restore({{EFrontierItem::Material,7}});
    Brush->Remaining=0; Brush->RegrowRemaining=180; Brush->HarvestCooldown=.8f; Brush->Refresh();
    Stone->Remaining=0; Stone->HarvestCooldown=0; Stone->RegrowRemaining=0; Stone->Refresh();
    const FString Slot=TEXT("FrontierResourceAutomation");
    bool Snapshot=Hero->SaveProgress(Slot) && Hero->LoadProgress(Slot) && Hero->LoadProgress(Slot);
    int32 RestoredWorkers=0;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It) if(It->Tamer==Hero->GetController())
    {
        It->SetActorTickEnabled(false); It->Tick(0);
        if(It->Activity==ECreatureActivity::Work && It->WorkSite==Post && It->Experience==3) ++RestoredWorkers;
    }
    Snapshot &= RestoredWorkers==1;
    Brush=nullptr; Stone=nullptr;
    for(TActorIterator<AResourceNode> It(World);It;++It)
    {
        It->SetActorTickEnabled(false);
        if(It->PersistentId==FGuid(0xCF170000,0,0,1)) Brush=*It;
        if(It->PersistentId==FGuid(0xCF170000,0,0,2)) Stone=*It;
    }
    Snapshot &= Brush && Stone && CountNodes()==7 && Brush->Remaining==0 && Brush->RegrowRemaining==180 && Brush->HarvestCooldown==.8f && Stone->Remaining==0 && Hero->Inventory->Count(EFrontierItem::Material)==7;
    if(!Brush || !Stone) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Brush->Tick(179); Stone->Tick(1000);
    bool Regrowth=Brush->Remaining==0 && Brush->RegrowRemaining==1 && Stone->Remaining==0;
    Brush->Tick(1); Regrowth &= Brush->Remaining==Capacity && Brush->RegrowRemaining==0 && Hero->Inventory->Count(EFrontierItem::Material)==7;
    bool Invalid=true;
    for(int32 Case=0;Case<6;++Case)
    {
        auto* Bad=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0));
        if(Case==0) { const FResourceSaveRecord Duplicate=Bad->ResourceNodes[0]; Bad->ResourceNodes.Add(Duplicate); }
        if(Case==1) Bad->ResourceNodes[0].Remaining=-1;
        if(Case==2) Bad->ResourceNodes[0].RegrowRemaining=5000;
        if(Case==3) Bad->ResourceNodes[0].Definition=TSoftObjectPtr<UResourceDefinition>(FSoftObjectPath(TEXT("/Game/Missing.Resource")));
        if(Case==4) Bad->ResourceNodes[0].HarvestCooldown=100;
        if(Case==5) { FCreatureSaveRecord Record; Record.CreatureClass=ACreatureCharacter::StaticClass(); Record.Health=-1; Bad->Creatures.Add(Record); }
        const FString BadSlot=TEXT("FrontierResourceInvalid");
        UGameplayStatics::SaveGameToSlot(Bad,BadSlot,0);
        Invalid &= !Hero->LoadProgress(BadSlot) && IsValid(Brush) && Brush->Remaining==Capacity && Brush->GetActorEnableCollision() && CountNodes()==7 && Hero->Inventory->Count(EFrontierItem::Material)==7;
    }
    auto* Legacy=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0)); Legacy->FormatVersion=16; Legacy->ResourceNodes.Reset();
    UGameplayStatics::SaveGameToSlot(Legacy,TEXT("FrontierResourceLegacy"),0);
    bool Migration=Hero->LoadProgress(TEXT("FrontierResourceLegacy")) && CountNodes()==7 && Hero->Inventory->Count(EFrontierItem::Material)==7;
    for(TActorIterator<AResourceNode> It(World);It;++It) Migration &= It->Remaining==It->Definition->Capacity;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It) It->Destroy();
    // Real navigation around the existing wall, then conserved harvest and depleted waiting.
    Brush=nullptr;
    for(TActorIterator<AResourceNode> It(World);It;++It)
    { if(It->PersistentId==FGuid(0xCF170000,0,0,1)) Brush=*It; else It->Destroy(); }
    Brush->SetActorLocation(FVector(1100,500,0)); Brush->Remaining=1; Brush->RegrowRemaining=0; Brush->HarvestCooldown=0; Brush->Refresh();
    Post->SetActorLocation(FVector(1100,850,-15));
    Hero->SetActorLocation(FVector(1300,750,40)); Hero->Inventory->Restore({{EFrontierItem::Material,0}});
    Pet=MakePet(FVector(500,500,40)); Pet->GetCharacterMovement()->SetMovementMode(MOVE_Walking); Pet->SetActorTickEnabled(true);
    auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Pet->GetActorLocation(),Brush->GetActorLocation(),Pet);
    const bool PathOK=Path && Path->IsValid() && !Path->IsPartial() && Path->PathPoints.Num()>=3;
    const bool Baseline=Manual && Atomic && Shared && Interrupt && Snapshot && Regrowth && Invalid && Migration && PathOK;
    UE_LOG(LogTemp,Display,TEXT("CF_RESOURCE checks manual=%d atomic=%d shared=%d interrupt=%d snapshot=%d regrowth=%d invalid=%d migration=%d path=%d"),Manual,Atomic,Shared,Interrupt,Snapshot,Regrowth,Invalid,Migration,PathOK);
    const FVector Start=Pet->GetActorLocation(); const double Started=World->GetTimeSeconds();
    const bool Preview=FParse::Param(FCommandLine::Get(),TEXT("CFResourcePreview"));
    TSharedRef<FTimerHandle> Poll=MakeShared<FTimerHandle>();
    World->GetTimerManager().SetTimer(*Poll,[World,Hero,Pet,Brush,Baseline,Start,Started,Poll,Preview,MaxDetour=0.f,HarvestTime=-1.0]() mutable
    {
        MaxDetour=FMath::Max(MaxDetour,FMath::Abs(static_cast<float>(Pet->GetActorLocation().Y-Start.Y)));
        if(Brush->Remaining==0 && HarvestTime<0) HarvestTime=World->GetTimeSeconds();
        if(HarvestTime<0 && World->GetTimeSeconds()-Started<14) return;
        if(HarvestTime>=0 && World->GetTimeSeconds()-HarvestTime<3) return;
        const bool Navigation=HarvestTime>=0 && MaxDetour>100 && Hero->Inventory->Count(EFrontierItem::Material)==1 && Pet->Experience==1;
        const bool Pass=Baseline && Navigation;
        UE_LOG(LogTemp,Display,TEXT("CF_RESOURCE %s navigation=%d detour=%.1f stock=%d materials=%d xp=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Navigation,MaxDetour,Brush->Remaining,Hero->Inventory->Count(EFrontierItem::Material),Pet->Experience);
        FTimerHandle ActivePoll=*Poll;
        if(Preview && Pass)
        {
            // Loading restores walking; freeze this camera fixture before aiming from its new height.
            Hero->GetCharacterMovement()->DisableMovement();
            Hero->SetActorLocation(FVector(1300,500,90));
            const FRotator View=(Brush->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
            Hero->GetController()->SetControlRotation(View); Hero->FirstPersonCamera->SetWorldRotation(View);
            FTimerHandle Shot,Done;
            World->GetTimerManager().SetTimer(Shot,[](){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("ResourcePreview.png"),false,false);},1.f,false);
            World->GetTimerManager().SetTimer(Done,[](){FPlatformMisc::RequestExitWithStatus(false,0);},2.f,false);
        }
        else FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
        World->GetTimerManager().ClearTimer(ActivePoll);
    },.1f,true);
}
