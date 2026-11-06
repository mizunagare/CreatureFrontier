#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "WorkStation.h"
#include "HomeModule.h"
#include "FarmCropComponent.h"
#include "InventoryComponent.h"
#include "FrontierSaveGame.h"
#include "EngineUtils.h"
#include "Containers/Ticker.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "CompanionApproach.h"
#include "CompanionClearance.h"
#include "CompanionCargoComponent.h"
#include "ResourceNode.h"
#include "Camera/CameraComponent.h"

// 真实家园副本上的显式存读夹具：仅首次投入3份测试种子，不代表生产来源。
void RunSeedSupplyPersistence(UWorld* World,ACFPlayerCharacter* Hero)
{
    auto Fail=[](const TCHAR* Why){UE_LOG(LogTemp,Error,TEXT("CF_SEED_PERSIST FAIL reason=%s"),Why);FPlatformMisc::RequestExitWithStatus(false,1);};
    const bool Read=FParse::Param(FCommandLine::Get(),TEXT("CFSeedSupplyRead"));
    if(!Hero->LoadProgress(TEXT("SeedSupplyInput"))){Fail(TEXT("load"));return;}
    if(FParse::Param(FCommandLine::Get(),TEXT("CFWorkshopCargoInvalid")))
    {
        ACreatureCharacter* Held=nullptr;AWorkStation* Farm=nullptr;
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController() && It->CargoInventory->Count(EFrontierItem::Fiber)==3)Held=*It;
        for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt && It->RequiredSpecialty==EWorkSpecialty::Farming)Farm=*It;
        auto* Target=Held?Cast<AWorkStation>(Held->Cargo->GetDestination()):nullptr;TArray<uint8> Before;
        if(!Held || !Target || !Farm || !Held->Cargo->IsReturning() || !Hero->SaveProgress(TEXT("SeedSupplyBefore")) || !UGameplayStatics::LoadDataFromSlot(Before,TEXT("SeedSupplyBefore"),0)){Fail(TEXT("workshop-cargo-baseline"));return;}
        const auto Id=Held->PersistentId;
        for(int32 Case=0;Case<10;++Case)
        {
            auto* Bad=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("SeedSupplyBefore"),0));
            auto* R=Bad?Bad->Creatures.FindByPredicate([Id](const FCreatureSaveRecord& Item){return Item.Id==Id;}):nullptr;
            if(!R){Fail(TEXT("workshop-cargo-record"));return;}
            switch(Case)
            {
                case 0:R->DeliveryWorkshopId=FGuid::NewGuid();break;
                case 1:R->DeliveryCrateId=FGuid::NewGuid();break;
                case 2:R->FieldResourceId.Invalidate();break;
                case 3:R->CarriedFood=1;break;
                case 4:R->CarriedSeeds=1;R->SeedCargoVersion=1;break;
                case 5:R->ConstructionModuleId=FGuid::NewGuid();break;
                case 6:Bad->FormatVersion=38;break;
                case 7:R->FieldResourceId=FGuid::NewGuid();break;
                case 8:R->DeliveryWorkshopId=Farm->PersistentId;break;
                case 9:R->CargoItems[EFrontierItem::Fiber]=4;break;
            }
            TArray<uint8> After;
            const bool Pass=UGameplayStatics::SaveGameToSlot(Bad,TEXT("SeedSupplyBad"),0) && !Hero->LoadProgress(TEXT("SeedSupplyBad")) && IsValid(Held) && !Held->IsActorBeingDestroyed() && IsValid(Target) && !Target->IsActorBeingDestroyed() && Held->Cargo->GetDestination()==Target && Held->CargoInventory->Count(EFrontierItem::Fiber)==3 && Hero->SaveProgress(TEXT("SeedSupplyAfter")) && UGameplayStatics::LoadDataFromSlot(After,TEXT("SeedSupplyAfter"),0) && Before==After;
            UE_LOG(LogTemp,Display,TEXT("CF_WORKSHOP_CARGO_INVALID_CASE %s case=%d worldBytesUnchanged=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Case,Before==After);
            if(!Pass){Fail(TEXT("workshop-cargo-invalid-transaction"));return;}
        }
        if(FParse::Param(FCommandLine::Get(),TEXT("CFWorkshopCargoBoundary")))
        {
            // 满格边界夹具明确投入999纤维；原携物、位置、资源节点和移动规则不改。
            Target->CraftInventory->Restore({{EFrontierItem::Fiber,999}});
            const int32 XP=Held->Experience,Remaining=Held->GetFieldResource()->Remaining;
            const auto Bag=Hero->Inventory->GetStacks();const FVector Start=Held->GetActorLocation();
            const double Deadline=FPlatformTime::Seconds()+60;
            auto Phase=MakeShared<int32>(0);auto FullAt=MakeShared<double>(0);
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([=](float)
            {
                if(FPlatformTime::Seconds()>Deadline || !IsValid(Held) || !IsValid(Target) || Held->Experience!=XP || !Held->GetFieldResource() || Held->GetFieldResource()->Remaining!=Remaining || !Hero->Inventory->GetStacks().OrderIndependentCompareEqual(Bag)){Fail(TEXT("cargo-boundary-state"));return false;}
                const int32 Count=Held->Cargo->Count(),Stock=Target->CraftInventory->Count(EFrontierItem::Fiber);
                if(*Phase==0)
                {
                    if(Count!=3 || Stock!=999){Fail(TEXT("cargo-full-loss"));return false;}
                    if(!Held->Cargo->Status().Contains(TEXT("设施库存已满")))return true;
                    if(Held->GetDistanceTo(Target)>220 || FVector::Dist2D(Start,Held->GetActorLocation())<400){Fail(TEXT("cargo-full-no-approach"));return false;}
                    *FullAt=World->GetTimeSeconds();*Phase=1;return true;
                }
                if(*Phase==1)
                {
                    if(Count!=3 || Stock!=999){Fail(TEXT("cargo-full-wait-loss"));return false;}
                    if(World->GetTimeSeconds()-*FullAt<2)return true;
                    if(!Target->CraftInventory->Consume(EFrontierItem::Fiber,3)){Fail(TEXT("cargo-full-free-space"));return false;}
                    *Phase=2;return true;
                }
                if(Count!=0){if(Count!=3 || Stock!=996){Fail(TEXT("cargo-partial-unload"));return false;}return true;}
                if(Stock!=999){Fail(TEXT("cargo-unload-conservation"));return false;}
                auto FindHeld=[World,Id](){for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->PersistentId==Id)return *It;return static_cast<ACreatureCharacter*>(nullptr);};
                if(!Hero->LoadProgress(TEXT("SeedSupplyBefore"))){Fail(TEXT("cargo-cancel-source"));return false;}
                auto* Pet=FindHeld();if(!Pet){Fail(TEXT("cargo-cancel-pet"));return false;}
                Hero->CommandFollow();
                if(Pet->Cargo->Count()!=3 || Pet->Cargo->HasOrder() || Pet->GetFieldResource() || !Hero->SaveProgress(TEXT("SeedSupplyAfter")) || !Hero->LoadProgress(TEXT("SeedSupplyAfter"))){Fail(TEXT("cargo-cancel-save"));return false;}
                Pet=FindHeld();if(!Pet || Pet->Cargo->Count()!=3 || Pet->Cargo->HasOrder() || Pet->Experience!=XP){Fail(TEXT("cargo-cancel-restore"));return false;}
                if(!Hero->LoadProgress(TEXT("SeedSupplyBefore"))){Fail(TEXT("cargo-removed-source"));return false;}
                Pet=FindHeld();auto* Removed=Pet?Pet->Cargo->GetDestination():nullptr;
                // 模拟目标在本帧AI更新前消失，验证写档规范化，不声称玩家远程拆除。
                if(!Removed || !Removed->Destroy() || !Hero->SaveProgress(TEXT("SeedSupplyAfter")) || !Hero->LoadProgress(TEXT("SeedSupplyAfter"))){Fail(TEXT("cargo-removed-save"));return false;}
                Pet=FindHeld();
                if(!Pet || Pet->Cargo->Count()!=3 || Pet->Cargo->HasOrder() || Pet->GetFieldResource() || Pet->Experience!=XP || !Hero->SaveProgress(TEXT("SeedSupplyCarry"))){Fail(TEXT("cargo-removed-restore"));return false;}
                UE_LOG(LogTemp,Display,TEXT("CF_WORKSHOP_CARGO_BOUNDARY PASS full=1 waited=1 delivered=1 cancelledRestore=1 removedRestore=1 fixtureStock=999"));
                UE_LOG(LogTemp,Display,TEXT("CF_WORKSHOP_CARGO_INVALID PASS rejected=10 worldUnchanged=1"));FPlatformMisc::RequestExitWithStatus(false,0);return false;
            }));return;
        }
        if(!Hero->SaveProgress(TEXT("SeedSupplyCarry"))){Fail(TEXT("workshop-cargo-save"));return;}
        UE_LOG(LogTemp,Display,TEXT("CF_WORKSHOP_CARGO_INVALID PASS rejected=10 worldUnchanged=1"));FPlatformMisc::RequestExitWithStatus(false,0);return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("CFWorkshopStockPersistence")))
    {
        auto FindWorkshop=[World](){AWorkStation* Found=nullptr;for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt && It->RequiredSpecialty==EWorkSpecialty::Crafting){if(Found)return static_cast<AWorkStation*>(nullptr);Found=*It;}return Found;};
        auto* Site=FindWorkshop();if(!Site){Fail(TEXT("workshop-source"));return;}
        const TMap<EFrontierItem,int32> Expected={{EFrontierItem::Food,1},{EFrontierItem::Fiber,1},{EFrontierItem::Medicine,2}};
        if(!Read)
        {
            // 隔离存读夹具仅首次投入明确物资和正值工序，不作为正常备料或加工耗时证据。
            if(!Site->CraftRecipeId.IsNone() || !Site->CraftInventory->GetStacks().IsEmpty()){Fail(TEXT("workshop-not-empty"));return;}
            Site->CraftInventory->Restore(Expected);Site->CraftRecipeId=TEXT("PlantingSeeds");Site->CraftWork=2;Site->bCraftUsesLocalStock=true;
        }
        auto Matches=[&](AWorkStation* Current){return Current && Current->CraftRecipeId==TEXT("PlantingSeeds") && Current->CraftWork==2 && Current->bCraftUsesLocalStock && Current->CraftInventory->GetStacks().OrderIndependentCompareEqual(Expected);};
        if(!Matches(Site)){Fail(TEXT("workshop-positive-state"));return;}
        const auto Id=Site->PersistentId;const auto Transform=Site->GetActorTransform();const auto Bag=Hero->Inventory->GetStacks();
        if(!Hero->SaveProgress(TEXT("SeedSupplyBefore")) || !Hero->LoadProgress(TEXT("SeedSupplyBefore"))){Fail(TEXT("workshop-roundtrip"));return;}
        Site=FindWorkshop();
        if(!Matches(Site) || Site->PersistentId!=Id || !Site->GetActorTransform().Equals(Transform,.01) || !Hero->Inventory->GetStacks().OrderIndependentCompareEqual(Bag)){Fail(TEXT("workshop-restored-state"));return;}
        // 以恢复后的世界为基线，非法读取必须保持实体身份和完整序列化字节不变。
        TArray<uint8> Before;
        if(!Hero->SaveProgress(TEXT("SeedSupplyBefore")) || !UGameplayStatics::LoadDataFromSlot(Before,TEXT("SeedSupplyBefore"),0)){Fail(TEXT("workshop-baseline"));return;}
        for(int32 Case=0;Case<8;++Case)
        {
            auto* Bad=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("SeedSupplyBefore"),0));
            auto* Record=Bad?Bad->Facilities.FindByPredicate([Id](const FFacilitySaveRecord& R){return R.Id==Id;}):nullptr;
            if(!Record){Fail(TEXT("workshop-record"));return;}
            switch(Case)
            {
                case 0:Record->CraftStockRevision=2;break;
                case 1:Record->CraftStockRevision=0;break;
                case 2:Record->CraftStock[EFrontierItem::Food]=-1;break;
                case 3:Record->CraftStock[EFrontierItem::Food]=1000;break;
                case 4:Record->CraftStock.Add(EFrontierItem::Count,1);break;
                case 5:Record->Specialty=EWorkSpecialty::Transport;break;
                case 6:Record->CraftRevision=1;break;
                case 7:Record->bCraftUsesLocalStock=false;break;
            }
            TArray<uint8> After;
            if(!UGameplayStatics::SaveGameToSlot(Bad,TEXT("SeedSupplyBad"),0) || Hero->LoadProgress(TEXT("SeedSupplyBad")) || !IsValid(Site) || FindWorkshop()!=Site || !Matches(Site) || !Hero->SaveProgress(TEXT("SeedSupplyAfter")) || !UGameplayStatics::LoadDataFromSlot(After,TEXT("SeedSupplyAfter"),0) || Before!=After){Fail(TEXT("workshop-invalid-transaction"));return;}
        }
        auto* Legacy=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("SeedSupplyBefore"),0));
        auto* Record=Legacy?Legacy->Facilities.FindByPredicate([Id](const FFacilitySaveRecord& R){return R.Id==Id;}):nullptr;
        if(!Record){Fail(TEXT("workshop-legacy-record"));return;}
        Record->CraftRevision=1;Record->bCraftUsesLocalStock=false;Record->CraftStockRevision=0;Record->CraftStock.Empty();
        if(!UGameplayStatics::SaveGameToSlot(Legacy,TEXT("SeedSupplyBad"),0) || !Hero->LoadProgress(TEXT("SeedSupplyBad"))){Fail(TEXT("workshop-legacy-load"));return;}
        Site=FindWorkshop();
        if(!Site || Site->PersistentId!=Id || Site->CraftWork!=2 || Site->CraftRecipeId!=TEXT("PlantingSeeds") || Site->bCraftUsesLocalStock || !Site->CraftInventory->GetStacks().IsEmpty() || !Hero->Inventory->GetStacks().OrderIndependentCompareEqual(Bag)){Fail(TEXT("workshop-legacy-state"));return;}
        if(!Hero->LoadProgress(TEXT("SeedSupplyBefore")) || !Matches(FindWorkshop()) || !Hero->SaveProgress(Read?TEXT("SeedSupplyDone"):TEXT("SeedSupplyCarry"))){Fail(TEXT("workshop-final"));return;}
        UE_LOG(LogTemp,Display,TEXT("CF_WORKSHOP_STOCK_PERSIST PASS read=%d positive=1 progress=2 invalid=8 worldUnchanged=1 legacy=1"),Read);FPlatformMisc::RequestExitWithStatus(false,0);return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("CFSeedSupplyInvalid")))
    {
        // 实际写盘并走正式读取事务；同一同步调用内比较完整世界快照，不推进或冻结世界。
        ACreatureCharacter* Held=nullptr;for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController() && It->CarriedSeeds==3)Held=*It;
        TArray<uint8> Before;
        if(!Held || !Hero->SaveProgress(TEXT("SeedSupplyBefore")) || !UGameplayStatics::LoadDataFromSlot(Before,TEXT("SeedSupplyBefore"),0)){Fail(TEXT("invalid-baseline"));return;}
        const FGuid Id=Held->PersistentId;
        for(int32 Case=0;Case<8;++Case)
        {
            auto* Bad=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("SeedSupplyBefore"),0));
            auto* Record=Bad?Bad->Creatures.FindByPredicate([Id](const FCreatureSaveRecord& R){return R.Id==Id;}):nullptr;
            if(!Record){Fail(TEXT("invalid-record"));return;}
            if(Case==0)Record->CarriedSeeds=-1;if(Case==1)Record->CarriedSeeds=4;
            if(Case==2)Record->SeedCargoVersion=0;if(Case==3)Record->SeedCargoVersion=2;
            if(Case==4)Record->bOwned=false;if(Case==5)Record->CarriedFood=1;
            if(Case==6)Record->CargoItems.Add(EFrontierItem::Material,1);if(Case==7)Record->ConstructionModuleId=FGuid::NewGuid();
            TArray<uint8> After;
            const bool Pass=UGameplayStatics::SaveGameToSlot(Bad,TEXT("SeedSupplyBad"),0) && !Hero->LoadProgress(TEXT("SeedSupplyBad")) &&
                IsValid(Held) && !Held->IsActorBeingDestroyed() && Held->PersistentId==Id && Held->CarriedSeeds==3 &&
                Hero->SaveProgress(TEXT("SeedSupplyAfter")) && UGameplayStatics::LoadDataFromSlot(After,TEXT("SeedSupplyAfter"),0) && Before==After;
            UE_LOG(LogTemp,Display,TEXT("CF_SEED_INVALID_CASE %s case=%d worldBytesUnchanged=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Case,Before==After);
            if(!Pass){Fail(TEXT("invalid-transaction"));return;}
        }
        UE_LOG(LogTemp,Display,TEXT("CF_SEED_INVALID PASS rejected=8 worldUnchanged=1"));FPlatformMisc::RequestExitWithStatus(false,0);return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("CFSeedSupplyInspect")))
    {
        FHitResult Aim;FCollisionQueryParams AimQuery(SCENE_QUERY_STAT(SavedAimInspect),false,Hero);
        const auto Eye=Hero->FirstPersonCamera->GetComponentLocation();
        World->LineTraceSingleByChannel(Aim,Eye,Eye+Hero->FirstPersonCamera->GetForwardVector()*800,ECC_Visibility,AimQuery);
        UE_LOG(LogTemp,Display,TEXT("CF_SEED_INSPECT_AIM actor=%s class=%s distance=%.2f resource=%s eye=%s"),*GetNameSafe(Aim.GetActor()),Aim.GetActor()?*Aim.GetActor()->GetClass()->GetName():TEXT("none"),Aim.Distance,*GetNameSafe(Hero->TraceResource()),*Eye.ToString());
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController())
            UE_LOG(LogTemp,Display,TEXT("CF_SEED_INSPECT_PET id=%s pos=%s activity=%d health=%.2f stamina=%.2f nutrition=%.2f xp=%d seeds=%d food=%d site=%s"),*It->PersistentId.ToString(),*It->GetActorLocation().ToString(),static_cast<int32>(It->Activity),It->Health,It->Stamina,It->Nutrition,It->Experience,It->CarriedSeeds,It->CarriedFood,IsValid(It->WorkSite)?*It->WorkSite->PersistentId.ToString():TEXT("none"));
        for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt)
            UE_LOG(LogTemp,Display,TEXT("CF_SEED_INSPECT_SITE id=%s kind=%d food=%d seeds=%d planted=%d growth=%.2f"),*It->PersistentId.ToString(),static_cast<int32>(It->RequiredSpecialty),It->StoredFood,It->Crop->Seeds,It->Crop->bPlanted,It->Crop->GrowthWork);
        for(TActorIterator<ACreatureCharacter> P(World);P;++P)if(P->Tamer==Hero->GetController() && P->CarriedFood>0)
            for(TActorIterator<AWorkStation> T(World);T;++T)if(T->RequiredSpecialty==EWorkSpecialty::Feeding)
            {
                const auto Direction=(P->GetActorLocation()-T->GetActorLocation()).GetSafeNormal2D();FVector Goal=T->GetActorLocation()+Direction*150;Goal.Z=P->GetActorLocation().Z;
                FNavLocation Projected;auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);const bool Found=Nav && Nav->ProjectPointToNavigation(Goal,Projected,FVector(250,250,300));
                auto* Path=Found?UNavigationSystemV1::FindPathToLocationSynchronously(World,P->GetActorLocation(),Projected.Location,*P):nullptr;
                FVector Alternative=FVector::ZeroVector;const bool Safe=FindCompanionApproach(*P,*T,Alternative);
                UE_LOG(LogTemp,Display,TEXT("CF_SEED_INSPECT_PATH goal=%s projected=%s found=%d valid=%d partial=%d clear=%d alternative=%d safeGoal=%s"),*Goal.ToString(),*Projected.Location.ToString(),Found,Path&&Path->IsValid(),Path&&Path->IsPartial(),Path&&FrontierFollowPath::ClearPath(*P,Path),Safe,*Alternative.ToString());
            }
        UE_LOG(LogTemp,Display,TEXT("CF_SEED_INSPECT PASS loaded=1 noSave=1"));FPlatformMisc::RequestExitWithStatus(false,0);return;
    }
    ACreatureCharacter* Courier=nullptr;ACreatureCharacter* Farmer=nullptr;AHomeModule* Crate=nullptr;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController() && IsValid(It->WorkSite))
    {if(It->WorkSite->RequiredSpecialty==EWorkSpecialty::Transport)Courier=*It;if(It->WorkSite->RequiredSpecialty==EWorkSpecialty::Farming)Farmer=*It;}
    for(TActorIterator<AHomeModule> It(World);It;++It)if(It->GetKind()==EHomeModuleKind::Storage && !It->bPreview && !It->bStaged){if(Crate){Fail(TEXT("multiple-crates"));return;}Crate=*It;}
    if(FParse::Param(FCommandLine::Get(),TEXT("CFSeedFoodRecovery")))
    {
        if(!Courier || Courier->CarriedFood!=2 || Courier->CarriedSeeds!=0 || !Farmer || Farmer->Experience!=47){Fail(TEXT("food-recovery-source"));return;}
        const double Start=World->GetTimeSeconds();const int32 XP=Courier->Experience;auto NextLog=MakeShared<double>(0);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([World,Hero,Courier,Farmer,Start,XP,NextLog](float)
        {
            const double Now=World->GetTimeSeconds();int32 Stock=0;for(TActorIterator<AWorkStation> It(World);It;++It)if(It->RequiredSpecialty==EWorkSpecialty::Feeding)Stock+=It->StoredFood;
            if(Now>*NextLog){*NextLog=Now+2;UE_LOG(LogTemp,Display,TEXT("CF_SEED_FOOD_STATE pos=%s food=%d stock=%d stamina=%.2f farmer=%s"),*Courier->GetActorLocation().ToString(),Courier->CarriedFood,Stock,Courier->Stamina,*Farmer->GetActorLocation().ToString());}
            const bool Delivered=Courier->CarriedFood==0 && Courier->Experience==XP+1 && Stock>=2;
            if(Delivered || Now-Start>30){const bool Saved=Hero->SaveProgress(Delivered?TEXT("SeedSupplyDone"):TEXT("SeedSupplyBlocked"));const bool Pass=Delivered&&Saved;UE_LOG(LogTemp,Display,TEXT("CF_SEED_FOOD_RECOVERY %s elapsed=%.3f"),Pass?TEXT("PASS"):TEXT("FAIL"),Now-Start);FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;}return true;
        }));return;
    }
    if(!Courier || !Farmer || !Crate || Courier->CarriedSeeds!=(Read?3:0) || Crate->StorageInventory->Count(EFrontierItem::Seeds)!=0 || Farmer->WorkSite->Crop->IsOccupied() || Farmer->Experience!=44){Fail(TEXT("source-state"));return;}
    if(!Read && !Crate->StorageInventory->AddItem(EFrontierItem::Seeds,3)){Fail(TEXT("fixture-seeds"));return;}
    UE_LOG(LogTemp,Display,TEXT("CF_SEED_PERSIST_SOURCE read=%d injected=%d id=%s carried=%d pos=%s"),Read,Read?0:3,*Courier->PersistentId.ToString(),Courier->CarriedSeeds,*Courier->GetActorLocation().ToString());
    struct FState{FVector Last;double LastTime=0,Start=0,NextLog=0,Travel=0;};
    auto S=MakeShared<FState>();S->Last=Courier->GetActorLocation();S->Start=S->LastTime=World->GetTimeSeconds();
    const auto Bag=Hero->Inventory->GetStacks();const float Health=Hero->Health;const auto HeroPosition=Hero->GetActorLocation();
    auto* Post=Courier->WorkSite.Get();auto* Farm=Farmer->WorkSite.Get();const FGuid PetId=Courier->PersistentId;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([World,Hero,Courier,Farmer,Crate,Post,Farm,PetId,Read,S,Bag,Health,HeroPosition](float)
    {
        const double Now=World->GetTimeSeconds(),Step=FVector::Dist(Courier->GetActorLocation(),S->Last);
        const bool Continuous=Step<=Courier->GetCharacterMovement()->GetMaxSpeed()*(Now-S->LastTime)+10;
        S->Travel+=Step;S->Last=Courier->GetActorLocation();S->LastTime=Now;
        const bool Valid=Continuous && Hero->Health==Health && Hero->GetActorLocation().Equals(HeroPosition,.1) && Hero->Inventory->GetStacks().OrderIndependentCompareEqual(Bag) && Courier->PersistentId==PetId && Courier->WorkSite==Post && Farmer->WorkSite==Farm && Courier->GetActorEnableCollision() && Courier->IsActorTickEnabled() && Courier->GetCharacterMovement()->MovementMode!=MOVE_None && Crate->StorageInventory->Count(EFrontierItem::Seeds)+Courier->CarriedSeeds+Farm->Crop->Seeds+(Farm->Crop->bPlanted?1:0)+Farmer->Experience-44==3;
        const bool Done=Read?(Courier->CarriedSeeds==0 && Farm->Crop->Seeds+(Farm->Crop->bPlanted?1:0)>0 && Courier->GetDistanceTo(Farm)<=220):(Courier->CarriedSeeds==3 && Crate->StorageInventory->Count(EFrontierItem::Seeds)==0 && S->Travel>100);
        if(!Valid || Done || Now-S->Start>60)
        {
            bool Pass=Valid && Done;const FString Slot=Pass?(Read?TEXT("SeedSupplyDone"):TEXT("SeedSupplyCarry")):TEXT("SeedSupplyBlocked");
            const bool Saved=Hero->SaveProgress(Slot);Pass &= Saved;
            if(Pass && !Read)
            {const auto* Disk=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0));const auto* Record=Disk?Disk->Creatures.FindByPredicate([PetId](const FCreatureSaveRecord& R){return R.Id==PetId;}):nullptr;Pass=Record && Record->SeedCargoVersion==1 && Record->CarriedSeeds==3 && Record->WorkSiteId==Post->PersistentId && Record->Transform.GetLocation().Equals(Courier->GetActorLocation(),.01);}
            UE_LOG(LogTemp,Display,TEXT("CF_SEED_PERSIST %s read=%d valid=%d carried=%d farm=%d planted=%d travel=%.1f elapsed=%.3f"),Pass?TEXT("PASS"):TEXT("FAIL"),Read,Valid,Courier->CarriedSeeds,Farm->Crop->Seeds,Farm->Crop->bPlanted,S->Travel,Now-S->Start);FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
        }
        if(Now>S->NextLog){S->NextLog=Now+10;UE_LOG(LogTemp,Display,TEXT("CF_SEED_PERSIST_STATE pos=%s box=%d carried=%d"),*S->Last.ToString(),Crate->StorageInventory->Count(EFrontierItem::Seeds),Courier->CarriedSeeds);}
        return true;
    }));
}
