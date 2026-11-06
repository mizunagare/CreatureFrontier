#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CompanionCargoComponent.h"
#include "WorkStation.h"
#include "InventoryComponent.h"
#include "FrontierSaveGame.h"
#include "BaseBuildingComponent.h"
#include "StorageInteractionComponent.h"
#include "InputRoutingQA.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

void RunCompanionCraftRules(UWorld* World,ACFPlayerCharacter* Hero)
{
    // 独立规则夹具使用明确库存，不作为真实采集、移动或时间进度证据。
    Hero->SetActorLocation(FVector(1200,-1200,40));Hero->Health=100;
    auto* Site=World->SpawnActor<AWorkStation>(FVector(1400,-1000,0),FRotator::ZeroRotator);Site->bPlayerBuilt=true;Site->RequiredSpecialty=EWorkSpecialty::Crafting;Site->CraftRecipeId=TEXT("Medicine");
    auto MakeWorker=[&](FVector Position){auto* P=World->SpawnActor<ACreatureCharacter>(Position,FRotator::ZeroRotator);P->Disposition=ECreatureDisposition::Tamed;P->Tamer=Hero->GetController();P->WorkSite=Site;P->Activity=ECreatureActivity::Work;return P;};
    auto* Pet=MakeWorker(FVector(1250,-1000,40));auto* Peer=MakeWorker(FVector(1400,-1180,40));
    Hero->Inventory->Restore({{EFrontierItem::Material,4}});
    const bool Empty=Site->CraftInventory->GetStacks().IsEmpty();
    Site->PersistentId=FGuid::NewGuid();
    // 快照规则夹具：设施尚未生成时只用已验证记录判断回运身份。
    FFacilitySaveRecord CargoTarget;CargoTarget.Id=Site->PersistentId;CargoTarget.Specialty=EWorkSpecialty::Crafting;
    FCreatureSaveRecord CargoRecord;CargoRecord.bOwned=true;CargoRecord.Disposition=ECreatureDisposition::Tamed;
    CargoRecord.Activity=ECreatureActivity::Work;CargoRecord.FieldResourceId=FGuid::NewGuid();
    CargoRecord.DeliveryWorkshopId=CargoTarget.Id;CargoRecord.CargoItems={{EFrontierItem::Fiber,3}};CargoRecord.bReturningCargo=true;
    bool CargoValid=UCompanionCargoComponent::ValidateSnapshot(CargoRecord,nullptr,&CargoTarget);
    auto BadCargo=CargoRecord;BadCargo.DeliveryCrateId=FGuid::NewGuid();CargoValid &= !UCompanionCargoComponent::ValidateSnapshot(BadCargo,nullptr,&CargoTarget);
    BadCargo=CargoRecord;BadCargo.CarriedSeeds=1;CargoValid &= !UCompanionCargoComponent::ValidateSnapshot(BadCargo,nullptr,&CargoTarget);
    BadCargo=CargoRecord;BadCargo.CarriedFood=1;CargoValid &= !UCompanionCargoComponent::ValidateSnapshot(BadCargo,nullptr,&CargoTarget);
    BadCargo=CargoRecord;BadCargo.ConstructionModuleId=FGuid::NewGuid();CargoValid &= !UCompanionCargoComponent::ValidateSnapshot(BadCargo,nullptr,&CargoTarget);
    CargoValid &= !UCompanionCargoComponent::ValidateSnapshot(CargoRecord,nullptr);
    CargoTarget.Specialty=EWorkSpecialty::Transport;CargoValid &= !UCompanionCargoComponent::ValidateSnapshot(CargoRecord,nullptr,&CargoTarget);
    CargoTarget.Specialty=EWorkSpecialty::Crafting;CargoTarget.Id=FGuid::NewGuid();CargoValid &= !UCompanionCargoComponent::ValidateSnapshot(CargoRecord,nullptr,&CargoTarget);
    UE_LOG(LogTemp,Display,TEXT("CF_WORKSHOP_CARGO_SNAPSHOT %s"),CargoValid?TEXT("PASS"):TEXT("FAIL"));
    Hero->FirstPersonCamera->SetWorldRotation((Site->GetActorLocation()+FVector(0,0,30)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());
    const bool Opened=CFInputQA::Press(Hero,EKeys::C) && Hero->IsBackpackOpen() && CFInputQA::Press(Hero,EKeys::E) && Hero->Storage->GetWorkshop()==Site && !Hero->IsBackpackOpen();
    const bool Deposited=Opened && CFInputQA::Press(Hero,EKeys::Right) && Site->CraftInventory->Count(EFrontierItem::Material)==4 && Hero->Inventory->Count(EFrontierItem::Material)==0;
    const bool Withdrawn=Deposited && CFInputQA::Press(Hero,EKeys::Left) && Site->CraftInventory->Count(EFrontierItem::Material)==0 && Hero->Inventory->Count(EFrontierItem::Material)==4;
    const bool Closed=CFInputQA::Press(Hero,EKeys::E) && !Hero->Storage->IsOpen();
    const bool StorageInput=Opened && Deposited && Withdrawn && Closed && Pet->Activity==ECreatureActivity::Work && Pet->WorkSite==Site && Peer->WorkSite==Site;
    UE_LOG(LogTemp,Display,TEXT("CF_WORKSHOP_INPUT %s open=%d deposit=%d withdraw=%d close=%d"),StorageInput?TEXT("PASS"):TEXT("FAIL"),Opened,Deposited,Withdrawn,Closed);
    // TransferTo保留零数量键；此处清理仅隔离后续规则夹具。
    Site->CraftInventory->Restore({});
    FFacilitySaveRecord Stock;Stock.Specialty=EWorkSpecialty::Crafting;
    bool StockValid=AWorkStation::ValidateCraft(Stock);Stock.CraftStockRevision=1;Stock.CraftStock={{EFrontierItem::Seeds,3}};StockValid &= AWorkStation::ValidateCraft(Stock);
    auto Bad=Stock;Bad.CraftStockRevision=0;StockValid &= !AWorkStation::ValidateCraft(Bad);
    Bad=Stock;Bad.CraftStockRevision=2;StockValid &= !AWorkStation::ValidateCraft(Bad);
    Bad=Stock;Bad.Specialty=EWorkSpecialty::Transport;StockValid &= !AWorkStation::ValidateCraft(Bad);
    Bad=Stock;Bad.CraftStock[EFrontierItem::Seeds]=-1;StockValid &= !AWorkStation::ValidateCraft(Bad);
    Bad=Stock;Bad.CraftStock[EFrontierItem::Seeds]=1000;StockValid &= !AWorkStation::ValidateCraft(Bad);
    Bad=Stock;Bad.CraftStock.Add(EFrontierItem::Count,1);StockValid &= !AWorkStation::ValidateCraft(Bad);
    const bool Protected=Site->CraftInventory->AddItem(EFrontierItem::Seeds,3) && !Hero->Building->DismantleStation(Site) && !Site->IsActorBeingDestroyed() && Site->CraftInventory->Count(EFrontierItem::Seeds)==3 && Hero->Inventory->Count(EFrontierItem::Material)==4;
    Site->CraftInventory->Restore({});
    UE_LOG(LogTemp,Display,TEXT("CF_WORKSHOP_STOCK_RULES %s validation=%d empty=%d dismantleProtected=%d"),StockValid&&Empty&&Protected?TEXT("PASS"):TEXT("FAIL"),StockValid,Empty,Protected);
    const bool Missing=!Site->WorkCraft(Pet,Hero,8) && Site->CraftWork==0 && Hero->Inventory->Count(EFrontierItem::Material)==4 && Pet->Experience==0;
    Hero->Inventory->Restore({{EFrontierItem::Material,5},{EFrontierItem::Medicine,999}});
    const bool Full=!Site->WorkCraft(Pet,Hero,8) && Site->CraftWork==0 && Hero->Inventory->Count(EFrontierItem::Medicine)==999 && Hero->Inventory->Count(EFrontierItem::Material)==5;
    Hero->Inventory->Restore({{EFrontierItem::Material,5}});
    const bool Partial=Site->WorkCraft(Pet,Hero,2) && Site->CraftWork==2 && Hero->Inventory->Count(EFrontierItem::Material)==5 && Hero->Inventory->Count(EFrontierItem::Medicine)==0;
    UGameplayStatics::SetGamePaused(World,true);const bool Paused=!Site->WorkCraft(Pet,Hero,1) && Site->CraftWork==2;UGameplayStatics::SetGamePaused(World,false);
    Pet->SetActivity(ECreatureActivity::Rest);const bool Rest=!Site->WorkCraft(Pet,Hero,1) && Site->CraftWork==2;Pet->SetActivity(ECreatureActivity::Work);
    const bool Exclusive=!Site->WorkCraft(Peer,Hero,2) && Site->CraftWork==2 && Peer->Experience==0;
    const bool Finished=Site->WorkCraft(Pet,Hero,6) && Site->CraftWork==0 && Site->CraftRecipeId.IsNone() && Hero->Inventory->Count(EFrontierItem::Material)==0 && Hero->Inventory->Count(EFrontierItem::Medicine)==1 && Pet->Experience==1 && Pet->Activity==ECreatureActivity::Rest;
    const bool Once=!Site->WorkCraft(Pet,Hero,8) && Hero->Inventory->Count(EFrontierItem::Medicine)==1 && Pet->Experience==1;
    // 新委托仅使用设施物资；即使主人背包有原料也不能越库扣取。
    Hero->Inventory->Restore({{EFrontierItem::Material,5},{EFrontierItem::Medicine,999}});
    const auto OldBag=Hero->Inventory->GetStacks();
    Hero->ToggleBackpack();
    const bool LocalOrder=Site->ToggleCraftOrder(Hero,TEXT("Medicine")) && Site->bCraftUsesLocalStock;
    Pet->SetActivity(ECreatureActivity::Work);
    const bool LocalMissing=!Site->WorkCraft(Pet,Hero,2) && Site->CraftWork==0;
    Site->CraftInventory->Restore({{EFrontierItem::Material,5},{EFrontierItem::Medicine,999}});
    const bool LocalFull=!Site->WorkCraft(Pet,Hero,2) && Site->CraftWork==0;
    Site->CraftInventory->Restore({{EFrontierItem::Material,5}});
    const bool LocalPartial=Site->WorkCraft(Pet,Hero,2) && Site->CraftWork==2 && Site->CraftInventory->Count(EFrontierItem::Material)==5;
    const bool LocalCancel=Site->ToggleCraftOrder(Hero,TEXT("Medicine")) && !Site->bCraftUsesLocalStock && Site->CraftRecipeId.IsNone() && Site->CraftWork==0 && Site->CraftInventory->Count(EFrontierItem::Material)==5;
    const bool LocalReorder=Site->ToggleCraftOrder(Hero,TEXT("Medicine"));Pet->SetActivity(ECreatureActivity::Work);
    const bool LocalFinish=Site->WorkCraft(Pet,Hero,8) && Site->CraftInventory->Count(EFrontierItem::Material)==0 && Site->CraftInventory->Count(EFrontierItem::Medicine)==1 && Pet->Experience==2 && Site->CraftRecipeId.IsNone() && !Site->bCraftUsesLocalStock && !Site->WorkCraft(Pet,Hero,8) && Hero->Inventory->GetStacks().OrderIndependentCompareEqual(OldBag);
    Hero->ToggleBackpack();
    Stock.CraftRecipe=TEXT("Medicine");Stock.CraftRevision=2;Stock.bCraftUsesLocalStock=true;
    bool ModeValid=AWorkStation::ValidateCraft(Stock);
    Bad=Stock;Bad.CraftRevision=1;ModeValid &= !AWorkStation::ValidateCraft(Bad);
    Bad=Stock;Bad.bCraftUsesLocalStock=false;ModeValid &= !AWorkStation::ValidateCraft(Bad);
    Bad=Stock;Bad.CraftRecipe=NAME_None;ModeValid &= !AWorkStation::ValidateCraft(Bad);
    Bad=Stock;Bad.CraftStockRevision=0;ModeValid &= !AWorkStation::ValidateCraft(Bad);
    Bad=Stock;Bad.CraftRevision=1;Bad.bCraftUsesLocalStock=false;Bad.CraftStockRevision=0;Bad.CraftStock.Empty();ModeValid &= AWorkStation::ValidateCraft(Bad);
    const bool Local=LocalOrder && LocalMissing && LocalFull && LocalPartial && LocalCancel && LocalReorder && LocalFinish && ModeValid;
    UE_LOG(LogTemp,Display,TEXT("CF_WORKSHOP_LOCAL %s order=%d missing=%d full=%d partial=%d cancel=%d reorder=%d finish=%d mode=%d"),Local?TEXT("PASS"):TEXT("FAIL"),LocalOrder,LocalMissing,LocalFull,LocalPartial,LocalCancel,LocalReorder,LocalFinish,ModeValid);
    const bool Pass=Local && StorageInput && StockValid && Empty && Protected && Missing && Full && Partial && Paused && Rest && Exclusive && Finished && Once;
    UE_LOG(LogTemp,Display,TEXT("CF_COMPANION_CRAFT_RULES %s missing=%d full=%d partial=%d paused=%d rest=%d exclusive=%d finished=%d once=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Missing,Full,Partial,Paused,Rest,Exclusive,Finished,Once);
    if(!Pass || !FParse::Param(FCommandLine::Get(),TEXT("CFWorkshopPreview"))){FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return;}
    // 规则夹具的图形快照，不作为正常物资来源或伙伴生产耗时证据。
    Pet->SetActivity(ECreatureActivity::Rest);Peer->SetActivity(ECreatureActivity::Rest);
    Hero->GetController()->SetControlRotation((Site->GetActorLocation()+FVector(0,0,30)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());
    Hero->ToggleBackpack();
    if(!Site->ToggleCraftOrder(Hero,TEXT("Medicine")) || !CFInputQA::Press(Hero,EKeys::E) || Hero->Storage->GetWorkshop()!=Site){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    const FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("WorkshopPreview")/FGuid::NewGuid().ToString());
    IFileManager::Get().MakeDirectory(*Root,true);
    UE_LOG(LogTemp,Display,TEXT("CF_WORKSHOP_PREVIEW_ROOT %s"),*Root);
    FTimerHandle StockShot,ShowOrder,OrderShot,ShowStatus,StatusShot,Exit;
    World->GetTimerManager().SetTimer(StockShot,[Root](){FScreenshotRequest::RequestScreenshot(Root/TEXT("Stock.png"),false,false);},1.f,false);
    World->GetTimerManager().SetTimer(ShowOrder,[Hero](){CFInputQA::Press(Hero,EKeys::E);CFInputQA::Press(Hero,EKeys::C);},2.f,false);
    World->GetTimerManager().SetTimer(OrderShot,[Root](){FScreenshotRequest::RequestScreenshot(Root/TEXT("Order.png"),false,false);},3.f,false);
    World->GetTimerManager().SetTimer(ShowStatus,[Hero](){CFInputQA::Press(Hero,EKeys::C);},4.f,false);
    World->GetTimerManager().SetTimer(StatusShot,[Root](){FScreenshotRequest::RequestScreenshot(Root/TEXT("Status.png"),false,false);},5.f,false);
    World->GetTimerManager().SetTimer(Exit,[Root,Hero,Site](){const bool Ready=!Hero->IsBackpackOpen() && !Hero->Storage->IsOpen() && Hero->TraceFacility()==Site && !Hero->TraceCreature() && !Hero->TraceResource() && Site->bCraftUsesLocalStock && Site->CraftWork==0 && IFileManager::Get().FileSize(*(Root/TEXT("Stock.png")))>1000 && IFileManager::Get().FileSize(*(Root/TEXT("Order.png")))>1000 && IFileManager::Get().FileSize(*(Root/TEXT("Status.png")))>1000;
        UE_LOG(LogTemp,Display,TEXT("CF_WORKSHOP_PREVIEW %s"),Ready?TEXT("PASS"):TEXT("FAIL"));FPlatformMisc::RequestExitWithStatus(false,Ready?0:1);},7.f,false);
}
