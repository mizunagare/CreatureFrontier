#include "RespawnScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "WorkStation.h"
#include "WorldLoot.h"
#include "InventoryComponent.h"
#include "BaseBuildingComponent.h"
#include "FrontierSaveGame.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "TimerManager.h"

void RunRespawnScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    UCreatureDefinition* Definition=nullptr;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It) { if(It->Definition) Definition=It->Definition; It->Destroy(); }
    if(!Definition) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Hero->Inventory->Restore({{EFrontierItem::Material,100},{EFrontierItem::CaptureDevice,7},{EFrontierItem::Medicine,3},{EFrontierItem::Food,9},{EFrontierItem::Crystal,2},{EFrontierItem::ResonanceDevice,1}});
    Hero->SetActorLocation(FVector(-350,-1050,40)); Hero->GetCharacterMovement()->DisableMovement();
    Hero->Building->Selected=EWorkSpecialty::Feeding;
    auto* Trough=Hero->Building->BuildAt(FTransform(FRotator::ZeroRotator,FVector(0,-700,-15),FVector(2,1,.7)));
    AWorkStation* Gather=nullptr; AWorkStation* Mine=nullptr;
    for(TActorIterator<AWorkStation> It(World);It;++It) { if(It->RequiredSpecialty==EWorkSpecialty::Gathering) Gather=*It; if(It->RequiredSpecialty==EWorkSpecialty::Mining) Mine=*It; }
    if(!Trough || !Gather || !Mine) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Trough->StoredFood=7;
    auto Make=[&](FVector Position)
    {
        const FTransform Transform(Position);
        auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Pet->Definition=Definition; Pet->FinishSpawning(Transform); Pet->Tame(Hero->GetController()); Pet->SetActorTickEnabled(false); Pet->GetCharacterMovement()->DisableMovement(); return Pet;
    };
    auto* Worker=Make(Gather->GetActorLocation()+FVector(0,180,55)); Worker->Health=60; Worker->Stamina=40; Worker->Nutrition=60; Worker->WorkSite=Gather; Worker->SetActivity(ECreatureActivity::Work);
    auto* Resting=Make(Mine->GetActorLocation()+FVector(0,180,55)); Resting->Stamina=20; Resting->WorkSite=Mine; Resting->SetActivity(ECreatureActivity::Rest); Resting->bResumeWorkAfterRest=true;
    auto* Fighter=Make(FVector(-1000,1000,40)); Fighter->Health=50; Fighter->Stamina=80; Fighter->SpecialCooldownRemaining=7;
    auto* Downed=Make(FVector(-1000,700,40)); Downed->Health=0; Downed->SetActivity(ECreatureActivity::Rest);
    auto* Enemy=World->SpawnActor<ACreatureCharacter>(FVector(1400,1000,40),FRotator::ZeroRotator); Enemy->SetActorTickEnabled(false); Enemy->GetCharacterMovement()->DisableMovement();
    Fighter->SetActivity(ECreatureActivity::Combat); Fighter->FocusTarget=Enemy; Fighter->SpecialWindupRemaining=.5f;
    Hero->SetActorLocation(FVector(-350,-400,40)); Hero->ObserveCreature(Resting); Hero->ClaimObjectiveReward(); Hero->ClaimObjectiveReward();
    const int32 Claimed=Hero->GetObjectives().Claimed, JournalCount=Hero->GetJournal().Num();
    const auto Bag=Hero->Inventory->GetStacks();
    auto SameBag=[Hero,Bag]() { for(int32 I=0;I<static_cast<int32>(EFrontierItem::Count);++I) if(Hero->Inventory->Count(static_cast<EFrontierItem>(I))!=Bag.FindRef(static_cast<EFrontierItem>(I))) return false; return true; };
    auto* Loot=World->SpawnActor<AWorldLoot>(FVector(1500,1400,0),FRotator::ZeroRotator); Loot->Materials=7;
    Hero->Health=25; const FVector AlivePosition=Hero->GetActorLocation();
    const bool Living=!Hero->RespawnAtBase() && Hero->Health==25 && Hero->GetActorLocation().Equals(AlivePosition) && SameBag();
    Hero->Health=100; Hero->SetActorLocation(FVector(1200,1000,40)); UGameplayStatics::ApplyDamage(Enemy,10,Hero->GetController(),Hero,nullptr);
    Hero->Building->Toggle(); Hero->ToggleJournal();
    UGameplayStatics::ApplyDamage(Hero,500,Enemy->GetController(),Enemy,nullptr);
    bool Death=Hero->Health==0 && Hero->GetCharacterMovement()->MovementMode==MOVE_None && !Hero->Building->IsActive() && !Hero->IsJournalOpen() && !Fighter->FocusTarget && Fighter->SpecialWindupRemaining==0 && Fighter->SpecialCooldownRemaining==7 && Fighter->Activity==ECreatureActivity::Follow;
    const FVector DeathPosition=Hero->GetActorLocation(); const FString DeadSlot=TEXT("FrontierRespawnDeadAutomation");
    const bool Saved=Hero->SaveProgress(DeadSlot);
    auto Block=[&](FVector Position,FVector Scale)
    {
        auto* Actor=World->SpawnActor<AActor>(); auto* Mesh=NewObject<UStaticMeshComponent>(Actor); Actor->SetRootComponent(Mesh); Mesh->RegisterComponent();
        Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"))); Mesh->SetCollisionProfileName(TEXT("BlockAll")); Actor->SetActorLocation(Position); Actor->SetActorScale3D(Scale); return Actor;
    };
    auto* FullBlock=Block(FVector(-450,0,80),FVector(30,30,6));
    bool Blocked=!Hero->RespawnAtBase() && Hero->Health==0 && Hero->GetActorLocation().Equals(DeathPosition) && SameBag(); FullBlock->Destroy();
    auto* CenterBlock=Block(FVector(0,0,30),FVector(2,2,1.6)); Enemy->SetActorLocation(FVector(0,0,40));
    bool Respawn=Hero->RespawnAtBase() && Hero->Health==100 && Hero->GetCharacterMovement()->MovementMode==MOVE_Walking && FVector::Dist2D(Hero->GetActorLocation(),Enemy->GetActorLocation())>=800 && FVector::Dist2D(Hero->GetActorLocation(),UBaseBuildingComponent::BaseOrigin())<=1201;
    bool Preserved=SameBag() && Hero->GetObjectives().Claimed==Claimed && Hero->GetJournal().Num()==JournalCount && Worker->Health==60 && Worker->Stamina==40 && Worker->Nutrition==60 && Worker->Activity==ECreatureActivity::Work && Worker->WorkSite==Gather && Resting->bResumeWorkAfterRest && Resting->WorkSite==Mine && Resting->Activity==ECreatureActivity::Rest && Fighter->Health==50 && Fighter->Stamina==80 && Downed->Health==0 && Trough->StoredFood==7 && Loot->Materials==7 && Enemy->Health==90;
    CenterBlock->Destroy(); Enemy->SetActorLocation(FVector(1100,0,40)); Enemy->Tick(0);
    Preserved &= Enemy->GetDistanceTo(Hero)>650 && Enemy->GetDistanceTo(Hero)<2000 && Enemy->Activity==ECreatureActivity::Idle; // Retaliation must not pull a far enemy back to the base.
    bool Shield=Hero->GetRespawnProtectionRemaining()>0 && UGameplayStatics::ApplyDamage(Hero,10,nullptr,Enemy,nullptr)==0 && Hero->Health==100;
    const FVector SafePosition=Hero->GetActorLocation(); Shield &= !Hero->RespawnAtBase() && Hero->GetActorLocation().Equals(SafePosition);
    if(FParse::Param(FCommandLine::Get(),TEXT("CFRespawnPreview")))
    {
        const FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("RespawnPreview")); IFileManager::Get().MakeDirectory(*Root,true);
        Hero->CaptureTarget(); UGameplayStatics::ApplyDamage(Hero,500,nullptr,nullptr,nullptr);
        FTimerHandle Shot,Return,Shot2,Done;
        World->GetTimerManager().SetTimer(Shot,[Root](){FScreenshotRequest::RequestScreenshot(Root/TEXT("downed.png"),false,false);},2.f,false);
        World->GetTimerManager().SetTimer(Return,[Hero](){Hero->RespawnAtBase();},3.f,false);
        World->GetTimerManager().SetTimer(Shot2,[Root](){FScreenshotRequest::RequestScreenshot(Root/TEXT("respawn.png"),false,false);},4.f,false);
        World->GetTimerManager().SetTimer(Done,[](){FPlatformMisc::RequestExitWithStatus(false,0);},6.f,false); return;
    }
    FTimerHandle Expired;
    World->GetTimerManager().SetTimer(Expired,[World,Hero,DeadSlot,Living,Death,Blocked,Respawn,Preserved,Shield,Saved]()
    {
        bool Expiry=Hero->GetRespawnProtectionRemaining()==0 && UGameplayStatics::ApplyDamage(Hero,10,nullptr,nullptr,nullptr)==10 && Hero->Health==90;
        bool Restore=Saved && Hero->LoadProgress(DeadSlot) && Hero->Health==0 && Hero->GetRespawnProtectionRemaining()==0 && Hero->RespawnAtBase();
        Hero->CommandFocus(); Restore &= Hero->GetRespawnProtectionRemaining()==0 && UGameplayStatics::ApplyDamage(Hero,10,nullptr,nullptr,nullptr)==10;
        Restore &= Hero->LoadProgress(DeadSlot) && Hero->RespawnAtBase(); Hero->CommandSpecial(); Restore &= Hero->GetRespawnProtectionRemaining()==0;
        Restore &= Hero->LoadProgress(DeadSlot) && Hero->RespawnAtBase(); Hero->Attack(); Restore &= Hero->GetRespawnProtectionRemaining()==0;
        Restore &= Hero->LoadProgress(DeadSlot) && Hero->RespawnAtBase(); Hero->CaptureTarget(); Restore &= Hero->GetRespawnProtectionRemaining()==0;
        Restore &= Hero->LoadProgress(DeadSlot) && Hero->RespawnAtBase();
        const FString LivingSlot=TEXT("FrontierRespawnLivingAutomation"); Restore &= Hero->SaveProgress(LivingSlot) && Hero->LoadProgress(LivingSlot) && Hero->Health==100 && Hero->GetRespawnProtectionRemaining()==0;
        int32 Pets=0,Facilities=0,Drops=0;
        for(TActorIterator<ACreatureCharacter> It(World);It;++It) ++Pets;
        for(TActorIterator<AWorkStation> It(World);It;++It) if(It->bPlayerBuilt) ++Facilities;
        for(TActorIterator<AWorldLoot> It(World);It;++It) ++Drops;
        Restore &= Pets==5 && Facilities==1 && Drops==1;
        const bool Pass=Living && Death && Blocked && Respawn && Preserved && Shield && Expiry && Restore;
        UE_LOG(LogTemp,Display,TEXT("CF_RESPAWN %s living=%d death=%d blocked=%d respawn=%d preserved=%d shield=%d expiry=%d restore=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Living,Death,Blocked,Respawn,Preserved,Shield,Expiry,Restore);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    },3.5f,false);
}
