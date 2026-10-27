#include "ObjectiveScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "CaptureComponent.h"
#include "InventoryComponent.h"
#include "BaseBuildingComponent.h"
#include "WorkStation.h"
#include "FarmCropComponent.h"
#include "ResourceNode.h"
#include "FrontierSaveGame.h"
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

void RunObjectiveScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    UCreatureDefinition* LeafDefinition=nullptr;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It) { if(It->Definition && It->Definition->Form==ECreatureForm::Leaf) LeafDefinition=It->Definition; It->Destroy(); }
    if(!LeafDefinition) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Hero->SetActorLocation(FVector(-350,-1050,40));
    const FString CleanSlot=TEXT("FrontierObjectiveClean"); Hero->SaveProgress(CleanSlot);
    auto* Clean=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(CleanSlot,0));
    Clean->Creatures.Empty(); Clean->Journal.Empty(); Clean->Objectives=FFrontierObjectiveProgress(); Clean->Health=100;
    Clean->InventoryStacks={{EFrontierItem::Material,100},{EFrontierItem::CaptureDevice,5}};
    UGameplayStatics::SaveGameToSlot(Clean,CleanSlot,0); Hero->LoadProgress(CleanSlot); Hero->GetCharacterMovement()->DisableMovement();
    auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),FTransform(FVector(0,-900,40)),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    Pet->Definition=LeafDefinition; Pet->FinishSpawning(FTransform(FVector(0,-900,40))); Pet->SetActorTickEnabled(false); Pet->GetCharacterMovement()->DisableMovement();
    Hero->ClaimObjectiveReward(); bool Sequence=Hero->GetObjectives().Claimed==0 && Hero->Inventory->Count(EFrontierItem::CaptureDevice)==5;
    Hero->ObserveCreature(Pet); Hero->ClaimObjectiveReward();
    Sequence &= Hero->GetObjectives().Claimed==1 && Hero->Inventory->Count(EFrontierItem::CaptureDevice)==7;
    Hero->ClaimObjectiveReward(); Sequence &= Hero->GetObjectives().Claimed==1 && Hero->Inventory->Count(EFrontierItem::CaptureDevice)==7;
    Hero->CaptureComponent->CapturePower=100;
    int32 Seed=1; for(;Seed<100;++Seed) { FMath::RandInit(Seed); if(FMath::FRand()<.95f) break; } FMath::RandInit(Seed);
    Hero->CaptureComponent->TryCapture(Pet); Hero->ClaimObjectiveReward();
    Sequence &= Pet->Tamer==Hero->GetController() && Hero->GetObjectives().Claimed==2 && Hero->Inventory->Count(EFrontierItem::Material)==112;
    AWorkStation* Gather=nullptr;
    for(TActorIterator<AWorkStation> It(World);It;++It) if(It->RequiredSpecialty==EWorkSpecialty::Gathering) Gather=*It;
    AResourceNode* Source=nullptr;
    for(TActorIterator<AResourceNode> It(World);It;++It) if(It->PersistentId==FGuid(0xCF170000,0,0,1)) Source=*It;
    if(!Gather || !Source) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    auto SetStack=[Hero](EFrontierItem Item,int32 Amount) { auto Stacks=Hero->Inventory->GetStacks(); Stacks.FindOrAdd(Item)=Amount; Hero->Inventory->Restore(Stacks); };
    Pet->WorkSite=Gather; Pet->SetActivity(ECreatureActivity::Work); Pet->SetActorLocation(Gather->GetActorLocation()+FVector(0,180,55));
    SetStack(EFrontierItem::Material,999); Pet->Tick(2);
    bool Production=Hero->GetObjectives().WorkMaterials==0 && Pet->Experience==0;
    Hero->Inventory->Consume(EFrontierItem::Material,1); Pet->Tick(0);
    Production &= Hero->GetObjectives().WorkMaterials==1 && Pet->Experience==1;
    Hero->Inventory->Consume(EFrontierItem::Material,2); Source->Tick(2); Pet->Tick(2); Source->Tick(2); Pet->Tick(2);
    Production &= Hero->GetObjectives().WorkMaterials==3;
    Hero->ClaimObjectiveReward(); bool Capacity=Hero->GetObjectives().Claimed==2 && Hero->Inventory->Count(EFrontierItem::Material)==999;
    Hero->Inventory->Consume(EFrontierItem::Material,12); Hero->ClaimObjectiveReward();
    Capacity &= Hero->GetObjectives().Claimed==3 && Hero->Inventory->Count(EFrontierItem::Material)==999;
    Hero->Building->Selected=EWorkSpecialty::Farming;
    auto* Garden=Hero->Building->BuildAt(FTransform(FRotator::ZeroRotator,FVector(0,-700,-15),FVector(2,1,.7)));
    if(!Garden) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Garden->Crop->Seeds=2; // Objective credit requires two real seed inputs, not free garden output.
    Pet->WorkSite=Garden; Pet->SetActorLocation(Garden->GetActorLocation()+FVector(0,180,55));
    SetStack(EFrontierItem::Food,999); Garden->StoredFood=30; Pet->Tick(4); Production &= Hero->GetObjectives().FarmFood==0;
    Garden->AdjustFoodStock(-1); Pet->Tick(0); Production &= Hero->GetObjectives().FarmFood==1;
    Garden->AdjustFoodStock(-1); Pet->Tick(4); Production &= Hero->GetObjectives().FarmFood==2;
    Hero->Health=0; Hero->ClaimObjectiveReward(); bool Life=Hero->GetObjectives().Claimed==3 && Hero->Inventory->Count(EFrontierItem::Medicine)==0;
    Hero->Health=100; Hero->ClaimObjectiveReward(); Life &= Hero->GetObjectives().Claimed==4 && Hero->Inventory->Count(EFrontierItem::Medicine)==2;
    auto* Normal=World->SpawnActor<ACreatureCharacter>(FVector(1400,-1400,40),FRotator::ZeroRotator);
    UGameplayStatics::ApplyDamage(Normal,1000,Hero->GetController(),Hero,nullptr);
    bool Alpha=!Hero->GetObjectives().bAlphaResolved;
    auto* Boss=World->SpawnActor<ACreatureCharacter>(FVector(1100,-1100,40),FRotator::ZeroRotator); Boss->bAlpha=true;
    Hero->RecordAlphaResolution(Boss); Alpha &= !Hero->GetObjectives().bAlphaResolved;
    UGameplayStatics::ApplyDamage(Boss,1000,Hero->GetController(),Pet,nullptr);
    Alpha &= Hero->GetObjectives().bAlphaResolved; UGameplayStatics::ApplyDamage(Boss,1000,Hero->GetController(),Pet,nullptr);
    SetStack(EFrontierItem::Material,970); SetStack(EFrontierItem::CaptureDevice,999);
    const FString ReadySlot=TEXT("FrontierObjectiveReady"); Hero->SaveProgress(ReadySlot);
    Hero->ClaimObjectiveReward(); Capacity &= Hero->GetObjectives().Claimed==4 && Hero->Inventory->Count(EFrontierItem::Material)==970 && Hero->Inventory->Count(EFrontierItem::CaptureDevice)==999;
    Hero->Inventory->Consume(EFrontierItem::CaptureDevice,3); Hero->ClaimObjectiveReward();
    Capacity &= Hero->GetObjectives().Claimed==5 && Hero->Inventory->Count(EFrontierItem::Material)==990 && Hero->Inventory->Count(EFrontierItem::CaptureDevice)==999;
    Hero->ClaimObjectiveReward(); Capacity &= Hero->Inventory->Count(EFrontierItem::Material)==990 && Hero->Inventory->Count(EFrontierItem::CaptureDevice)==999;
    bool Snapshot=Hero->LoadProgress(ReadySlot) && Hero->GetObjectives().Claimed==4 && Hero->Inventory->Count(EFrontierItem::Material)==970;
    Hero->Inventory->Consume(EFrontierItem::CaptureDevice,3); Hero->ClaimObjectiveReward(); Snapshot &= Hero->GetObjectives().Claimed==5 && Hero->Inventory->Count(EFrontierItem::Material)==990;
    const FString Slot=TEXT("FrontierObjectiveAutomation"); Snapshot &= Hero->SaveProgress(Slot) && Hero->LoadProgress(Slot);
    Hero->ClaimObjectiveReward(); Snapshot &= Hero->GetObjectives().Claimed==5 && Hero->Inventory->Count(EFrontierItem::Material)==990;
    if(FParse::Param(FCommandLine::Get(),TEXT("CFObjectivePreview")))
    {
        Hero->LoadProgress(ReadySlot); Hero->GetCharacterMovement()->DisableMovement();
        const FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("JourneyPreview")); IFileManager::Get().MakeDirectory(*Root,true);
        FTimerHandle Shot,Claim,Shot2,Done;
        World->GetTimerManager().SetTimer(Shot,[Root](){FScreenshotRequest::RequestScreenshot(Root/TEXT("ready.png"),false,false);},3.f,false);
        World->GetTimerManager().SetTimer(Claim,[Hero](){Hero->Inventory->Consume(EFrontierItem::CaptureDevice,3);Hero->ClaimObjectiveReward();},4.f,false);
        World->GetTimerManager().SetTimer(Shot2,[Root](){FScreenshotRequest::RequestScreenshot(Root/TEXT("complete.png"),false,false);},6.f,false);
        World->GetTimerManager().SetTimer(Done,[](){FPlatformMisc::RequestExitWithStatus(false,0);},8.f,false); return;
    }
    auto* Invalid=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0)); Invalid->Objectives.Claimed=6;
    UGameplayStatics::SaveGameToSlot(Invalid,TEXT("FrontierObjectiveInvalid"),0);
    bool Validation=!Hero->LoadProgress(TEXT("FrontierObjectiveInvalid")) && Hero->GetObjectives().Claimed==5 && Hero->Inventory->Count(EFrontierItem::Material)==990;
    Invalid->Objectives.Claimed=3; Invalid->Objectives.WorkMaterials=0;
    UGameplayStatics::SaveGameToSlot(Invalid,TEXT("FrontierObjectiveInvalid"),0); Validation &= !Hero->LoadProgress(TEXT("FrontierObjectiveInvalid")) && Hero->GetObjectives().Claimed==5;
    auto* Legacy=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0)); Legacy->FormatVersion=12;
    for(auto& Facility:Legacy->Facilities) if(Facility.Specialty==EWorkSpecialty::Farming) Facility.StoredFood=0;
    UGameplayStatics::SaveGameToSlot(Legacy,TEXT("FrontierObjectiveLegacy"),0);
    bool Migration=Hero->LoadProgress(TEXT("FrontierObjectiveLegacy")) && Hero->GetObjectives().Claimed==0 && Hero->GetObjectives().WorkMaterials==0 && Hero->GetObjectives().FarmFood==0 && !Hero->GetObjectives().bAlphaResolved && Hero->GetObjectiveProgress(0)==1 && Hero->GetObjectiveProgress(1)==1;
    auto* CapturedAlpha=World->SpawnActor<ACreatureCharacter>(FVector(-800,-1000,40),FRotator::ZeroRotator); CapturedAlpha->bAlpha=true;
    Hero->CaptureResolved(CapturedAlpha,false); Alpha &= !Hero->GetObjectives().bAlphaResolved;
    CapturedAlpha->Tame(Hero->GetController()); Hero->CaptureResolved(CapturedAlpha,true); Alpha &= Hero->GetObjectives().bAlphaResolved;
    Hero->SaveProgress(TEXT("FrontierObjectiveOwnedAlpha"));
    auto* OwnedLegacy=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("FrontierObjectiveOwnedAlpha"),0)); OwnedLegacy->FormatVersion=12;
    for(auto& Facility:OwnedLegacy->Facilities) if(Facility.Specialty==EWorkSpecialty::Farming) Facility.StoredFood=0;
    UGameplayStatics::SaveGameToSlot(OwnedLegacy,TEXT("FrontierObjectiveOwnedAlpha"),0);
    Migration &= Hero->LoadProgress(TEXT("FrontierObjectiveOwnedAlpha")) && Hero->GetObjectives().bAlphaResolved && Hero->GetObjectives().Claimed==0;
    const bool Pass=Sequence && Production && Capacity && Life && Alpha && Snapshot && Validation && Migration;
    UE_LOG(LogTemp,Display,TEXT("CF_OBJECTIVE %s sequence=%d production=%d capacity=%d life=%d alpha=%d snapshot=%d invalid=%d migration=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Sequence,Production,Capacity,Life,Alpha,Snapshot,Validation,Migration);
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}
