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

// 真实家园副本上的显式存读夹具：仅首次投入3份测试种子，不代表生产来源。
void RunSeedSupplyPersistence(UWorld* World,ACFPlayerCharacter* Hero)
{
    auto Fail=[](const TCHAR* Why){UE_LOG(LogTemp,Error,TEXT("CF_SEED_PERSIST FAIL reason=%s"),Why);FPlatformMisc::RequestExitWithStatus(false,1);};
    const bool Read=FParse::Param(FCommandLine::Get(),TEXT("CFSeedSupplyRead"));
    if(!Hero->LoadProgress(TEXT("SeedSupplyInput"))){Fail(TEXT("load"));return;}
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
