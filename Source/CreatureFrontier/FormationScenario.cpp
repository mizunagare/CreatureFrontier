#include "FormationScenario.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "CFPlayerCharacter.h"
#include "InventoryComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameViewportClient.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "TimerManager.h"
#include "AIController.h"

void RunFormationScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    TMap<ECreatureForm,UCreatureDefinition*> Definitions;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It) { if(It->Definition) Definitions.Add(It->Definition->Form,It->Definition); It->Destroy(); }
    if(Definitions.Num()!=3) { UE_LOG(LogTemp,Error,TEXT("CF_FORMATION FAIL fixtures")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    int32 Lead=0; const bool ExplicitLead=FParse::Value(FCommandLine::Get(),TEXT("CFFormationLead="),Lead);
    const bool Reverse=FParse::Param(FCommandLine::Get(),TEXT("CFFormationReverse"));
    if(Lead<0 || Lead>2) { UE_LOG(LogTemp,Error,TEXT("CF_FORMATION FAIL invalid lead form")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Hero->GetCharacterMovement()->DisableMovement(); Hero->SetActorLocation(FVector(1100,500,40)); Hero->GetController()->SetControlRotation(FRotator::ZeroRotator);
    TArray<ACreatureCharacter*> Pets,Excluded;
    for(int32 Index=0;Index<9;++Index)
    {
        const FTransform Transform(FVector(300,100+Index*120,40));
        FActorSpawnParameters Spawn; Spawn.bDeferConstruction=true; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        // Keep construction/tick order Leaf, Ember, Stone. Permute roster roles through names,
        // matching the cooked build's reverse-generated names rather than swapping definitions.
        const int32 Rank=(Reverse?(Lead-Index%3+3):(Index%3-Lead+3))%3;
        if(ExplicitLead) Spawn.Name=FName(*FString::Printf(TEXT("FormationFixture_%02d"),(Index/3)*3+Rank));
        auto* Pet=World->SpawnActor<ACreatureCharacter>(ACreatureCharacter::StaticClass(),Transform,Spawn);
        Pet->Definition=Definitions.FindRef(static_cast<ECreatureForm>(Index%3)); Pet->FinishSpawning(Transform); Pet->Tame(Hero->GetController()); Pet->SetActorTickEnabled(false);
        if(Index<6) Pets.Add(Pet);
        else { Pet->SetActivity(Index==6?ECreatureActivity::Work:ECreatureActivity::Rest); if(Index==8) Pet->Health=0; Excluded.Add(Pet); }
    }
    bool Layout=true; TArray<FVector> Goals;
    for(auto* Pet:Pets)
    {
        const FVector Goal=Pet->GetFollowDestination(); const FVector Delta=Goal-Hero->GetActorLocation();
        Layout &= Delta.X<0 && (FMath::IsNearlyEqual(Delta.X,-170.f,.01f) || FMath::IsNearlyEqual(Delta.X,-320.f,.01f)); Goals.Add(Goal);
    }
    for(int32 A=0;A<Goals.Num();++A) for(int32 B=A+1;B<Goals.Num();++B) Layout &= FVector::Dist2D(Goals[A],Goals[B])>=129;
    for(int32 Index=0;Index<Pets.Num();++Index) if(FMath::Abs(Goals[Index].Y-500)<1 && FMath::Abs(Goals[Index].X-930)<1)
    {
        auto* Probe=Pets[Index]; const FVector Before=Probe->GetActorLocation();
        Probe->SetActorLocation(Hero->GetActorLocation()+FVector(500,0,0));
        const FVector Waypoint=Probe->GetFollowDestination();
        const FVector Closest=FMath::ClosestPointOnSegment(Hero->GetActorLocation(),Probe->GetActorLocation(),Waypoint);
        Layout &= FVector::Dist2D(Closest,Hero->GetActorLocation())>90 && FMath::Abs(Waypoint.X-Hero->GetActorLocation().X)<1;
        Probe->SetActorLocation(Before); break;
    }
    Layout &= Excluded[0]->Activity==ECreatureActivity::Work && Excluded[1]->Activity==ECreatureActivity::Rest && Excluded[2]->Health==0;
    for(auto* Pet:Excluded) Pet->Destroy();
    while(Pets.Num()>3) { Pets.Last()->Destroy(); Pets.Pop(); }
    ACreatureCharacter* Center=nullptr;
    int32 Side=0;
    for(auto* Pet:Pets)
    {
        const FVector Goal=Pet->GetFollowDestination();
        if(FMath::Abs(Goal.Y-500)<1) { Center=Pet; Pet->SetActorLocation(FVector(500,500,40)); }
        else { Pet->SetActorLocation(FVector(500,Side++==0?300:700,40)); }
        Pet->BondPoints=12; Pet->CarriedFood=2; Pet->SetActorTickEnabled(true);
    }
    auto* Path=Center?UNavigationSystemV1::FindPathToLocationSynchronously(World,Center->GetActorLocation(),Center->GetFollowDestination(),Center):nullptr;
    UE_LOG(LogTemp,Display,TEXT("CF_FORMATION_FIXTURE lead=%d center=%d named=%d reverse=%d"),Lead,Center && Center->Definition?static_cast<int32>(Center->Definition->Form):-1,ExplicitLead,Reverse);
    const bool PathValid=Path && Path->IsValid() && !Path->IsPartial() && Path->PathPoints.Num()>=3;
    const int32 Materials=Hero->Inventory->Count(EFrontierItem::Material);
    struct FTrial { int32 Phase=0; double Started=0,NextTrace=0; float Detour=0; TArray<FVector> Previous; };
    auto Trial=MakeShared<FTrial>(); Trial->Started=World->GetTimeSeconds(); auto Poll=MakeShared<FTimerHandle>();
    World->GetTimerManager().SetTimer(*Poll,[World,Hero,Pets,Center,Layout,PathValid,Materials,Trial,Poll]()
    {
        // Clearing this delegate destroys its captures. Keep the handle on the stack,
        // and never access captures after the final ClearTimer call.
        FTimerHandle ActivePoll=*Poll;
        if(Center && Trial->Phase==0) Trial->Detour=FMath::Max(Trial->Detour,FMath::Abs(static_cast<float>(Center->GetActorLocation().Y)-500));
        bool Arrived=true;
        const auto Ordered=Hero->GetCompanionRoster();
        const FRotator Facing(0,Hero->GetController()->GetControlRotation().Yaw,0);
        for(auto* Pet:Pets)
        {
            const int32 Slot=Ordered.IndexOfByKey(Pet);
            const float Side=Slot%3==0?0.f:Slot%3==1?-130.f:130.f;
            const FVector Nominal=Hero->GetActorLocation()+Facing.RotateVector(FVector(-170.f-(Slot/3)*150.f,Side,0));
            // A temporary avoidance/occupied-slot waypoint is not evidence of docking in formation.
            Arrived &= Slot!=INDEX_NONE && FVector::Dist2D(Pet->GetActorLocation(),Nominal)<=30 && Pet->GetVelocity().Size2D()<10;
        }
        const double Elapsed=World->GetTimeSeconds()-Trial->Started;
        if(FParse::Param(FCommandLine::Get(),TEXT("CFFormationTrace")) && Elapsed>=Trial->NextTrace)
        {
            Trial->NextTrace=Elapsed+.5;
            for(int32 Index=0;Index<Pets.Num();++Index)
            {
                auto* Pet=Pets[Index]; auto* AI=Cast<AAIController>(Pet->GetController());
                UE_LOG(LogTemp,Display,TEXT("CF_FORMATION_TRACE phase=%d t=%.2f pet=%d name=%s pos=%s goal=%s velocity=%s segment=%s rvo=%.1f dt=%.4f"),
                    Trial->Phase,Elapsed,Index,*Pet->GetName(),*Pet->GetActorLocation().ToString(),*Pet->GetFollowDestination().ToString(),
                    *Pet->GetVelocity().ToString(),*(AI?AI->GetImmediateMoveDestination():FVector::ZeroVector).ToString(),
                    Pet->GetCharacterMovement()->GetRVOAvoidanceWeight(),World->GetDeltaSeconds());
            }
        }
        if(Elapsed<2 || (!Arrived && Elapsed<12)) return;
        bool Spaced=true,Stats=Hero->Inventory->Count(EFrontierItem::Material)==Materials;
        for(int32 A=0;A<Pets.Num();++A)
        {
            Stats &= Pets[A]->Health==Pets[A]->GetMaxHealth() && Pets[A]->Experience==0 && Pets[A]->Stamina==100 && Pets[A]->BondPoints==12 && Pets[A]->CarriedFood==2;
            for(int32 B=A+1;B<Pets.Num();++B) Spaced &= FVector::Dist2D(Pets[A]->GetActorLocation(),Pets[B]->GetActorLocation())>80;
        }
        if(!Layout || !PathValid || !Arrived || !Spaced || !Stats || Trial->Detour<=100)
        {
            UE_LOG(LogTemp,Error,TEXT("CF_FORMATION FAIL phase=%d layout=%d path=%d arrived=%d spaced=%d stats=%d detour=%.1f"),Trial->Phase,Layout,PathValid,Arrived,Spaced,Stats,Trial->Detour);
            for(auto* Pet:Pets) UE_LOG(LogTemp,Display,TEXT("CF_FORMATION position=%s goal=%s"),*Pet->GetActorLocation().ToString(),*Pet->GetFollowDestination().ToString());
            if(auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)) for(auto* Pet:Pets)
            {
                FNavLocation Projected; const bool Valid=Nav->ProjectPointToNavigation(Pet->GetFollowDestination(),Projected,FVector(250,250,300));
                UE_LOG(LogTemp,Display,TEXT("CF_FORMATION projected=%d %s velocity=%s"),Valid,*Projected.Location.ToString(),*Pet->GetVelocity().ToString());
            }
            World->GetTimerManager().ClearTimer(ActivePoll); FPlatformMisc::RequestExitWithStatus(false,1); return;
        }
        if(Trial->Phase==0)
        {
            for(auto* Pet:Pets) Trial->Previous.Add(Pet->GetActorLocation());
            Hero->SetActorLocation(FVector(1100,-50,40)); Hero->GetController()->SetControlRotation(FRotator(0,90,0));
            Trial->Phase=1; Trial->Started=World->GetTimeSeconds(); Trial->NextTrace=0; return;
        }
        bool Moved=true;
        for(int32 Index=0;Index<Pets.Num();++Index) Moved &= FVector::Dist2D(Pets[Index]->GetActorLocation(),Trial->Previous[Index])>200 && Pets[Index]->GetFollowDestination().Y<Hero->GetActorLocation().Y;
        bool Saved=Hero->SaveProgress(TEXT("FrontierFormationAutomation")) && Hero->LoadProgress(TEXT("FrontierFormationAutomation"));
        TArray<ACreatureCharacter*> Restored;
        for(TActorIterator<ACreatureCharacter> It(World);It;++It) if(It->Tamer==Hero->GetController())
        {
            Restored.Add(*It); Saved &= It->Activity==ECreatureActivity::Follow && It->CarriedFood==2 && It->BondPoints==12;
        }
        Saved &= Restored.Num()==3;
        const bool Passed=Moved && Saved;
        UE_LOG(LogTemp,Display,TEXT("CF_FORMATION %s layout=%d real_nav=%d spacing=%d rotation=%d stats=%d save=%d detour=%.1f"),Passed?TEXT("PASS"):TEXT("FAIL"),Layout,Arrived,Spaced,Moved,Stats,Saved,Trial->Detour);
        if(Passed && FParse::Param(FCommandLine::Get(),TEXT("CFFormationPreview")))
        {
            for(auto* Pet:Restored) { Pet->SetActorTickEnabled(false); Pet->GetCharacterMovement()->StopMovementImmediately(); }
            Hero->GetCharacterMovement()->DisableMovement();
            const FVector Camera(1100,300,430); const FRotator View=(FVector(1100,-220,0)-Camera).Rotation();
            Hero->FirstPersonCamera->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform); Hero->FirstPersonCamera->bUsePawnControlRotation=false;
            Hero->FirstPersonCamera->SetWorldLocationAndRotation(Camera,View); Hero->GetController()->SetControlRotation(View);
            Hero->Feedback=TEXT("Formation navigation verified - three followers, no teleport. Preview camera only");
            const FString Root=FPaths::ProjectSavedDir()/TEXT("FormationPreview"); IFileManager::Get().MakeDirectory(*Root,true);
            FTimerHandle Shot,Done; World->GetTimerManager().SetTimer(Shot,[Root](){FScreenshotRequest::RequestScreenshot(Root/TEXT("followers.png"),false,false);},3.f,false);
            World->GetTimerManager().SetTimer(Done,[](){FPlatformMisc::RequestExitWithStatus(false,0);},5.f,false);
            World->GetTimerManager().ClearTimer(ActivePoll); return;
        }
        World->GetTimerManager().ClearTimer(ActivePoll);
        FPlatformMisc::RequestExitWithStatus(false,Passed?0:1);
    },.2f,true);
}
