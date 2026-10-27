#include "StorageScenario.h"
#include "CFPlayerCharacter.h"
#include "StorageInteractionComponent.h"
#include "HomeBuildingComponent.h"
#include "HomeModule.h"
#include "HomeLayout.h"
#include "InventoryComponent.h"
#include "FrontierSaveGame.h"
#include "CreatureCharacter.h"
#include "ResearchComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "UnrealClient.h"

namespace CFStorageQA
{
AHomeModule* Find(UWorld* World,EHomeModuleKind Kind)
{
    for(TActorIterator<AHomeModule> It(World);It;++It) if(!It->bPreview && !It->bStaged && It->GetKind()==Kind) return *It;
    return nullptr;
}
TMap<EFrontierItem,int32> Pack(int32 ItemCount=static_cast<int32>(EFrontierItem::Count))
{
    TMap<EFrontierItem,int32> Result;
    for(int32 Index=0;Index<ItemCount;++Index)
        Result.Add(static_cast<EFrontierItem>(Index),(Index==0?86:10)-(Index+1));
    return Result;
}
TMap<EFrontierItem,int32> Stock(int32 ItemCount=static_cast<int32>(EFrontierItem::Count))
{
    TMap<EFrontierItem,int32> Result;
    for(int32 Index=0;Index<ItemCount;++Index) Result.Add(static_cast<EFrontierItem>(Index),Index+1);
    return Result;
}
bool Snapshot(UWorld* World,ACFPlayerCharacter* Hero,int32 ItemCount=static_cast<int32>(EFrontierItem::Count))
{
    auto* Crate=Find(World,EHomeModuleKind::Storage); auto* Floor=Find(World,EHomeModuleKind::Floor);
    return Crate && Floor && Crate->FloorId==Floor->PersistentId && Crate->PersistentId.IsValid() &&
        HomeLayout::Collect(World).Num()==2 && Crate->PaidMaterials==6 &&
        Crate->StorageInventory->GetStacks().OrderIndependentCompareEqual(Stock(ItemCount)) && Hero->Inventory->GetStacks().OrderIndependentCompareEqual(Pack(ItemCount)) &&
        !Hero->Research->Has(EFrontierResearch::CrystalSample);
}
void Finish(UWorld* World,ACFPlayerCharacter* Hero,bool Pass,int32 Mode)
{
    UE_LOG(LogTemp,Display,TEXT("CF_STORAGE%s %s"),Mode==1?TEXT("_WRITE"):Mode==2?TEXT("_READ"):TEXT(""),Pass?TEXT("PASS"):TEXT("FAIL"));
    if(!Pass || !FParse::Param(FCommandLine::Get(),TEXT("CFStoragePreview"))) { FPlatformMisc::RequestExitWithStatus(false,Pass?0:1); return; }
    Hero->GetCharacterMovement()->DisableMovement(); Hero->Storage->Open(Find(World,EHomeModuleKind::Storage));
    if(auto* Crate=Find(World,EHomeModuleKind::Storage)) Hero->GetController()->SetControlRotation((Crate->GetActorLocation()+FVector(0,0,40)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());
    const FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("StoragePreview")); IFileManager::Get().MakeDirectory(*Root,true);
    FTimerHandle Shot,Exit;
    World->GetTimerManager().SetTimer(Shot,[Root](){FScreenshotRequest::RequestScreenshot(Root/TEXT("Storage.png"),false,false);},1.f,false);
    World->GetTimerManager().SetTimer(Exit,[Root](){const bool Ready=IFileManager::Get().FileSize(*(Root/TEXT("Storage.png")))>1000;
        UE_LOG(LogTemp,Display,TEXT("CF_STORAGE_PREVIEW %s"),Ready?TEXT("PASS"):TEXT("FAIL")); FPlatformMisc::RequestExitWithStatus(false,Ready?0:1);},3.f,false);
}
}
void RunStorageScenario(UWorld* World,ACFPlayerCharacter* Hero,int32 Mode)
{
    using namespace CFStorageQA;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It) It->Destroy();
    if(Mode==2)
    {
        auto* Saved=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("StorageCross"),0));
        const int32 ItemCount=Saved && Saved->FormatVersion<23?6:static_cast<int32>(EFrontierItem::Count);
        const bool Loaded=Saved && Hero->LoadProgress(TEXT("StorageCross")) && Snapshot(World,Hero,ItemCount) && Hero->LoadProgress(TEXT("StorageCross")) && Snapshot(World,Hero,ItemCount);
        Finish(World,Hero,Loaded,Mode); return;
    }
    Hero->GetCharacterMovement()->DisableMovement(); Hero->SetActorLocation(FVector(-400,-1050,40));
    TMap<EFrontierItem,int32> Initial;
    for(int32 Index=0;Index<static_cast<int32>(EFrontierItem::Count);++Index) Initial.Add(static_cast<EFrontierItem>(Index),Index==0?100:10);
    Hero->Inventory->Restore(Initial);
    Hero->HomeBuilding->Selected=EHomeModuleKind::Floor;
    auto* Floor=Hero->HomeBuilding->BuildAt(FTransform(FVector(-400,-800,-40)));
    Hero->HomeBuilding->Selected=EHomeModuleKind::Storage;
    auto* Crate=Floor?Hero->HomeBuilding->BuildAt(FTransform(FVector(-400,-800,-30)),Floor->PersistentId):nullptr;
    if(!Crate) { UE_LOG(LogTemp,Error,TEXT("CF_STORAGE FAIL build: %s"),*Hero->Feedback); Finish(World,Hero,false,Mode); return; }
    bool Build=Hero->Inventory->Count(EFrontierItem::Material)==86 && Crate->StorageInventory->GetStacks().IsEmpty();
    const auto CrateId=Crate->PersistentId; const auto FloorId=Floor->PersistentId;
    Hero->FirstPersonCamera->SetWorldRotation((Crate->GetActorLocation()+FVector(0,0,40)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());
    Hero->CommandWork(); bool Interface=Hero->Storage->GetCrate()==Crate;
    for(int32 Index=0;Index<static_cast<int32>(EFrontierItem::Count);++Index)
    {
        Interface &= Hero->Storage->GetSelection()==Index;
        for(int32 Unit=0;Unit<=Index;++Unit) Hero->ConfirmInterfaceAction();
        Hero->PauseMenuNext();
    }
    Interface &= Snapshot(World,Hero);
    Hero->Storage->WithdrawOne(); Hero->Storage->DepositOne();
    Interface &= Snapshot(World,Hero);
    Hero->Storage->WithdrawStack(); Hero->Storage->DepositStack(); // all-or-nothing full stack round trip, then restore exact distribution.
    bool UIStacks=Crate->StorageInventory->Count(EFrontierItem::Material)==86 && Hero->Inventory->Count(EFrontierItem::Material)==0;
    Interface &= UIStacks && Hero->Storage->Transfer(EFrontierItem::Material,85,false) && Snapshot(World,Hero);
    const auto BeforePack=Hero->Inventory->GetStacks(); const auto BeforeStock=Crate->StorageInventory->GetStacks();
    bool Atomic=!Hero->Inventory->TransferTo(Hero->Inventory,{{EFrontierItem::Material,1}}) &&
        !Hero->Inventory->TransferTo(Crate->StorageInventory,{{EFrontierItem::Count,1}}) &&
        !Hero->Storage->Transfer(EFrontierItem::Material,0,true) && !Hero->Storage->Transfer(EFrontierItem::Material,-1,true) &&
        !Hero->Storage->Transfer(EFrontierItem::Material,999,true);
    Crate->StorageInventory->Restore({{EFrontierItem::Material,999},{EFrontierItem::CaptureDevice,2}});
    const auto FullStock=Crate->StorageInventory->GetStacks();
    Atomic &= !Hero->Inventory->TransferTo(Crate->StorageInventory,{{EFrontierItem::CaptureDevice,1},{EFrontierItem::Material,1}}) &&
        Hero->Inventory->GetStacks().OrderIndependentCompareEqual(BeforePack) && Crate->StorageInventory->GetStacks().OrderIndependentCompareEqual(FullStock);
    Crate->StorageInventory->Restore(BeforeStock); Hero->Inventory->Restore({{EFrontierItem::Material,999}});
    Atomic &= !Hero->Storage->Transfer(EFrontierItem::Material,1,false) && Crate->StorageInventory->GetStacks().OrderIndependentCompareEqual(BeforeStock) && Hero->Inventory->Count(EFrontierItem::Material)==999;
    Hero->Inventory->Restore(BeforePack);
    bool Access=true;
    Hero->SetActorLocation(FVector(-400,-1400,40)); Access &= !Hero->Storage->Transfer(EFrontierItem::Material,1,true);
    Hero->SetActorLocation(FVector(-400,-1050,40)); Hero->Storage->Open(Crate);
    auto* Block=World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Block); Block->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(100,10,150)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->SetCanEverAffectNavigation(false); Box->RegisterComponent(); Block->SetActorLocation(FVector(-400,-930,60));
    Access &= !Hero->Storage->Transfer(EFrontierItem::Material,1,true); Block->SetActorEnableCollision(false); Block->Destroy();
    Hero->Storage->Open(Crate); Hero->TogglePauseMenu(); Access &= !Hero->Storage->IsOpen() && !Hero->Storage->Transfer(EFrontierItem::Material,1,true); Hero->TogglePauseMenu();
    Crate->bPreview=true; Access &= !Hero->Storage->Open(Crate); Crate->bPreview=false;
    Crate->bStaged=true; Access &= !Hero->Storage->Open(Crate); Crate->bStaged=false;
    Access &= Snapshot(World,Hero);
    bool Removal=!Hero->HomeBuilding->Dismantle(Crate) && !Hero->HomeBuilding->Dismantle(Floor) && Snapshot(World,Hero);
    const FString Good=TEXT("StorageGood");
    bool Saved=Hero->SaveProgress(Good) && Hero->LoadProgress(Good) && Snapshot(World,Hero) && Hero->LoadProgress(Good) && Snapshot(World,Hero);
    auto* LoadedCrate=Find(World,EHomeModuleKind::Storage); Saved &= LoadedCrate && LoadedCrate->PersistentId==CrateId && LoadedCrate->FloorId==FloorId;
    bool Invalid=true;
    for(int32 Case=0;Case<9;++Case)
    {
        auto* Live=Find(World,EHomeModuleKind::Storage); auto* Bad=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Good,0));
        auto* Record=Bad?Bad->HomeModules.FindByPredicate([](const FHomeModuleRecord& R){return R.Definition.LoadSynchronous() && R.Definition.Get()->Kind==EHomeModuleKind::Storage;}):nullptr;
        if(!Record) { Invalid=false; break; }
        if(Case==0) Record->StoredItems.Add(EFrontierItem::Count,1);
        if(Case==1) Record->StoredItems.Add(EFrontierItem::Material,1000);
        if(Case==2) Record->StoredItems.Add(EFrontierItem::Material,-1);
        if(Case==3) for(auto& R:Bad->HomeModules) if(R.Id==FloorId) R.StoredItems.Add(EFrontierItem::Material,1);
        if(Case==4) Record->FloorId=FGuid::NewGuid();
        if(Case==5) { const auto Copy=*Record; Bad->HomeModules.Add(Copy); }
        if(Case==6)
        {
            Bad->FormatVersion=20;
            for(auto Item:{EFrontierItem::Fiber,EFrontierItem::Ore,EFrontierItem::Scrap}) Bad->InventoryStacks.Remove(Item);
        }
        if(Case==7) { FCreatureSaveRecord Broken; Broken.Id=FGuid::NewGuid(); Broken.CreatureClass=ACreatureCharacter::StaticClass(); Broken.Health=-1; Bad->Creatures.Add(Broken); }
        if(Case==8) { FFacilitySaveRecord Overlap; Overlap.Id=FGuid::NewGuid(); Overlap.Transform=FTransform(FRotator::ZeroRotator,FVector(-400,-800,5),FVector(2,1,.7)); Bad->Facilities.Add(Overlap); }
        Invalid &= UGameplayStatics::SaveGameToSlot(Bad,TEXT("StorageInvalid"),0) && !Hero->LoadProgress(TEXT("StorageInvalid")) && IsValid(Live) && Live->PersistentId==CrateId && Snapshot(World,Hero);
    }
    auto* Legacy=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Good,0)); Legacy->FormatVersion=20;
    auto LegacyPack=BeforePack;
    for(auto Item:{EFrontierItem::Fiber,EFrontierItem::Ore,EFrontierItem::Scrap}) {Legacy->InventoryStacks.Remove(Item); LegacyPack.Remove(Item);}
    Legacy->HomeModules.RemoveAll([](const FHomeModuleRecord& R){return R.Definition.LoadSynchronous()->Kind==EHomeModuleKind::Storage;});
    Legacy->HomeModules[0].StoredItems.Add(EFrontierItem::Crystal,999); // Future field ignored for actual old-format structure.
    bool Migration=UGameplayStatics::SaveGameToSlot(Legacy,TEXT("StorageLegacy20"),0) && Hero->LoadProgress(TEXT("StorageLegacy20")) &&
        !Find(World,EHomeModuleKind::Storage) && Hero->Inventory->GetStacks().OrderIndependentCompareEqual(LegacyPack) &&
        Hero->LoadProgress(TEXT("StorageLegacy20")) && !Find(World,EHomeModuleKind::Storage) && Hero->Inventory->GetStacks().OrderIndependentCompareEqual(LegacyPack);
    Saved &= Hero->LoadProgress(Good) && Snapshot(World,Hero);
    Crate=Find(World,EHomeModuleKind::Storage); Hero->Storage->Open(Crate);
    UGameplayStatics::ApplyDamage(Hero,500,nullptr,nullptr,nullptr);
    bool Death=Hero->Health<=0 && !Hero->Storage->IsOpen() && !Hero->Storage->Transfer(EFrontierItem::Material,1,false) && Snapshot(World,Hero);
    Saved &= Hero->LoadProgress(Good) && Snapshot(World,Hero);
    Crate=Find(World,EHomeModuleKind::Storage);
    Hero->Storage->Open(Crate); bool Empty=true;
    for(int32 Index=0;Index<static_cast<int32>(EFrontierItem::Count);++Index) Empty &= Hero->Storage->Transfer(static_cast<EFrontierItem>(Index),Index+1,false);
    Removal &= Empty && Hero->HomeBuilding->Dismantle(Crate) && !Hero->Storage->IsOpen() && Hero->Inventory->Count(EFrontierItem::Material)==89;
    Saved &= Hero->LoadProgress(Good) && Snapshot(World,Hero) && Hero->SaveProgress(TEXT("StorageCross"));
    UE_LOG(LogTemp,Display,TEXT("CF_STORAGE_CHECK build=%d interface=%d atomic=%d access=%d removal=%d saved=%d invalid=%d migration=%d death=%d"),Build,Interface,Atomic,Access,Removal,Saved,Invalid,Migration,Death);
    Finish(World,Hero,Build && Interface && Atomic && Access && Removal && Saved && Invalid && Migration && Death,Mode);
}
