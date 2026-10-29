#include "ConstructionScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CompanionConstructionComponent.h"
#include "CompanionCargoComponent.h"
#include "HomeBuildingComponent.h"
#include "HomeModule.h"
#include "HomeLayout.h"
#include "InventoryComponent.h"
#include "FrontierSaveGame.h"
#include "WildEncounter.h"
#include "WorkStation.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "InputRoutingQA.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Containers/Ticker.h"

void RunConstructionHomeScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto End=[Hero](bool Passed,const TCHAR* Reason){UE_LOG(LogTemp,Display,TEXT("CF_CONSTRUCTION_HOME %s reason=%s feedback=%s"),Passed?TEXT("PASS"):TEXT("FAIL"),Reason,*Hero->Feedback);FPlatformMisc::RequestExitWithStatus(false,Passed?0:1);return false;};
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("ConstructionInput"),0));
    if(!Source || (Read?(Source->FormatVersion<35 || Source->FormatVersion>FrontierCurrentSaveVersion):Source->FormatVersion!=34) || Source->HomeModules.Num()!=19 || Source->Facilities.Num()!=2 || !Hero->LoadProgress(TEXT("ConstructionInput")) ||
        Hero->Inventory->Count(EFrontierItem::Material)!=(Read?0:2) || Hero->Inventory->Count(EFrontierItem::Food)!=1 || Hero->Inventory->Count(EFrontierItem::Seeds)!=1){End(false,TEXT("source"));return;}
    ACreatureCharacter* Pet=nullptr;AHomeModule* Crate=nullptr;AHomeModule* Fence=nullptr;AHomeModule* Door=nullptr;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController())Pet=*It;
    for(TActorIterator<AHomeModule> It(World);It;++It){if(It->GetKind()==EHomeModuleKind::Storage)Crate=*It;if(It->GetKind()==EHomeModuleKind::Fence && It->GetActorLocation().Equals(FVector(0,-600,-30),.01))Fence=*It;if(It->GetKind()==EHomeModuleKind::Door && It->GetActorLocation().Equals(FVector(-400,-1000,-30),.01))Door=*It;}
    if(!Pet || !Crate || !Fence || !Door || Pet->Experience!=8 || Pet->BondPoints!=6 || Pet->PersistentId!=FGuid(0x1209AAFE,0x4D5C4544,0x1D65AF94,0x1B0435D1)){End(false,TEXT("identity"));return;}
    auto Face=[Hero](AActor* Actor,float Z){const auto Aim=(Actor->GetActorLocation()+FVector(0,0,Z)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(Aim);Hero->FirstPersonCamera->SetWorldRotation(Aim);};
    if(!Read)
    {
        Hero->SetActorLocation(FVector(-400,-1250,60));Face(Door,125);
        if(Door->DoorState==1 && (!CFInputQA::Press(Hero,EKeys::E) || Door->DoorState!=2)){End(false,TEXT("door"));return;}
        Hero->SetActorLocation(FVector(300,-400,60));const auto Floor=Fence->FloorId;
        if(!Hero->HomeBuilding->Dismantle(Fence) || Hero->Inventory->Count(EFrontierItem::Material)!=4){End(false,TEXT("refund"));return;}
        Hero->SetActorLocation(FVector(-750,-850,60));Hero->HomeBuilding->Selected=EHomeModuleKind::Fence;
        Fence=Hero->HomeBuilding->QueueAt(FTransform(FVector(0,-600,-30)),Floor);if(!Fence){End(false,TEXT("queue"));return;}
        Face(Crate,40);
        if(!CFInputQA::Press(Hero,EKeys::E) || !CFInputQA::Press(Hero,EKeys::Right) || !CFInputQA::Press(Hero,EKeys::E) ||
            Hero->Inventory->Count(EFrontierItem::Material)!=0 || Crate->StorageInventory->Count(EFrontierItem::Material)!=4){End(false,TEXT("stock"));return;}
        if(!CFInputQA::Press(Hero,EKeys::V)){End(false,TEXT("select"));return;}Face(Fence,12);
        if(!CFInputQA::Press(Hero,EKeys::E) || Pet->Construction->GetSite()!=Fence){End(false,TEXT("order"));return;}
        // 施工输入仍用明确观察点，伙伴不传送；离开室内物流通道避免长期阻挡工人。
        Hero->SetActorLocation(FVector(300,-400,60));
        UE_LOG(LogTemp,Display,TEXT("CF_CONSTRUCTION_HOME_ORIGIN PASS version=34 modules=19 refund=2 originalMaterial=2 stock=4 pet=%s"),*Pet->PersistentId.ToString());
    }
    else if(!Fence->bUnderConstruction || Pet->Construction->GetSite()!=Fence || Pet->Cargo->Count()!=3 || Crate->StorageInventory->Count(EFrontierItem::Material)!=1){End(false,TEXT("resume"));return;}
    if(Read){UE_LOG(LogTemp,Display,TEXT("CF_CONSTRUCTION_HOME_RESTORE PASS version=%d carried=3 stock=1 identity=1"),Source->FormatVersion);}
    for(TActorIterator<AWildEncounter> It(World);It;++It)It->SetActorTickEnabled(false);
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(*It!=Pet){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();}
    const auto PetId=Pet->PersistentId,SiteId=Fence->PersistentId,BoxId=Crate->PersistentId;
    const auto SourceHouse=Source->HomeModules;const auto Bag=Hero->Inventory->GetStacks();
    auto Previous=MakeShared<FVector>(Pet->GetActorLocation());auto Travel=MakeShared<double>(0);auto SavedMid=MakeShared<bool>(Read);
    const double Deadline=FPlatformTime::Seconds()+100;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([=](float)
    {
        if(!IsValid(Pet) || !IsValid(Fence) || !IsValid(Crate) || FPlatformTime::Seconds()>Deadline)return End(false,TEXT("lifetime"));
        const double Step=FVector::Dist(Pet->GetActorLocation(),*Previous);*Previous=Pet->GetActorLocation();*Travel+=Step;
        if(Step>120 || !Pet->IsActorTickEnabled() || !Pet->GetActorEnableCollision() || Pet->Health<=0 || Hero->Health<=0 ||
            Crate->StorageInventory->Count(EFrontierItem::Material)+Fence->StorageInventory->Count(EFrontierItem::Material)+Pet->Cargo->Count()+(Fence->bUnderConstruction?0:Fence->PaidMaterials)!=4)return End(false,TEXT("conservation"));
        if(!*SavedMid && Pet->Cargo->Count()==3){if(!Hero->SaveProgress(TEXT("ConstructionHomeMid")))return End(false,TEXT("mid"));*SavedMid=true;UE_LOG(LogTemp,Display,TEXT("CF_CONSTRUCTION_HOME_MID PASS version=36 carried=3 identity=1"));}
        if(Fence->bUnderConstruction)return true;
        if(!*SavedMid || *Travel<600 || Fence->PersistentId!=SiteId || Pet->Experience!=8 || Pet->BondPoints!=6 || Pet->Cargo->Count()!=0 || Pet->Construction->GetSite() ||
            !Bag.OrderIndependentCompareEqual(Hero->Inventory->GetStacks()) || !Hero->SaveProgress(TEXT("ConstructionHomeDone")) || !Hero->LoadProgress(TEXT("ConstructionHomeDone")))return End(false,TEXT("done"));
        const auto* Done=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("ConstructionHomeDone"),0));
        if(!Done || Done->FormatVersion!=FrontierCurrentSaveVersion || Done->HomeModules.Num()!=19 || Done->Facilities.Num()!=2)return End(false,TEXT("done-save"));
        for(const auto& Old:SourceHouse)
        {
            if(Old.Transform.GetLocation().Equals(FVector(0,-600,-30),.01))continue;
            const auto* Now=Done->HomeModules.FindByPredicate([&](const FHomeModuleRecord& R){return R.Id==Old.Id;});
            if(!Now || Now->Definition!=Old.Definition || Now->FloorId!=Old.FloorId || !Now->Transform.Equals(Old.Transform,.01) || Now->PaidMaterials!=Old.PaidMaterials || Now->bUnderConstruction)return End(false,TEXT("retained-house"));
        }
        const auto* Worker=Done->Creatures.FindByPredicate([&](const FCreatureSaveRecord& R){return R.Id==PetId && R.bOwned;});
        const auto* Built=Done->HomeModules.FindByPredicate([&](const FHomeModuleRecord& R){return R.Id==SiteId;});
        const auto* Box=Done->HomeModules.FindByPredicate([&](const FHomeModuleRecord& R){return R.Id==BoxId;});
        if(!Worker || Worker->Experience!=8 || Worker->BondPoints!=6 || Worker->ConstructionModuleId.IsValid() || Worker->Activity!=ECreatureActivity::Follow || !Built || Built->bUnderConstruction || Built->PaidMaterials!=4 || !Box || Box->StoredItems.FindRef(EFrontierItem::Material)!=0)return End(false,TEXT("identity-done"));
        UE_LOG(LogTemp,Display,TEXT("CF_CONSTRUCTION_HOME_DONE PASS version=36 modules=19 retained=18 material=0 food=1 seeds=1 cost=4 travel=%.1f pet=%s site=%s"),*Travel,*PetId.ToString(),*SiteId.ToString());return End(true,TEXT("complete"));
    }));
}
