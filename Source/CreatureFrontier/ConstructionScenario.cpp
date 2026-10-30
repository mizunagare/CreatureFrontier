#include "ConstructionScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "CompanionConstructionComponent.h"
#include "CompanionCargoComponent.h"
#include "HomeModule.h"
#include "HomeLayout.h"
#include "HomeBuildingComponent.h"
#include "InventoryComponent.h"
#include "FrontierSaveGame.h"
#include "WildEncounter.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/Ticker.h"
#include "InputRoutingQA.h"
#include "BaseBuildingComponent.h"
#include "WorkStation.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "InputKeyEventArgs.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

namespace
{
struct FConstructionTest
{
    UWorld* World;ACFPlayerCharacter* Hero;TWeakObjectPtr<ACreatureCharacter> Pet;TWeakObjectPtr<AHomeModule> Site,Crate;
    FGuid PetId,SiteId,CrateId;int32 Phase=0;double At=0,Deadline=0,Travel=0;FVector Previous;
    bool Needs=false,CancelledOnce=false,Fed=false;float FeedingWork=0;
    bool WaitAudit=false,WaitingRestore=false,RefillCarried=false;float WaitingStamina=0;
    TWeakObjectPtr<AWorkStation> Trough;
    bool Preview=false;FString ShotRoot;TSet<FString> Shots;double ShotUntil=0;
    bool Rebind()
    {
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController()){Pet=*It;PetId=It->PersistentId;}
        for(TActorIterator<AHomeModule> It(World);It;++It){if(It->bUnderConstruction || It->PersistentId==SiteId){Site=*It;SiteId=It->PersistentId;}if(It->GetKind()==EHomeModuleKind::Storage){Crate=*It;CrateId=It->PersistentId;}}
        if(!Pet.IsValid() || !Site.IsValid() || !Crate.IsValid())return false;
        for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt && It->RequiredSpecialty==EWorkSpecialty::Feeding)Trough=*It;
        if(Needs && !Trough.IsValid())return false;
        for(TActorIterator<AWildEncounter> It(World);It;++It)It->SetActorTickEnabled(false);
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(*It!=Pet.Get()){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();}
        Previous=Pet->GetActorLocation();return true;
    }
    void Face(AActor* Actor){const auto* Module=Cast<AHomeModule>(Actor);const auto R=(Actor->GetActorLocation()+FVector(0,0,Module && Module->bUnderConstruction?12:30)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(R);Hero->FirstPersonCamera->SetWorldRotation(R);}
    bool Photo(const TCHAR* Scene)
    {
        if(!Preview || Shots.Contains(Scene))return false;Shots.Add(Scene);Face(Site.Get());
        FScreenshotRequest::RequestScreenshot(ShotRoot/(FString(Scene)+TEXT(".png")),false,false);ShotUntil=FPlatformTime::Seconds()+.15;
        UE_LOG(LogTemp,Display,TEXT("CF_CONSTRUCTION_SHOT scene=%s requested=1 frame=%llu"),Scene,static_cast<uint64>(GFrameCounter));return true;
    }
    bool Failures()
    {
        const float Stamina=Pet->Stamina;const float Work=Site->ConstructionWork;
        // 独立无导航料箱仅用于查询节流探针，不把其材料投入正常施工，也不保存该临时实体。
        auto* Unreachable=World->SpawnActorDeferred<AHomeModule>(AHomeModule::StaticClass(),FTransform(FVector(-5000,-5000,-30)),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Unreachable)return false;Unreachable->Definition=UHomeModuleDefinition::LoadDefault(EHomeModuleKind::Storage);Unreachable->PersistentId=FGuid::NewGuid();Unreachable->PaidMaterials=6;Unreachable->FinishSpawning(Unreachable->GetActorTransform());Unreachable->StorageInventory->AddItem(EFrontierItem::Material,1);
        if(!Pet->Construction->Assign(Site.Get(),Unreachable,Hero)){Unreachable->Destroy();return false;}
        const uint32 BeforeSearches=Pet->Construction->GetApproachSearchCount();
        for(int32 Attempt=0;Attempt<60;++Attempt)Pet->Construction->TickWork(0,Hero);
        const uint32 Searches=Pet->Construction->GetApproachSearchCount()-BeforeSearches;
        const bool Untouched=Unreachable->StorageInventory->Count(EFrontierItem::Material)==1 && Pet->Cargo->Count()==0;
        const bool Reassigned=Pet->Construction->Assign(Site.Get(),Crate.Get(),Hero);Unreachable->Destroy();
        UE_LOG(LogTemp,Display,TEXT("CF_CONSTRUCTION_PATH_RETRY %s calls=60 searches=%u retained=%d"),Searches==1 && Untouched && Reassigned?TEXT("PASS"):TEXT("FAIL"),Searches,Untouched && Reassigned);
        if(Searches!=1 || !Untouched || !Reassigned || HomeLayout::Collect(World).Num()!=5)return false;
        UGameplayStatics::SetGamePaused(World,true);Pet->Construction->TickWork(1,Hero);UGameplayStatics::SetGamePaused(World,false);
        if(Pet->Stamina!=Stamina || Site->ConstructionWork!=Work || Hero->HomeBuilding->Dismantle(Crate.Get()))return false;
        TGuardValue<EHomeModuleKind> Kind(Hero->HomeBuilding->Selected,EHomeModuleKind::Fence);
        auto* Cancelled=Hero->HomeBuilding->QueueAt(FTransform(FVector(-800,-1000,-30)),Crate->FloorId);
        if(!Cancelled || !Hero->Inventory->TransferTo(Cancelled->StorageInventory,{{EFrontierItem::Material,2}}))return false;
        // 仅容量拒绝单元使用满背包夹具；检查后恢复原背包，不能把夹具材料投入施工。
        const auto Bag=Hero->Inventory->GetStacks();Hero->Inventory->AddItem(EFrontierItem::Material,999-Hero->Inventory->Count(EFrontierItem::Material));
        const bool Kept=!Hero->HomeBuilding->Dismantle(Cancelled) && !Cancelled->IsActorBeingDestroyed() && Cancelled->StorageInventory->Count(EFrontierItem::Material)==2;
        Hero->Inventory->Restore(Bag);
        if(!Kept || !Hero->HomeBuilding->Dismantle(Cancelled) || Hero->Inventory->Count(EFrontierItem::Material)!=69)return false;
        UE_LOG(LogTemp,Display,TEXT("CF_CONSTRUCTION_CANCEL_CAPACITY PASS retained=2 refunded=2"));
        if(!Hero->SaveProgress(TEXT("ConstructionShortage")))return false;
        for(int32 Case=0;Case<7;++Case)
        {
            auto* Bad=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("ConstructionShortage"),0));if(!Bad)return false;
            auto* Module=Bad->HomeModules.FindByPredicate([&](const FHomeModuleRecord& R){return R.Id==SiteId;});
            auto* Worker=Bad->Creatures.FindByPredicate([&](const FCreatureSaveRecord& R){return R.Id==PetId;});if(!Module || !Worker)return false;
            if(Case==0)Module->ConstructionCrateId=FGuid::NewGuid();
            if(Case==1)Bad->FormatVersion=34;
            if(Case==2)Module->StoredItems={{EFrontierItem::Material,7}};
            if(Case==3)Module->StoredItems={{EFrontierItem::Fiber,1}};
            if(Case==4)Module->ConstructionWork=1;
            if(Case==5)Worker->ConstructionModuleId=FGuid::NewGuid();
            if(Case==6){auto Other=*Worker;Other.Id=FGuid::NewGuid();Bad->Creatures.Add(Other);}
            if(!UGameplayStatics::SaveGameToSlot(Bad,TEXT("ConstructionBad"),0) || Hero->LoadProgress(TEXT("ConstructionBad")) || !Pet.IsValid() || !Site.IsValid() ||
                Pet->PersistentId!=PetId || Site->PersistentId!=SiteId || Pet->Construction->GetSite()!=Site.Get() || Hero->Inventory->Count(EFrontierItem::Material)!=69 ||
                Site->StorageInventory->Count(EFrontierItem::Material)!=3 || Crate->StorageInventory->Count(EFrontierItem::Material)!=0)return false;
        }
        UE_LOG(LogTemp,Display,TEXT("CF_CONSTRUCTION_BAD PASS cases=7 retained=1 cancel=1 paused=1 sourceProtected=1"));return true;
    }
    bool Finish(bool Pass,const TCHAR* Reason)
    {
        UE_LOG(LogTemp,Display,TEXT("CF_CONSTRUCTION %s reason=%s phase=%d travel=%.1f pet=%s site=%s material=%d stock=%d supplied=%d carried=%d feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Reason,Phase,Travel,*PetId.ToString(),*SiteId.ToString(),Hero->Inventory->Count(EFrontierItem::Material),Crate.IsValid()?Crate->StorageInventory->Count(EFrontierItem::Material):-1,Site.IsValid()?Site->StorageInventory->Count(EFrontierItem::Material):-1,Pet.IsValid()?Pet->Cargo->Count():-1,*Hero->Feedback);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    bool Tick()
    {
        if(FPlatformTime::Seconds()<ShotUntil)return true;
        if(!Pet.IsValid() || !Site.IsValid() || !Crate.IsValid() || FPlatformTime::Seconds()>Deadline)return Finish(false,TEXT("lifetime"));
        const auto Position=Pet->GetActorLocation();const double Step=FVector::Dist(Position,Previous);Travel+=Step;Previous=Position;
        if(Step>120 || !Pet->IsActorTickEnabled() || !Pet->GetActorEnableCollision() || Pet->Health<=0 || Pet->GetCharacterMovement()->MovementMode==MOVE_None)return Finish(false,TEXT("continuous"));
        const int32 Budget=Hero->Inventory->Count(EFrontierItem::Material)+Crate->StorageInventory->Count(EFrontierItem::Material)+Site->StorageInventory->Count(EFrontierItem::Material)+Pet->Cargo->Count()+(Site->bUnderConstruction?0:Site->PaidMaterials);
        if(Budget!=72)return Finish(false,TEXT("conservation"));
        if(Phase==0 && Pet->Cargo->Count()>0)
        {
            if(!CancelledOnce)
            {
                if(!CFInputQA::Press(Hero,EKeys::F) || Pet->Construction->GetSite() || Pet->Cargo->Count()!=3 ||
                    !Hero->SaveProgress(TEXT("ConstructionCancelled")) || !Hero->LoadProgress(TEXT("ConstructionCancelled")) || !Rebind() || Pet->Construction->GetSite() || Pet->Activity!=ECreatureActivity::Follow || Pet->Cargo->Count()!=3)return Finish(false,TEXT("cancel-carried"));
                Face(Pet.Get());if(!CFInputQA::Press(Hero,EKeys::Z) || Pet->Cargo->Count()!=0 || Hero->Inventory->Count(EFrontierItem::Material)!=72 ||
                    !Hero->Inventory->TransferTo(Crate->StorageInventory,{{EFrontierItem::Material,3}}))return Finish(false,TEXT("cancel-recover"));
                Face(Crate.Get());if(!CFInputQA::Press(Hero,EKeys::V))return Finish(false,TEXT("cancel-select"));Face(Site.Get());
                if(!CFInputQA::Press(Hero,EKeys::E) || Pet->Construction->GetSite()!=Site.Get())return Finish(false,TEXT("cancel-reassign"));
                CancelledOnce=true;UE_LOG(LogTemp,Display,TEXT("CF_CONSTRUCTION_CANCEL_CARRIED PASS saved=1 held=3 recovered=3 reassigned=1"));return true;
            }
            if(!Hero->SaveProgress(TEXT("ConstructionMid")))return Finish(false,TEXT("mid-save"));
            const auto OldPet=PetId,OldSite=SiteId,OldBox=CrateId;const auto Pose=Pet->GetActorTransform();
            if(!Hero->LoadProgress(TEXT("ConstructionMid")) || !Rebind() || PetId!=OldPet || SiteId!=OldSite || CrateId!=OldBox || !Pose.Equals(Pet->GetActorTransform(),.01) || Pet->Construction->GetSite()!=Site.Get() || Pet->Cargo->Count()!=3)return Finish(false,TEXT("mid-restore"));
            UE_LOG(LogTemp,Display,TEXT("CF_CONSTRUCTION_MID PASS version=%d carried=3 identity=1 intent=1 pet=%s site=%s"),FrontierCurrentSaveVersion,*PetId.ToString(),*SiteId.ToString());Phase=1;
        }
        if(Phase==1 && Site->StorageInventory->Count(EFrontierItem::Material)==3 && Pet->Cargo->Count()==0){Phase=2;At=World->GetTimeSeconds();WaitingStamina=Pet->Stamina;}
        if(Phase==2 && World->GetTimeSeconds()-At>(WaitAudit?5:2))
        {
            if(WaitAudit)
            {
                const float Loss=WaitingStamina-Pet->Stamina;
                UE_LOG(LogTemp,Display,TEXT("CF_CONSTRUCTION_WAIT_OBSERVED seconds=%.3f staminaLoss=%.6f work=%.3f cargo=%d stock=%d intent=%d"),World->GetTimeSeconds()-At,Loss,Site->ConstructionWork,Pet->Cargo->Count(),Crate->StorageInventory->Count(EFrontierItem::Material),Pet->Construction->GetSite()==Site.Get());
                if(!Hero->SaveProgress(TEXT("ConstructionShortage")))return Finish(false,TEXT("wait-save"));
                if(!FMath::IsNearlyZero(Loss,.01f) || Site->ConstructionWork!=0 || Pet->Activity!=ECreatureActivity::Work || Pet->Construction->GetSite()!=Site.Get())return Finish(false,TEXT("wait-drain"));
                UE_LOG(LogTemp,Display,TEXT("CF_CONSTRUCTION_WAIT PASS noDrain=1 noRecovery=1 sameTask=1"));
            }
            if(Photo(TEXT("Shortage")))return true;
            if(Site->ConstructionWork!=0 || !Site->bUnderConstruction || Crate->StorageInventory->Count(EFrontierItem::Material)!=0)return Finish(false,TEXT("shortage"));
            UE_LOG(LogTemp,Display,TEXT("CF_CONSTRUCTION_SHORTAGE PASS supplied=3 work=0 reserved=1"));
            if(!Failures())return Finish(false,TEXT("failure-paths"));
            // 通过原库存事务补入已在玩家背包的三份材料，不修改工地或工人携带量。
            if(!Hero->Inventory->TransferTo(Crate->StorageInventory,{{EFrontierItem::Material,3}}))return Finish(false,TEXT("refill"));
            Phase=3;
        }
        if(Phase==3 && Pet->Cargo->Count()==3)RefillCarried=true;
        if(Phase==3 && Site->ConstructionWork>1)
        {
            if(Photo(TEXT("Working")))return true;
            if(Needs && !Fed){FeedingWork=Site->ConstructionWork;Pet->Nutrition=39;Pet->AutoFeedCooldown=0;Phase=6;return true;}
            // 已实际搬完两批后，以明确低体力夹具覆盖正常耗尽／休息／恢复分支。
            Pet->Stamina=.1f;Phase=4;
        }
        if(Phase==6 && Trough.IsValid() && Trough->StoredFood==0)
        {
            if(Pet->Nutrition<60 || Pet->GetDistanceTo(Trough.Get())>220 || Pet->Construction->GetSite()!=Site.Get() || Site->ConstructionWork!=FeedingWork || Hero->Inventory->Count(EFrontierItem::Food)!=0)return Finish(false,TEXT("feeding"));
            Fed=true;Phase=7;UE_LOG(LogTemp,Display,TEXT("CF_CONSTRUCTION_FED PASS consumed=1 intent=1 workRetained=1"));
        }
        if(Phase==7 && Site->ConstructionWork>FeedingWork+.2f)
        {Phase=3;UE_LOG(LogTemp,Display,TEXT("CF_CONSTRUCTION_FOOD_RESUME PASS actualReturn=1 workAdvanced=1"));}
        if(Phase==4 && Pet->Activity==ECreatureActivity::Rest)
        {
            const float Work=Site->ConstructionWork;
            if(!Pet->bResumeWorkAfterRest || !Hero->SaveProgress(TEXT("ConstructionRest")) || !Hero->LoadProgress(TEXT("ConstructionRest")) || !Rebind() ||
                Pet->Activity!=ECreatureActivity::Rest || !Pet->bResumeWorkAfterRest || Pet->Construction->GetSite()!=Site.Get() || !FMath::IsNearlyEqual(Work,Site->ConstructionWork,.001f))return Finish(false,TEXT("rest-save"));
            UE_LOG(LogTemp,Display,TEXT("CF_CONSTRUCTION_REST PASS version=%d work=%.3f intent=1 pet=%s site=%s"),FrontierCurrentSaveVersion,Work,*PetId.ToString(),*SiteId.ToString());Phase=5;
        }
        if(Phase==5 && !Site->bUnderConstruction)
        {
            // 等待态读取时首批搬运已在上一进程完成；按原220厘米接触半径推导
            // 本次剩余往返的最短距离，并额外要求补料后真实携带过三份材料。
            const double MinimumTravel=WaitingRestore?FMath::Max(0.,(FVector::Dist2D(Crate->GetActorLocation(),Site->ConstructionAnchor())-440)*2):700;
            if(Travel<MinimumTravel || (WaitAudit && !RefillCarried) || Site->PaidMaterials!=6 || Pet->Cargo->Count()!=0 || Pet->Construction->GetSite() || Pet->Activity!=ECreatureActivity::Follow || Hero->Inventory->Count(EFrontierItem::Material)!=66 ||
                !Hero->SaveProgress(TEXT("ConstructionDone")) || !Hero->LoadProgress(TEXT("ConstructionDone")) || !Rebind() || Pet->Construction->GetSite() || Pet->Cargo->Count()!=0 || Pet->Activity!=ECreatureActivity::Follow ||
                Hero->Inventory->Count(EFrontierItem::Material)!=66 || Crate->StorageInventory->Count(EFrontierItem::Material)!=0)return Finish(false,TEXT("done"));
            AHomeModule* Done=nullptr;for(TActorIterator<AHomeModule> It(World);It;++It)if(It->PersistentId==SiteId)Done=*It;
            if(!Done || Done->bUnderConstruction || Done->PaidMaterials!=6 || Done->ConstructionWork!=0 || Done->ConstructionCrateId.IsValid())return Finish(false,TEXT("done-restore"));
            if(Needs && (!Fed || !Trough.IsValid() || Trough->StoredFood!=0 || Hero->Inventory->Count(EFrontierItem::Food)!=0))return Finish(false,TEXT("food-restore"));
            UE_LOG(LogTemp,Display,TEXT("CF_CONSTRUCTION_DONE PASS version=%d material=66 cost=6 identity=1 actualNav=1 rest=1"),FrontierCurrentSaveVersion);Phase=8;
            if(Photo(TEXT("Done")))return true;return Finish(true,TEXT("complete"));
        }
        if(Phase==8)
        {
            for(const auto& Scene:Shots)if(IFileManager::Get().FileSize(*(ShotRoot/(Scene+TEXT(".png"))))<1000)return Finish(false,TEXT("screens"));
            return Finish(true,TEXT("complete"));
        }
        return true;
    }
};
}
void RunConstructionScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    const auto State=MakeShared<FConstructionTest>();State->World=World;State->Hero=Hero;State->Deadline=FPlatformTime::Seconds()+150;
    State->Needs=FParse::Param(FCommandLine::Get(),TEXT("CFConstructionNeeds"));
    State->WaitAudit=FParse::Param(FCommandLine::Get(),TEXT("CFConstructionWaitAudit"));
    State->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFConstructionPreview"));
    if(State->Preview){State->ShotRoot=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("ConstructionPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*State->ShotRoot,true);UE_LOG(LogTemp,Display,TEXT("CF_CONSTRUCTION_PREVIEW_ROOT %s"),*State->ShotRoot);}
    if(Read)
    {
        const bool Waiting=FParse::Param(FCommandLine::Get(),TEXT("CFConstructionWaitRead"));
        if(!Hero->LoadProgress(TEXT("ConstructionInput")) || !State->Rebind() || !State->Site->bUnderConstruction || State->Pet->Construction->GetSite()!=State->Site.Get() || State->Pet->Cargo->Count()!=(Waiting?0:3)){State->Finish(false,TEXT("cold-restore"));return;}
        State->Phase=Waiting?2:1;State->CancelledOnce=true;
        if(Waiting)
        {
            if(!State->WaitAudit || State->Site->StorageInventory->Count(EFrontierItem::Material)!=3 || State->Crate->StorageInventory->Count(EFrontierItem::Material)!=0){State->Finish(false,TEXT("wait-restore"));return;}
            State->At=World->GetTimeSeconds();State->WaitingStamina=State->Pet->Stamina;
            State->WaitingRestore=true;
            UE_LOG(LogTemp,Display,TEXT("CF_CONSTRUCTION_WAIT_RESTORE PASS version=%d supplied=3 carried=0 intent=1"),Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("ConstructionInput"),0))->FormatVersion);
        }
        else UE_LOG(LogTemp,Display,TEXT("CF_CONSTRUCTION_COLD PASS version=%d carried=3 intent=1"),Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("ConstructionInput"),0))->FormatVersion);
    }
    else
    {
        Hero->SetActorLocation(FVector(-350,-1200,60));Hero->Inventory->Restore({{EFrontierItem::Material,State->Needs?112:100},{EFrontierItem::Food,State->Needs?1:0}});Hero->HomeBuilding->Selected=EHomeModuleKind::Floor;
        auto* East=Hero->HomeBuilding->BuildAt(FTransform(FVector(-400,-800,-40)));auto* West=Hero->HomeBuilding->BuildAt(FTransform(FVector(-800,-800,-40)));
        if(!East || !West){State->Finish(false,TEXT("floors"));return;}
        Hero->SetActorLocation(FVector(-650,-1200,60));Hero->HomeBuilding->Selected=EHomeModuleKind::Storage;
        State->Crate=Hero->HomeBuilding->BuildAt(FTransform(FVector(-900,-900,-30)),West->PersistentId);Hero->HomeBuilding->Selected=EHomeModuleKind::Wall;
        if(!State->Crate.IsValid() || !Hero->HomeBuilding->BuildAt(FTransform(FVector(-400,-1000,-30)),East->PersistentId)){State->Finish(false,TEXT("foundation"));return;}
        State->Site=Hero->HomeBuilding->QueueAt(FTransform(FVector(-400,-600,-30)),East->PersistentId);
        if(State->Needs)
        {
            Hero->SetActorLocation(FVector(-1050,-1200,60));Hero->Building->Selected=EWorkSpecialty::Feeding;
            State->Trough=Hero->Building->BuildAt(FTransform(FRotator::ZeroRotator,FVector(-700,-1100,-15),FVector(2,1,.7)));
            if(!State->Trough.IsValid() || !State->Trough->TransferFood(Hero,true)){State->Finish(false,TEXT("trough"));return;}
        }
        Hero->SetActorLocation(FVector(-1000,-800,60));
        if(!State->Site.IsValid() || Hero->Inventory->Count(EFrontierItem::Material)!=72){State->Finish(false,TEXT("queue-budget"));return;}
        // 待建墙不是承重墙；安装位及支撑仍由原结构系统保护。
        Hero->HomeBuilding->Selected=EHomeModuleKind::Roof;FString Reason;
        if(Hero->HomeBuilding->ValidatePlacement(FTransform(FVector(-400,-800,280)),East->PersistentId,Reason) || Hero->HomeBuilding->Dismantle(East)){State->Finish(false,TEXT("support"));return;}
        if(!Hero->Inventory->TransferTo(State->Crate->StorageInventory,{{EFrontierItem::Material,3}})){State->Finish(false,TEXT("stock"));return;}
        const FTransform Start(FVector(-1100,-800,40));auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),Start,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Pet->Definition=LoadObject<UCreatureDefinition>(nullptr,TEXT("/Game/Creatures/DA_Mosskip.DA_Mosskip"));Pet->FinishSpawning(Start);Pet->Tame(Hero->GetController());
        if(!State->Rebind()){State->Finish(false,TEXT("bind"));return;}
        Hero->SetActorLocation(FVector(-1050,-950,60));State->Face(State->Crate.Get());
        if(!CFInputQA::Press(Hero,EKeys::V)){State->Finish(false,TEXT("select"));return;}State->Face(State->Site.Get());
        if(!CFInputQA::Press(Hero,EKeys::E) || Pet->Construction->GetSite()!=State->Site.Get()){State->Finish(false,TEXT("assign"));return;}
        UE_LOG(LogTemp,Display,TEXT("CF_CONSTRUCTION_QUEUE PASS material=69 paid=0 support=1 input=1 pet=%s site=%s"),*State->PetId.ToString(),*State->SiteId.ToString());
    }
    for(TActorIterator<AWildEncounter> It(World);It;++It)It->SetActorTickEnabled(false);
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(*It!=State->Pet.Get()){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();}
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float){return State->Tick();}));
}

void RunConstructionInputScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    Hero->Inventory->Restore({{EFrontierItem::Material,20}});Hero->SetActorLocation(FVector(-350,-300,60));Hero->HomeBuilding->Selected=EHomeModuleKind::Floor;
    auto* Floor=Hero->HomeBuilding->BuildAt(FTransform(FVector(-400,-800,-40)));
    if(!Floor){UE_LOG(LogTemp,Error,TEXT("CF_CONSTRUCTION_INPUT FAIL reason=floor"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    const auto Look=(FVector(-400,-610,-30)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(Look);Hero->FirstPersonCamera->SetWorldRotation(Look);
    Hero->HomeBuilding->Selected=EHomeModuleKind::Wall;
    const bool Open=CFInputQA::Press(Hero,EKeys::B) && CFInputQA::Press(Hero,EKeys::Eight);auto* PC=Cast<APlayerController>(Hero->GetController());
    if(!Open || !PC){UE_LOG(LogTemp,Error,TEXT("CF_CONSTRUCTION_INPUT FAIL reason=bindings"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    PC->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::RightShift,IE_Pressed,1.0f,false,FPlatformTime::Cycles64()));
    const bool Preview=FParse::Param(FCommandLine::Get(),TEXT("CFConstructionPreview"));
    const FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("ConstructionPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));
    IFileManager::Get().MakeDirectory(*Root,true);UE_LOG(LogTemp,Display,TEXT("CF_CONSTRUCTION_PREVIEW_ROOT %s"),*Root);
    auto Phase=MakeShared<int32>(0);auto Next=MakeShared<double>(FPlatformTime::Seconds()+.2);const auto FloorId=Floor->PersistentId;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([=](float)
    {
        if(FPlatformTime::Seconds()<*Next)return true;
        auto Fail=[=](const TCHAR* Reason){PC->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::RightShift,IE_Released,0.0f,false,FPlatformTime::Cycles64()));UE_LOG(LogTemp,Error,TEXT("CF_CONSTRUCTION_INPUT FAIL reason=%s feedback=%s"),Reason,*Hero->Feedback);FPlatformMisc::RequestExitWithStatus(false,1);return false;};
        if(*Phase==0)
        {
            Hero->HomeBuilding->UpdatePreview();auto* Ghost=Hero->HomeBuilding->GetPreview();
            if(!PC->IsInputKeyDown(EKeys::RightShift) || !Ghost || !Ghost->GetActorLocation().Equals(FVector(-400,-600,-30),.01))return Fail(TEXT("preview"));
            if(Preview)FScreenshotRequest::RequestScreenshot(Root/TEXT("Planning.png"),false,false);*Phase=1;*Next=FPlatformTime::Seconds()+.2;return true;
        }
        if(*Phase==1)
        {
            if(!CFInputQA::Action(Hero,TEXT("Attack"),EKeys::LeftMouseButton))return Fail(TEXT("confirm"));
            PC->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::RightShift,IE_Released,0.0f,false,FPlatformTime::Cycles64()));CFInputQA::Press(Hero,EKeys::B);
            AHomeModule* Site=nullptr;for(TActorIterator<AHomeModule> It(World);It;++It)if(It->bUnderConstruction)Site=*It;
            if(!Site || Site->FloorId!=FloorId || Site->PaidMaterials!=0 || Hero->Inventory->Count(EFrontierItem::Material)!=12 || HomeLayout::Collect(World).Num()!=2)return Fail(TEXT("planned"));
            const auto Aim=(Site->GetActorLocation()+FVector(0,0,12)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();PC->SetControlRotation(Aim);Hero->FirstPersonCamera->SetWorldRotation(Aim);
            if(Preview)FScreenshotRequest::RequestScreenshot(Root/TEXT("Scaffold.png"),false,false);*Phase=2;*Next=FPlatformTime::Seconds()+.3;return true;
        }
        if(Preview && (IFileManager::Get().FileSize(*(Root/TEXT("Planning.png")))<1000 || IFileManager::Get().FileSize(*(Root/TEXT("Scaffold.png")))<1000))return Fail(TEXT("images"));
        UE_LOG(LogTemp,Display,TEXT("CF_CONSTRUCTION_INPUT PASS modifier=1 mapped=1 preview=1 material=12 paid=0"));FPlatformMisc::RequestExitWithStatus(false,0);return false;
    }));
}
