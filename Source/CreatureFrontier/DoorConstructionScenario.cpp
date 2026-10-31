#include "ConstructionScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "HomeModule.h"
#include "HomeBuildingComponent.h"
#include "HomeLayout.h"
#include "InventoryComponent.h"
#include "CompanionCargoComponent.h"
#include "CompanionConstructionComponent.h"
#include "FrontierSaveGame.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/Ticker.h"
#include "NavigationSystem.h"

// 独立夹具验证原施工管线，不冒充探索所得或玩家自由建房流程。
void RunDoorConstructionScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto Fail=[Hero](const TCHAR* Reason){UE_LOG(LogTemp,Error,TEXT("CF_DOOR_CONSTRUCTION FAIL reason=%s feedback=%s"),Reason,*Hero->Feedback);FPlatformMisc::RequestExitWithStatus(false,1);};
    if(Read)
    {
        if(!Hero->LoadProgress(TEXT("DoorConstructionInput"))){Fail(TEXT("load"));return;}
    }
    else
    {
        Hero->SetActorLocation(FVector(-1050,-950,60));
        Hero->Inventory->Restore({{EFrontierItem::Material,26}});
        Hero->HomeBuilding->Selected=EHomeModuleKind::Floor;
        auto* Floor=Hero->HomeBuilding->BuildAt(FTransform(FVector(-400,-800,-40)));
        if(!Floor){Fail(TEXT("floor"));return;}
        Hero->HomeBuilding->Selected=EHomeModuleKind::Storage;
        auto* Box=Hero->HomeBuilding->BuildAt(FTransform(FVector(-500,-900,-30)),Floor->PersistentId);
        Hero->HomeBuilding->Selected=EHomeModuleKind::Door;
        auto* Door=Hero->HomeBuilding->QueueAt(FTransform(FVector(-400,-600,-30)),Floor->PersistentId);
        if(!Box || !Door || Door->DoorState!=1 || Door->PaidMaterials || !Hero->Inventory->TransferTo(Box->StorageInventory,{{EFrontierItem::Material,12}}) || Door->ToggleDoor(Hero))
        {Fail(TEXT("queue"));return;}
        const FTransform Start(FVector(-750,-800,40));
        auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),Start,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Pet){Fail(TEXT("pet"));return;}
        Pet->Definition=LoadObject<UCreatureDefinition>(nullptr,TEXT("/Game/Creatures/DA_Mosskip.DA_Mosskip"));Pet->FinishSpawning(Start);Pet->Tame(Hero->GetController());
        if(!Pet->Construction->Assign(Door,Box,Hero)){Fail(TEXT("assign"));return;}
    }
    AHomeModule* Door=nullptr;ACreatureCharacter* Pet=nullptr;
    for(TActorIterator<AHomeModule> It(World);It;++It)if(It->GetKind()==EHomeModuleKind::Door && !It->bPreview)Door=*It;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController())Pet=*It;
    if(!Door || !Pet || (Read && (Pet->Cargo->Count()!=3 || Pet->Construction->GetSite()!=Door))){Fail(TEXT("source"));return;}
    const FGuid DoorId=Door->PersistentId,PetId=Pet->PersistentId;
    const double Deadline=FPlatformTime::Seconds()+90;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([=,Mid=Read](float) mutable
    {
        AHomeModule* Site=nullptr;ACreatureCharacter* Worker=nullptr;int32 Total=Hero->Inventory->Count(EFrontierItem::Material);
        for(TActorIterator<AHomeModule> It(World);It;++It)if(!It->bPreview && !It->bStaged){Total+=It->PaidMaterials+It->StorageInventory->Count(EFrontierItem::Material);if(It->PersistentId==DoorId)Site=*It;}
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->PersistentId==PetId)Worker=*It;
        if(!Site || !Worker || FPlatformTime::Seconds()>Deadline || Total+Worker->Cargo->Count()!=26){Fail(TEXT("budget-time"));return false;}
        if(Site->bUnderConstruction)
        {
            if(Site->DoorState!=1 || Site->ToggleDoor(Hero)){Fail(TEXT("unfinished-toggle"));return false;}
            if(!Mid && Worker->Cargo->Count()==3)
            {
                FString Reason;
                if(!HomeLayout::Validate(HomeLayout::Collect(World),Reason) || !Hero->SaveProgress(TEXT("DoorConstructionMid"))){Fail(TEXT("mid-save"));return false;}
                auto* Bad=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("DoorConstructionMid"),0));
                auto* Record=Bad?Bad->HomeModules.FindByPredicate([&](const FHomeModuleRecord& R){return R.Id==DoorId;}):nullptr;
                if(!Record){Fail(TEXT("mid-record"));return false;}Record->DoorState=2;
                if(!UGameplayStatics::SaveGameToSlot(Bad,TEXT("DoorConstructionBad"),0) || Hero->LoadProgress(TEXT("DoorConstructionBad")) || Worker->Cargo->Count()!=3 || Worker->Construction->GetSite()!=Site){Fail(TEXT("bad-open-state"));return false;}
                Mid=true;UE_LOG(LogTemp,Display,TEXT("CF_DOOR_CONSTRUCTION_MID PASS carried=3 unfinishedToggle=0 badOpenRejected=1"));
            }
            return true;
        }
        FString Reason;
        if(!Mid || Site->PaidMaterials!=12 || Site->DoorState!=1 || Worker->Cargo->Count()!=0 || Worker->Construction->GetSite() || !HomeLayout::Validate(HomeLayout::Collect(World),Reason))
        {Fail(TEXT("complete"));return false;}
        if(!Hero->SaveProgress(TEXT("DoorConstructionDone")) || !Hero->LoadProgress(TEXT("DoorConstructionDone"))){Fail(TEXT("done-save"));return false;}
        UE_LOG(LogTemp,Display,TEXT("CF_DOOR_CONSTRUCTION PASS paid=12 total=26 closed=1 carried=0 restored=1 read=%d"),Read);FPlatformMisc::RequestExitWithStatus(false,0);return false;
    }));
}
