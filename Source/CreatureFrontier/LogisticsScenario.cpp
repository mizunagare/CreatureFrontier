#include "LogisticsScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "WorkStation.h"
#include "BaseBuildingComponent.h"
#include "InventoryComponent.h"
#include "FrontierSaveGame.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "TimerManager.h"

static void FinishLogisticsChecks(UWorld* World,ACFPlayerCharacter* Hero,AWorkStation* Garden,AWorkStation* Trough,AWorkStation* Post,ACreatureCharacter* Courier,ACreatureCharacter* Farmer,bool Production,bool Trip,bool Meal)
{
    auto Freeze=[](ACreatureCharacter* Pet) { Pet->SetActorTickEnabled(false); Pet->GetCharacterMovement()->StopMovementImmediately(); Pet->GetCharacterMovement()->DisableMovement(); };
    Freeze(Farmer); Freeze(Courier); Courier->Nutrition=100;
    const FGuid GardenId=Garden->PersistentId,TroughId=Trough->PersistentId,PostId=Post->PersistentId;
    Hero->SetActorLocation(FVector(-350,-1050,40));
    Garden->StoredFood=3; Trough->StoredFood=30;
    const int32 MaterialsBefore=Hero->Inventory->Count(EFrontierItem::Material);
    bool Storage=Garden->TransferFood(Hero,false) && Garden->StoredFood==0 && Hero->Inventory->Count(EFrontierItem::Food)==3;
    Storage &= Garden->TransferFood(Hero,true) && Garden->StoredFood==3 && Hero->Inventory->Count(EFrontierItem::Food)==0;
    Storage &= !Hero->Building->DismantleStation(Garden) && Hero->Inventory->Count(EFrontierItem::Material)==MaterialsBefore;
    Courier->SetActorLocation(FVector(0,-900,40)); Courier->Tick(3);
    bool Full=Garden->StoredFood==3 && Courier->CarriedFood==0;
    Trough->StoredFood=29; Courier->Tick(3);
    bool Pickup=Garden->StoredFood==0 && Courier->CarriedFood==3 && Trough->StoredFood==29;
    const FString Slot=TEXT("FrontierLogisticsAutomation");
    bool Saved=Hero->SaveProgress(Slot) && Hero->LoadProgress(Slot) && Hero->LoadProgress(Slot);
    auto Rebind=[&]()
    {
        Garden=nullptr; Trough=nullptr; Post=nullptr; Courier=nullptr; Farmer=nullptr;
        for(TActorIterator<AWorkStation> It(World);It;++It) { if(It->PersistentId==GardenId) Garden=*It; if(It->PersistentId==TroughId) Trough=*It; if(It->PersistentId==PostId) Post=*It; }
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)
            if(It->Tamer==Hero->GetController()) { Freeze(*It); if(It->WorkSite && It->WorkSite->RequiredSpecialty==EWorkSpecialty::Transport) Courier=*It; else Farmer=*It; }
        return Garden && Trough && Post && Courier && Farmer;
    };
    if(!Rebind()) { UE_LOG(LogTemp,Error,TEXT("CF_LOGISTICS FAIL restore references")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Saved &= Garden->StoredFood==0 && Trough->StoredFood==29 && Courier->CarriedFood==3;
    Courier->SetActorLocation(FVector(0,-900,40)); Courier->Tick(0);
    bool Partial=Courier->CarriedFood==2 && Trough->StoredFood==30 && Garden->StoredFood==0;
    bool Orders=Hero->SelectCompanion(Courier); Hero->CommandFollow(); Orders &= Courier->CarriedFood==2 && !Courier->WorkSite && Farmer->WorkSite==Garden;
    Hero->SetActorLocation(FVector(-450,-900,40)); Hero->CommandWork(); Orders &= Courier->WorkSite==Post && Courier->Activity==ECreatureActivity::Work && Courier->CarriedFood==2;
    Hero->SetActorLocation(FVector(-350,-1050,40));
    auto Stacks=Hero->Inventory->GetStacks(); Stacks.FindOrAdd(EFrontierItem::Food)=999; Hero->Inventory->Restore(Stacks);
    bool Recovery=!Hero->WithdrawCompanionCargo(Courier) && Courier->CarriedFood==2;
    Hero->Inventory->Consume(EFrontierItem::Food,1); Recovery &= Hero->WithdrawCompanionCargo(Courier) && Courier->CarriedFood==1 && Hero->Inventory->Count(EFrontierItem::Food)==999;
    UGameplayStatics::ApplyDamage(Courier,1000,nullptr,nullptr,nullptr); Hero->Inventory->Consume(EFrontierItem::Food,1);
    Recovery &= Courier->Health==0 && Courier->CarriedFood==1 && Hero->WithdrawCompanionCargo(Courier) && Courier->Health==0 && Courier->CarriedFood==0;
    Courier->Health=Courier->GetMaxHealth(); Courier->CarriedFood=1; Courier->Nutrition=0; Courier->AutoFeedCooldown=0; Courier->Stamina=20;
    Courier->SetActivity(ECreatureActivity::Rest); Courier->WorkSite=Post; Courier->bResumeWorkAfterRest=true; Courier->Tick(0);
    bool Emergency=Courier->CarriedFood==0 && Courier->Nutrition==35 && Courier->Stamina==35 && Courier->AutoFeedCooldown==3 && Trough->StoredFood==30;
    Courier->Nutrition=100; Courier->AutoFeedCooldown=0; Courier->Stamina=.1f; Courier->CarriedFood=2; Courier->SetActivity(ECreatureActivity::Work);
    Courier->Tick(.2f); bool Rest=Courier->Activity==ECreatureActivity::Rest && Courier->bResumeWorkAfterRest && Courier->CarriedFood==2;
    Courier->SetActorLocation(Post->GetActorLocation()+FVector(0,180,55)); Courier->Tick(7);
    Rest &= Courier->Activity==ECreatureActivity::Work && !Courier->bResumeWorkAfterRest && Courier->CarriedFood==2;
    const FString StateSlot=TEXT("FrontierLogisticsState"); Hero->SaveProgress(StateSlot);
    auto* Bad=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(StateSlot,0));
    for(auto& Record:Bad->Creatures) if(Record.WorkSiteId==PostId) Record.CarriedFood=4;
    UGameplayStatics::SaveGameToSlot(Bad,TEXT("FrontierLogisticsInvalid"),0);
    bool Validation=!Hero->LoadProgress(TEXT("FrontierLogisticsInvalid")) && Courier->CarriedFood==2 && Trough->StoredFood==30;
    Bad=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(StateSlot,0)); for(auto& Facility:Bad->Facilities) if(Facility.Id==GardenId) Facility.StoredFood=31;
    UGameplayStatics::SaveGameToSlot(Bad,TEXT("FrontierLogisticsInvalid"),0);
    Validation &= !Hero->LoadProgress(TEXT("FrontierLogisticsInvalid")) && Courier->CarriedFood==2 && Garden->StoredFood==0;
    auto* Legacy=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(StateSlot,0)); Legacy->FormatVersion=14;
    Legacy->Facilities.RemoveAll([](const auto& Facility){return Facility.Specialty==EWorkSpecialty::Transport;});
    for(auto& Facility:Legacy->Facilities) if(Facility.Specialty==EWorkSpecialty::Farming) Facility.StoredFood=0;
    for(auto& Record:Legacy->Creatures)
    {
        Record.CarriedFood=0;
        if(Record.WorkSiteId==PostId) { Record.WorkSiteId.Invalidate(); Record.WorkSiteName=NAME_None; Record.Activity=ECreatureActivity::Follow; Record.bResumeWorkAfterRest=false; }
    }
    UGameplayStatics::SaveGameToSlot(Legacy,TEXT("FrontierLogisticsLegacy"),0);
    bool Migration=Hero->LoadProgress(TEXT("FrontierLogisticsLegacy")) && Hero->Inventory->Count(EFrontierItem::Food)==999;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It) Migration &= It->CarriedFood==0;
    for(TActorIterator<AWorkStation> It(World);It;++It) Migration &= It->RequiredSpecialty!=EWorkSpecialty::Transport;
    const bool Pass=Production && Trip && Meal && Storage && Full && Pickup && Saved && Partial && Orders && Recovery && Emergency && Rest && Validation && Migration;
    UE_LOG(LogTemp,Display,TEXT("CF_LOGISTICS %s production=%d trip=%d meal=%d storage=%d full=%d pickup=%d save=%d partial=%d orders=%d recovery=%d emergency=%d rest=%d invalid=%d migration=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Production,Trip,Meal,Storage,Full,Pickup,Saved,Partial,Orders,Recovery,Emergency,Rest,Validation,Migration);
    if(Pass && FParse::Param(FCommandLine::Get(),TEXT("CFLogisticsPreview")))
    {
        Hero->LoadProgress(Slot); Rebind();
        if(Courier) { Courier->Tick(0); Freeze(Courier); }
        Hero->SetActorLocation(FVector(-600,-1300,220)); Hero->GetCharacterMovement()->DisableMovement();
        Hero->ClearCompanionSelection();
        if(Courier) { const auto Look=(Courier->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation(); Hero->GetController()->SetControlRotation(Look); Hero->FirstPersonCamera->SetWorldRotation(Look); }
        const FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("LogisticsPreview")); IFileManager::Get().MakeDirectory(*Root,true);
        FTimerHandle Shot,Done; World->GetTimerManager().SetTimer(Shot,[Root](){FScreenshotRequest::RequestScreenshot(Root/TEXT("food_route.png"),false,false);},3.f,false);
        World->GetTimerManager().SetTimer(Done,[](){FPlatformMisc::RequestExitWithStatus(false,0);},5.f,false); return;
    }
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}

void RunLogisticsScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    UCreatureDefinition* Definition=nullptr;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It) { if(It->Definition && It->Definition->Form==ECreatureForm::Leaf) Definition=It->Definition; It->Destroy(); }
    if(!Definition) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Hero->SetActorLocation(FVector(-350,-1400,40)); Hero->GetCharacterMovement()->DisableMovement(); Hero->Inventory->Restore({{EFrontierItem::Material,150}});
    auto Build=[&](EWorkSpecialty Kind,int32 X,int32 Y) { Hero->Building->Selected=Kind; return Hero->Building->BuildAt(FTransform(FRotator::ZeroRotator,FVector(X,Y,-15),FVector(2,1,.7))); };
    auto* Garden=Build(EWorkSpecialty::Farming,0,-700); auto* Trough=Build(EWorkSpecialty::Feeding,0,-1100); auto* Post=Build(EWorkSpecialty::Transport,-300,-700);
    if(!Garden || !Trough || !Post) { UE_LOG(LogTemp,Error,TEXT("CF_LOGISTICS FAIL building %s"),*Hero->Feedback); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    auto Make=[&](FVector Position)
    {
        const FTransform Transform(Position); auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Pet->Definition=Definition; Pet->FinishSpawning(Transform); Pet->Tame(Hero->GetController()); return Pet;
    };
    auto* Farmer=Make(FVector(0,-520,40)); Farmer->SetActorTickEnabled(false); Farmer->GetCharacterMovement()->DisableMovement(); Farmer->WorkSite=Garden; Farmer->SetActivity(ECreatureActivity::Work);
    Farmer->Tick(3.4f); Farmer->Tick(3.4f); Farmer->Tick(3.4f);
    const bool Production=Garden->StoredFood==3 && Farmer->Experience==3 && Hero->Inventory->Count(EFrontierItem::Food)==0;
    auto* Courier=Make(FVector(-550,-1000,40)); Courier->WorkSite=Post; Courier->SetActivity(ECreatureActivity::Work);
    Hero->SetActorLocation(FVector(-600,-1300,40));
    auto Stage=MakeShared<int32>(0); auto Trip=MakeShared<bool>(false); auto Poll=MakeShared<FTimerHandle>(); const double Deadline=World->GetTimeSeconds()+25;
    World->GetTimerManager().SetTimer(*Poll,[World,Hero,Garden,Trough,Post,Courier,Farmer,Stage,Trip,Poll,Production,Deadline]()
    {
        if(*Stage==0 && Garden->StoredFood==0 && Trough->StoredFood==3 && Courier->CarriedFood==0)
        {
            *Trip=true; *Stage=1; Farmer->Nutrition=30; Farmer->Stamina=50; Farmer->SetActorTickEnabled(true); Farmer->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        }
        const bool Ate=*Stage==1 && Trough->StoredFood==2 && Farmer->Nutrition>60;
        if(Ate || World->GetTimeSeconds()>Deadline)
        {
            UE_LOG(LogTemp,Display,TEXT("CF_LOGISTICS_ROUTE farmer=%s courier=%s farm=%d trough=%d cargo=%d satiety=%.1f"),*Farmer->GetActorLocation().ToString(),*Courier->GetActorLocation().ToString(),Garden->StoredFood,Trough->StoredFood,Courier->CarriedFood,Farmer->Nutrition);
            FinishLogisticsChecks(World,Hero,Garden,Trough,Post,Courier,Farmer,Production,*Trip,Ate);
            // Last capture access: clearing a repeating timer destroys its running delegate.
            World->GetTimerManager().ClearTimer(*Poll);
        }
    },.2f,true);
}
