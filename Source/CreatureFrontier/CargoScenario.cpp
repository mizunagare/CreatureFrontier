#include "CargoScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "CompanionCargoComponent.h"
#include "InventoryComponent.h"
#include "HomeBuildingComponent.h"
#include "HomeModule.h"
#include "HomeLayout.h"
#include "ResourceNode.h"
#include "FrontierSaveGame.h"
#include "ResearchComponent.h"
#include "WildEncounter.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"

namespace CFCargoQA
{
ACreatureCharacter* Worker(UWorld* World,ACFPlayerCharacter* Hero)
{ for(TActorIterator<ACreatureCharacter> It(World);It;++It) if(It->Tamer==Hero->GetController()) return *It; return nullptr; }
AHomeModule* Box(UWorld* World)
{ for(TActorIterator<AHomeModule> It(World);It;++It) if(!It->bPreview && !It->bStaged && It->GetKind()==EHomeModuleKind::Storage) return *It; return nullptr; }
AResourceNode* Source(UWorld* World,bool Crystal=false,bool Ore=false)
{ for(TActorIterator<AResourceNode> It(World);It;++It) if(It->PersistentId==FGuid(0xCF170000,0,0,Ore?9:Crystal?7:1)) return *It; return nullptr; }
bool Mid(UWorld* World,ACFPlayerCharacter* Hero,bool Crystal=false,bool Ore=false)
{
    auto* Pet=Worker(World,Hero); auto* Crate=Box(World); auto* Node=Source(World,Crystal,Ore);
    const auto Item=Ore?EFrontierItem::Ore:Crystal?EFrontierItem::Crystal:EFrontierItem::Material;
    return Pet && Crate && Node && Pet->PersistentId.IsValid() && Pet->Health>0 && Pet->Cargo->Count()==3 && Pet->CargoInventory->Count(Item)==3 &&
        Pet->Cargo->HasOrder() && Pet->Cargo->IsReturning() && Pet->Cargo->GetTarget()==Crate && Pet->GetFieldResource()==Node &&
        Pet->Experience==3 && Node->Remaining==1 && Crate->StorageInventory->Count(Item)==0 && Hero->Inventory->Count(Item)==999 &&
        (!Crystal || Hero->Research->Has(EFrontierResearch::CrystalSample));
}
void Shot(UWorld* World,ACFPlayerCharacter* Hero,ACreatureCharacter* Pet,const TCHAR* Name)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("CFCargoPreview"))) return;
    auto* Camera=World->SpawnActor<ACameraActor>();
    Camera->SetActorLocation(Pet->GetActorLocation()+FVector(-380,-280,220)); Camera->GetCameraComponent()->FieldOfView=70;
    Camera->SetActorRotation((Pet->GetActorLocation()-Camera->GetActorLocation()).Rotation());
    if(auto* PC=Cast<APlayerController>(Hero->GetController())) PC->SetViewTarget(Camera);
    Hero->Feedback=Pet->Cargo->Status();
    const FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("CargoPreview")); IFileManager::Get().MakeDirectory(*Root,true);
    FScreenshotRequest::RequestScreenshot(Root/(FString(Name)+TEXT(".png")),false,false);
}
bool Faults(UWorld* World,ACFPlayerCharacter* Hero)
{
    const FString Slot=TEXT("CargoMid"); bool Pass=Hero->LoadProgress(Slot) && Mid(World,Hero);
    if(!Pass) return false;
    auto Check=[&](const TCHAR* Stage)
    { UE_LOG(LogTemp,Display,TEXT("CF_CARGO_RECOVERY %s=%d feedback=%s"),Stage,Pass,*Hero->Feedback); return Pass; };
    for(int32 Case=0;Case<14;++Case)
    {
        auto* Live=Worker(World,Hero); auto* Bad=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0));
        if(!Bad || Bad->Creatures.Num()!=1) return false;
        auto& R=Bad->Creatures[0];
        if(Case==0) R.CargoItems.Add(EFrontierItem::Material,4);
        if(Case==1) R.CargoItems.Add(EFrontierItem::Count,1);
        if(Case==2) R.CarriedFood=1;
        if(Case==3) R.DeliveryCrateId=FGuid::NewGuid();
        if(Case==4) for(const auto& Home:Bad->HomeModules) if(Home.Definition.LoadSynchronous()->Kind==EHomeModuleKind::Floor) R.DeliveryCrateId=Home.Id;
        if(Case==5) R.CargoItems.Reset();
        if(Case==6) R.DeliveryCrateId.Invalidate();
        if(Case==7) R.Activity=ECreatureActivity::Follow;
        if(Case==8) { R.bOwned=false; R.Disposition=ECreatureDisposition::Wild; R.FieldResourceId.Invalidate(); R.DeliveryCrateId.Invalidate(); R.bReturningCargo=false; }
        if(Case==9) R.CargoItems.Add(EFrontierItem::Material,-1);
        if(Case==10) R.CargoItems={{EFrontierItem::Food,3}};
        if(Case==11) { FCreatureSaveRecord Broken=R; Broken.Id=FGuid::NewGuid(); Broken.Health=-1; Broken.FieldResourceId.Invalidate(); Broken.DeliveryCrateId.Invalidate(); Broken.bReturningCargo=false; Bad->Creatures.Add(Broken); }
        if(Case==12) { const auto Duplicate=R; Bad->Creatures.Add(Duplicate); }
        if(Case==13) R.Id.Invalidate();
        Pass &= UGameplayStatics::SaveGameToSlot(Bad,TEXT("CargoBad"),0) && !Hero->LoadProgress(TEXT("CargoBad")) && IsValid(Live) && Worker(World,Hero)==Live && Mid(World,Hero);
        if(!Pass) { UE_LOG(LogTemp,Error,TEXT("CF_CARGO_BAD case=%d feedback=%s"),Case,*Hero->Feedback); return false; }
    }
    auto* Pet=Worker(World,Hero);
    Hero->CommandFollow();
    Pass &= Pet && Pet->Cargo->Count()==3 && !Pet->Cargo->HasOrder() && !Pet->GetFieldResource() && Pet->Experience==3;
    Pass &= Hero->SaveProgress(TEXT("CargoCancelled")) && Hero->LoadProgress(TEXT("CargoCancelled")); Pet=Worker(World,Hero);
    Pass &= Pet && Pet->Cargo->Count()==3 && !Pet->Cargo->HasOrder() && Pet->Activity==ECreatureActivity::Follow;
    if(!Pet) return false;
    Hero->SetActorLocation(Pet->GetActorLocation()+FVector(-260,0,0));
    Pass &= !Hero->WithdrawCompanionCargo(Pet) && Pet->Cargo->Count()==3 && Hero->Inventory->Count(EFrontierItem::Material)==999;
    Hero->Inventory->Consume(EFrontierItem::Material,3);
    Pass &= Hero->WithdrawCompanionCargo(Pet) && Pet->Cargo->Count()==0 && Hero->Inventory->Count(EFrontierItem::Material)==999 && Pet->Experience==3;
    if(!Check(TEXT("cancel_recover"))) return false;
    Pass &= Hero->LoadProgress(Slot) && Mid(World,Hero); Pet=Worker(World,Hero);
    if(!Pet) return false;
    Pet->Stamina=0; Pet->Tick(.1f);
    Pass &= Pet->Activity==ECreatureActivity::Rest && Pet->bResumeWorkAfterRest && Pet->Cargo->Count()==3 && Pet->Cargo->HasOrder();
    Pass &= Hero->SaveProgress(TEXT("CargoRest")) && Hero->LoadProgress(TEXT("CargoRest")); Pet=Worker(World,Hero);
    Pass &= Pet && Pet->Activity==ECreatureActivity::Rest && Pet->bResumeWorkAfterRest && Pet->Cargo->IsReturning() && Pet->Cargo->Count()==3;
    if(!Pet) return false;
    Pet->Stamina=80; Pet->Nutrition=100; Pet->Tick(.1f);
    Pass &= Pet->Activity==ECreatureActivity::Work && Pet->Cargo->Count()==3 && Pet->Cargo->HasOrder();
    if(!Check(TEXT("rest_resume"))) return false;
    Pass &= Hero->LoadProgress(Slot) && Mid(World,Hero); Pet=Worker(World,Hero);
    UGameplayStatics::ApplyDamage(Pet,1000,nullptr,Hero,nullptr);
    Pass &= Pet->Health==0 && Pet->Cargo->Count()==3 && !Pet->Cargo->HasOrder() && !Pet->GetFieldResource();
    Pass &= Hero->SaveProgress(TEXT("CargoDowned")) && Hero->LoadProgress(TEXT("CargoDowned")); Pet=Worker(World,Hero);
    Pass &= Pet && Pet->Health==0 && Pet->Cargo->Count()==3 && !Pet->Cargo->HasOrder();
    if(!Pet) return false;
    Hero->SetActorLocation(Pet->GetActorLocation()+FVector(-260,0,0)); Hero->Inventory->Consume(EFrontierItem::Material,3);
    Pass &= Hero->WithdrawCompanionCargo(Pet) && Pet->Cargo->Count()==0 && Pet->Experience==3;
    if(!Check(TEXT("downed_recover"))) return false;
    Pass &= Hero->LoadProgress(Slot) && Mid(World,Hero);
    auto* Crate=Box(World); Hero->SetActorLocation(FVector(-650,-1000,40)); Hero->Inventory->Consume(EFrontierItem::Material,3);
    Pass &= Crate && Hero->HomeBuilding->Dismantle(Crate); // A valid empty-box removal, with real refund capacity.
    Pass &= Hero->SaveProgress(TEXT("CargoOrphan")) && Hero->LoadProgress(TEXT("CargoOrphan")); Pet=Worker(World,Hero);
    Pass &= !Box(World) && Pet && Pet->Cargo->Count()==3 && !Pet->Cargo->HasOrder() && !Pet->GetFieldResource() && Pet->Activity==ECreatureActivity::Follow && Hero->Inventory->Count(EFrontierItem::Material)==999;
    if(!Check(TEXT("crate_removed"))) return false;
    Pass &= Hero->LoadProgress(Slot) && Mid(World,Hero);
    auto* Node=Source(World); Node->Destroy(); // 外部故障注入：残留的一份资源不能计为已采集。
    Pass &= Hero->SaveProgress(TEXT("CargoSourceGone")) && Hero->LoadProgress(TEXT("CargoSourceGone")); Pet=Worker(World,Hero);
    Pass &= !Source(World) && Pet && Pet->Cargo->Count()==3 && !Pet->Cargo->HasOrder() && !Pet->GetFieldResource();
    if(!Check(TEXT("source_removed"))) return false;
    Pass &= Hero->LoadProgress(Slot) && Mid(World,Hero);
    AWildEncounter* Grove=nullptr;
    for(TActorIterator<AWildEncounter> It(World);It;++It) if(It->EncounterId==TEXT("MeadowGrove")) Grove=*It;
    if(!Grove) return false;
    Grove->Tick(0); auto* Resident=Grove->GetResident();
    if(!Resident) return false;
    Resident->Tame(Hero->GetController()); Resident->CargoInventory->Restore({{EFrontierItem::Material,1}}); // 独立的货物保留夹具，不是采集产出。
    const FGuid ResidentId=Resident->PersistentId;
    UGameplayStatics::ApplyDamage(Resident,1000,nullptr,Hero,nullptr); Grove->Tick(60);
    Pass &= IsValid(Resident) && !Resident->IsActorBeingDestroyed() && Resident->Disposition==ECreatureDisposition::Tamed && Resident->Health==0 && Resident->Cargo->Count()==1 && Grove->GetResident()!=Resident && Grove->GetResident()!=nullptr;
    Pass &= Resident->PersistentId==ResidentId && Grove->GetResident() && Grove->GetResident()->PersistentId!=ResidentId;
    if(!Check(TEXT("owned_camp_retention"))) return false;
    auto* Legacy=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0)); Legacy->FormatVersion=21;
    Pass &= UGameplayStatics::SaveGameToSlot(Legacy,TEXT("CargoLegacy21"),0) && Hero->LoadProgress(TEXT("CargoLegacy21")); Pet=Worker(World,Hero);
    Pass &= Pet && Pet->Cargo->Count()==0 && !Pet->Cargo->HasOrder() && Pet->GetFieldResource() && Source(World)->Remaining==1 && Hero->Inventory->Count(EFrontierItem::Material)==999;
    if(!Pet) return false;
    const FGuid MigratedId=Pet->PersistentId;
    Pass &= MigratedId.IsValid() && Hero->SaveProgress(TEXT("CargoMigrated22")) && Hero->LoadProgress(TEXT("CargoMigrated22")) && Worker(World,Hero) && Worker(World,Hero)->PersistentId==MigratedId &&
        Hero->LoadProgress(TEXT("CargoMigrated22")) && Worker(World,Hero) && Worker(World,Hero)->PersistentId==MigratedId && Worker(World,Hero)->Cargo->Count()==0;
    if(!Check(TEXT("legacy21"))) return false;
    Pass &= Hero->LoadProgress(Slot) && Mid(World,Hero);
    return Pass;
}
struct FRun : TSharedFromThis<FRun>
{
    UWorld* World=nullptr; ACFPlayerCharacter* Hero=nullptr; int32 Mode=0,Phase=0; double Started=0,PhaseAt=0;
    FVector CarryStart=FVector::ZeroVector,PausePosition=FVector::ZeroVector,PartialStart=FVector::ZeroVector; FTimerHandle Timer;
    bool Loaded=true,Paused=false,Delivered=false; FGuid OwnedId;
    void Finish(bool Passed,const TCHAR* Reason)
    {
        UE_LOG(LogTemp,Display,TEXT("CF_CARGO%s %s phase=%d loaded=%d paused=%d delivered=%d reason=%s"),Mode==1?TEXT("_WRITE"):Mode==2?TEXT("_READ"):Mode==3?TEXT("_CRYSTAL"):Mode==4?TEXT("_ORE"):TEXT(""),Passed?TEXT("PASS"):TEXT("FAIL"),Phase,Loaded,Paused,Delivered,Reason);
        if(!Passed || !FParse::Param(FCommandLine::Get(),TEXT("CFCargoPreview"))) FPlatformMisc::RequestExitWithStatus(false,Passed?0:1);
        else
        {
            FTimerHandle Exit;
            World->GetTimerManager().SetTimer(Exit,[World=World]()
            {
                const FString Root=FPaths::ProjectSavedDir()/TEXT("CargoPreview");
                const bool Ready=IFileManager::Get().FileSize(*(Root/TEXT("Carrying.png")))>1000 && IFileManager::Get().FileSize(*(Root/TEXT("Delivered.png")))>1000;
                UE_LOG(LogTemp,Display,TEXT("CF_CARGO_PREVIEW %s"),Ready?TEXT("PASS"):TEXT("FAIL")); FPlatformMisc::RequestExitWithStatus(false,Ready?0:1);
            },3.f,false);
        }
        FTimerHandle Handle=Timer; World->GetTimerManager().ClearTimer(Handle);
    }
    void Tick()
    {
        const bool Crystal=Mode==3,Ore=Mode==4; const auto Item=Ore?EFrontierItem::Ore:Crystal?EFrontierItem::Crystal:EFrontierItem::Material;
        const FString MidSlot=Ore?TEXT("CargoOreMid"):Crystal?TEXT("CargoCrystalMid"):TEXT("CargoMid");
        auto* Pet=Worker(World,Hero); auto* Crate=Box(World); auto* Node=Source(World,Crystal,Ore);
        const double Now=World->GetTimeSeconds();
        if(!Pet || !Crate || !Node || Now-Started>45) { Finish(false,TEXT("missing actor or timeout")); return; }
        const int32 Stock=Crate->StorageInventory->Count(Item),Count=Pet->Cargo->Count();
        if(Phase==0)
        {
            if(Node->Remaining+Count+Stock!=4 || Hero->Inventory->Count(Item)!=999 || Pet->Experience!=4-Node->Remaining)
            { Finish(false,TEXT("pickup conservation")); return; }
            if(Count<3 || !Pet->Cargo->IsReturning()) return;
            CarryStart=Pet->GetActorLocation();
            OwnedId=Pet->PersistentId;
            Loaded &= Mid(World,Hero,Crystal,Ore) && Hero->SaveProgress(MidSlot);
            if(Mode==1) { Loaded &= Hero->SaveProgress(TEXT("CargoCross")); Finish(Loaded,TEXT("cross checkpoint")); return; }
            Loaded &= Hero->LoadProgress(MidSlot) && Mid(World,Hero,Crystal,Ore) && Hero->LoadProgress(MidSlot) && Mid(World,Hero,Crystal,Ore);
            Loaded &= Worker(World,Hero) && Worker(World,Hero)->PersistentId==OwnedId;
            if(!Loaded) { Finish(false,TEXT("midtrip restore")); return; }
            Pet=Worker(World,Hero); Crate=Box(World); Shot(World,Hero,Pet,TEXT("Carrying"));
            Crate->StorageInventory->Restore({{Item,999}}); Phase=1; PhaseAt=Now; return;
        }
        if(Phase==1)
        {
            if(Node->Remaining!=1 || Count!=3 || Stock!=999 || Pet->Experience!=3 || Hero->Inventory->Count(Item)!=999)
            { Finish(false,TEXT("blocked/full batch changed")); return; }
            if(!Pet->Cargo->Status().Contains(TEXT("箱子已满"))) return;
            if(Pet->GetDistanceTo(Crate)>220 || FVector::Dist2D(CarryStart,Pet->GetActorLocation())<400)
            { Finish(false,TEXT("no real delivery approach")); return; }
            UGameplayStatics::ApplyDamage(Hero,500,nullptr,Pet,nullptr); PausePosition=Pet->GetActorLocation(); Phase=2; PhaseAt=Now; return;
        }
        if(Phase==2)
        {
            if(Now-PhaseAt<1) return;
            Paused=Hero->Health==0 && Node->Remaining==1 && Count==3 && Stock==999 && Pet->Experience==3 && FVector::Dist2D(PausePosition,Pet->GetActorLocation())<1;
            if(!Paused || !Hero->RespawnAtBase()) { Finish(false,TEXT("owner down freeze")); return; }
            Crate->StorageInventory->Consume(Item,3); Phase=3; PhaseAt=Now; return;
        }
        if(Phase==3)
        {
            if(Count>0) return;
            Delivered=Stock==999 && Node->Remaining==1 && Pet->Experience==3 && Hero->Inventory->Count(Item)==999;
            UE_LOG(LogTemp,Display,TEXT("CF_CARGO_TRIP moved=%.1f cargo=%d stock=%d source=%d xp=%d"),FVector::Dist2D(CarryStart,Pet->GetActorLocation()),Count,Stock,Node->Remaining,Pet->Experience);
            Shot(World,Hero,Pet,TEXT("Delivered"));
            Phase=4; PhaseAt=Now; return;
        }
        if(Phase==4)
        {
            if(Now-PhaseAt<.3) return; // 快照回退故障检查前，先输出独立的交付完成帧。
            if(Mode==2 || Mode==3 || Mode==4) { Finish(Loaded && Paused && Delivered,TEXT("restored batch checks")); return; }
            Phase=5; return; // 正常情况必须实际返程并采集最后不足一批的资源。
        }
        if(Phase==5)
        {
            if(Stock!=999 || Hero->Inventory->Count(Item)!=999) { Finish(false,TEXT("second pickup conservation")); return; }
            if(Node->Remaining!=0 || Count!=1 || !Pet->Cargo->IsReturning()) return;
            if(Pet->Experience!=4) { Finish(false,TEXT("partial pickup XP")); return; }
            PartialStart=Pet->GetActorLocation(); Crate->StorageInventory->Consume(Item,1); Phase=6; return;
        }
        if(Phase==6)
        {
            if(Count>0) return;
            const bool Partial=Stock==999 && Node->Remaining==0 && Pet->Experience==4 && Hero->Inventory->Count(Item)==999 && FVector::Dist2D(PartialStart,Pet->GetActorLocation())>400;
            UE_LOG(LogTemp,Display,TEXT("CF_CARGO_PARTIAL moved=%.1f cargo=%d stock=%d source=%d xp=%d valid=%d"),FVector::Dist2D(PartialStart,Pet->GetActorLocation()),Count,Stock,Node->Remaining,Pet->Experience,Partial);
            const bool Checks=Partial && Faults(World,Hero);
            Finish(Loaded && Paused && Delivered && Checks,TEXT("batch and recovery checks")); return;
        }
    }
};
}
void RunCargoScenario(UWorld* World,ACFPlayerCharacter* Hero,int32 Mode)
{
    using namespace CFCargoQA;
    UCreatureDefinition* Leaf=nullptr;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It) { if(It->Definition && It->Definition->Form==(Mode>=3?ECreatureForm::Stone:ECreatureForm::Leaf)) Leaf=It->Definition; It->Destroy(); }
    auto Run=MakeShared<FRun>(); Run->World=World; Run->Hero=Hero; Run->Mode=Mode; Run->Started=World->GetTimeSeconds();
    Hero->GetCharacterMovement()->DisableMovement();
    if(Mode==2)
    {
        Run->Loaded=Hero->LoadProgress(TEXT("CargoCross")) && Mid(World,Hero);
        auto* Saved=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("CargoCross"),0));
        Run->Loaded &= Saved && Saved->Creatures.Num()==1 && Worker(World,Hero) && Worker(World,Hero)->PersistentId==Saved->Creatures[0].Id;
        if(!Run->Loaded) { Run->Finish(false,TEXT("cross restore")); return; }
        Run->CarryStart=Worker(World,Hero)->GetActorLocation(); Shot(World,Hero,Worker(World,Hero),TEXT("Carrying"));
        Box(World)->StorageInventory->Restore({{EFrontierItem::Material,999}}); Run->Phase=1;
    }
    else
    {
        Hero->SetActorLocation(FVector(-650,-1050,40)); Hero->Inventory->Restore({{EFrontierItem::Material,100}});
        Hero->HomeBuilding->Selected=EHomeModuleKind::Floor;
        auto* Floor=Hero->HomeBuilding->BuildAt(FTransform(FVector(-400,-800,-40)));
        Hero->HomeBuilding->Selected=EHomeModuleKind::Storage;
        auto* Crate=Floor?Hero->HomeBuilding->BuildAt(FTransform(FVector(-400,-800,-30)),Floor->PersistentId):nullptr;
        auto* Node=Source(World,Mode==3,Mode==4);
        if(!Crate || !Node || !Leaf) { Run->Finish(false,TEXT("fixture")); return; }
        Node->Remaining=4; Node->HarvestCooldown=0; Node->RegrowRemaining=0;
        const FTransform Spawn(Node->GetActorLocation()+FVector(0,-210,40));
        auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),Spawn,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Pet->Definition=Leaf; Pet->FinishSpawning(Spawn); Pet->Tame(Hero->GetController());
        Hero->FirstPersonCamera->SetWorldRotation((Crate->GetActorLocation()+FVector(0,0,40)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation()); Hero->CycleBuilding();
        Hero->SetActorLocation(Node->GetActorLocation()+FVector(-250,-290,40));
        Hero->FirstPersonCamera->SetWorldRotation((Node->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation()); Hero->CommandWork();
        Hero->Inventory->Restore({{Mode==4?EFrontierItem::Ore:Mode==3?EFrontierItem::Crystal:EFrontierItem::Material,999}});
        if(!Hero->HasDeliverySelection() || !Pet->Cargo->HasOrder() || Pet->Cargo->GetTarget()!=Crate || Pet->GetFieldResource()!=Node)
        { Run->Finish(false,TEXT("actual V/E order")); return; }
    }
    World->GetTimerManager().SetTimer(Run->Timer,[Run](){Run->Tick();},.1f,true);
}
