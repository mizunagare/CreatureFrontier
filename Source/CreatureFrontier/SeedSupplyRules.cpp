#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "HomeModule.h"
#include "HomeModuleDefinition.h"
#include "WorkStation.h"
#include "FarmCropComponent.h"
#include "InventoryComponent.h"
#include "FrontierSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/Ticker.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "GameFramework/CharacterMovementComponent.h"

// 显式隔离事务夹具，不写存档，不冒充正常来源或实际运输路线。
void RunSeedSupplyRules(UWorld* World,ACFPlayerCharacter* Hero)
{
    const bool Blocked=FParse::Param(FCommandLine::Get(),TEXT("CFSeedSupplyBlocked"));
    const bool Route=Blocked || FParse::Param(FCommandLine::Get(),TEXT("CFSeedSupplyRoute"));
    auto* Pet=World->SpawnActor<ACreatureCharacter>(FVector(-1100,-1500,40),FRotator::ZeroRotator);
    auto* Post=World->SpawnActor<AWorkStation>(FVector(-1100,-1750,0),FRotator::ZeroRotator);
    auto* Farm=World->SpawnActor<AWorkStation>(FVector(Route?-500:-950,-1500,0),FRotator::ZeroRotator);
    auto* Crate=World->SpawnActor<AHomeModule>(FVector(Route?-1600:-1250,-1500,Blocked?700:0),FRotator::ZeroRotator);
    Post->RequiredSpecialty=EWorkSpecialty::Transport;Farm->RequiredSpecialty=EWorkSpecialty::Farming;
    Crate->Definition=NewObject<UHomeModuleDefinition>(Crate);Crate->Definition->Kind=EHomeModuleKind::Storage;
    Pet->Tamer=Hero->GetController();Pet->Disposition=ECreatureDisposition::Tamed;Pet->Activity=ECreatureActivity::Work;Pet->WorkSite=Post;
    if(Blocked)
    {
        // 高处无通路种箱与地面粮运并存；观察期间不移动设施或改库存。
        Crate->StorageInventory->Restore({{EFrontierItem::Seeds,3}});Crate->OnConstruction(Crate->GetActorTransform());
        Farm->StoredFood=3;Farm->RefreshFarm();
        auto* Trough=World->SpawnActor<AWorkStation>(FVector(-1100,-1100,0),FRotator::ZeroRotator);Trough->RequiredSpecialty=EWorkSpecialty::Feeding;Trough->RefreshFarm();
        const double Start=World->GetTimeSeconds();auto Picked=MakeShared<bool>(false);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([World,Pet,Post,Farm,Crate,Trough,Start,Picked](float)
        {
            *Picked |= Pet->CarriedFood>0;
            const bool Invariant=Pet->WorkSite==Post && Pet->CarriedSeeds==0 && Crate->StorageInventory->Count(EFrontierItem::Seeds)==3 && Farm->Crop->Seeds==0 && Farm->StoredFood+Pet->CarriedFood+Trough->StoredFood==3 && Pet->GetActorEnableCollision();
            const bool Delivered=*Picked && Trough->StoredFood==3 && Pet->CarriedFood==0 && Farm->StoredFood==0;
            if(!Invariant || Delivered || World->GetTimeSeconds()-Start>25)
            {const bool Pass=Invariant && Delivered;UE_LOG(LogTemp,Display,TEXT("CF_SEED_BLOCKED %s invariant=%d picked=%d food=%d seedBox=%d elapsed=%.3f"),Pass?TEXT("PASS"):TEXT("FAIL"),Invariant,*Picked,Trough->StoredFood,Crate->StorageInventory->Count(EFrontierItem::Seeds),World->GetTimeSeconds()-Start);FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;}
            return true;
        }));return;
    }
    if(Route)
    {
        // 只在夹具初始化时投入3种；观察期间完全交给普通AI，不补值、不瞬移。
        Crate->StorageInventory->Restore({{EFrontierItem::Seeds,3}});Crate->OnConstruction(Crate->GetActorTransform());Farm->RefreshFarm();
        struct FRouteState{FVector Last;double LastTime=0,Start=0,NextLog=0,Travel=0;bool Picked=false;};
        auto S=MakeShared<FRouteState>();S->Last=Pet->GetActorLocation();S->Start=World->GetTimeSeconds();S->LastTime=S->Start;
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([World,Pet,Post,Farm,Crate,S](float)
        {
            const double Now=World->GetTimeSeconds(),Step=FVector::Dist(S->Last,Pet->GetActorLocation());
            const bool Continuous=Step<=Pet->GetCharacterMovement()->GetMaxSpeed()*(Now-S->LastTime)+10;
            S->Travel+=Step;S->Last=Pet->GetActorLocation();S->LastTime=Now;
            if(Pet->CarriedSeeds>0 && !S->Picked){S->Picked=true;UE_LOG(LogTemp,Display,TEXT("CF_SEED_ROUTE_PICKUP pos=%s carried=%d box=%d"),*S->Last.ToString(),Pet->CarriedSeeds,Crate->StorageInventory->Count(EFrontierItem::Seeds));}
            const bool Invariant=Continuous && Pet->WorkSite==Post && Pet->GetActorEnableCollision() && Pet->IsActorTickEnabled() && Pet->GetCharacterMovement()->MovementMode!=MOVE_None && Pet->CarriedFood==0 && Farm->StoredFood==0 && !Farm->Crop->bPlanted && Crate->StorageInventory->Count(EFrontierItem::Seeds)+Pet->CarriedSeeds+Farm->Crop->Seeds==3;
            const bool Delivered=S->Picked && Pet->CarriedSeeds==0 && Farm->Crop->Seeds==3 && Crate->StorageInventory->Count(EFrontierItem::Seeds)==0 && S->Travel>800 && Pet->GetDistanceTo(Farm)<=220;
            if(!Invariant || Delivered || Now-S->Start>45)
            {const bool Pass=Invariant && Delivered;UE_LOG(LogTemp,Display,TEXT("CF_SEED_ROUTE %s invariant=%d picked=%d farm=%d carried=%d travel=%.1f elapsed=%.3f pos=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Invariant,S->Picked,Farm->Crop->Seeds,Pet->CarriedSeeds,S->Travel,Now-S->Start,*S->Last.ToString());FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;}
            if(Now>S->NextLog){S->NextLog=Now+5;UE_LOG(LogTemp,Display,TEXT("CF_SEED_ROUTE_STATE pos=%s carried=%d farm=%d"),*S->Last.ToString(),Pet->CarriedSeeds,Farm->Crop->Seeds);}
            return true;
        }));
        return;
    }
    Crate->StorageInventory->Restore({{EFrontierItem::Seeds,4}});
    const bool Range=!Pet->PickUpSeeds(Crate,0) && !Pet->PickUpSeeds(Crate,4) && Crate->StorageInventory->Count(EFrontierItem::Seeds)==4;
    UGameplayStatics::SetGamePaused(World,true);const bool Paused=!Pet->PickUpSeeds(Crate,3);UGameplayStatics::SetGamePaused(World,false);
    Pet->CarriedFood=1;const bool Mixed=!Pet->PickUpSeeds(Crate,3);Pet->CarriedFood=0;
    const bool Pickup=Pet->PickUpSeeds(Crate,3) && Pet->CarriedSeeds==3 && Crate->StorageInventory->Count(EFrontierItem::Seeds)==1 && !Pet->PickUpSeeds(Crate,1);
    Farm->Crop->Seeds=30;const bool Full=!Pet->DeliverSeeds(Farm) && Pet->CarriedSeeds==3;
    Farm->Crop->Seeds=29;const bool Partial=Pet->DeliverSeeds(Farm) && Pet->CarriedSeeds==2 && Farm->Crop->Seeds==30;
    Pet->SetActivity(ECreatureActivity::Follow);const bool Cancel=!Pet->DeliverSeeds(Farm) && Pet->CarriedSeeds==2;
    FCreatureSaveRecord Record;const bool Legacy=ACreatureCharacter::ValidateSeedCargo(Record);
    Record.SeedCargoVersion=1;Record.CarriedSeeds=2;Record.bOwned=true;Record.Disposition=ECreatureDisposition::Tamed;
    const bool Retained=ACreatureCharacter::ValidateSeedCargo(Record);
    bool Reject=true;auto Bad=Record;Bad.CarriedSeeds=4;Reject &= !ACreatureCharacter::ValidateSeedCargo(Bad);
    Bad=Record;Bad.SeedCargoVersion=0;Reject &= !ACreatureCharacter::ValidateSeedCargo(Bad);
    Bad=Record;Bad.SeedCargoVersion=2;Reject &= !ACreatureCharacter::ValidateSeedCargo(Bad);
    Bad=Record;Bad.bOwned=false;Reject &= !ACreatureCharacter::ValidateSeedCargo(Bad);
    Bad=Record;Bad.CarriedFood=1;Reject &= !ACreatureCharacter::ValidateSeedCargo(Bad);
    Bad=Record;Bad.CargoItems.Add(EFrontierItem::Material,1);Reject &= !ACreatureCharacter::ValidateSeedCargo(Bad);
    Bad=Record;Bad.DeliveryCrateId=FGuid::NewGuid();Reject &= !ACreatureCharacter::ValidateSeedCargo(Bad);
    // 同步事务夹具重新布置主人；不用于证明实际行走或正常物资来源。
    Hero->SetActorLocation(Pet->GetActorLocation()+FVector(0,-150,60));
    Hero->Inventory->Restore({{EFrontierItem::Seeds,UInventoryComponent::StackLimit}});
    const bool RecoveryFull=!Hero->WithdrawCompanionCargo(Pet) && Pet->CarriedSeeds==2 && Hero->Inventory->Count(EFrontierItem::Seeds)==UInventoryComponent::StackLimit;
    Hero->Inventory->Consume(EFrontierItem::Seeds,1);
    const bool RecoveryPartial=Hero->WithdrawCompanionCargo(Pet) && Pet->CarriedSeeds==1 && Hero->Inventory->Count(EFrontierItem::Seeds)==UInventoryComponent::StackLimit;
    Hero->Inventory->Consume(EFrontierItem::Seeds,1);
    UGameplayStatics::SetGamePaused(World,true);
    const bool RecoveryPaused=!Hero->WithdrawCompanionCargo(Pet) && Pet->CarriedSeeds==1;
    UGameplayStatics::SetGamePaused(World,false);
    Farm->Destroy();Post->Destroy();Pet->Health=0;
    const bool RecoveryDowned=Hero->WithdrawCompanionCargo(Pet) && Pet->CarriedSeeds==0 && Hero->Inventory->Count(EFrontierItem::Seeds)==UInventoryComponent::StackLimit && !Hero->WithdrawCompanionCargo(Pet);
    const bool Recovery=RecoveryFull && RecoveryPartial && RecoveryPaused && RecoveryDowned;
    UE_LOG(LogTemp,Display,TEXT("CF_SEED_RECOVERY %s full=%d partial=%d paused=%d downedAfterDestroy=%d"),Recovery?TEXT("PASS"):TEXT("FAIL"),RecoveryFull,RecoveryPartial,RecoveryPaused,RecoveryDowned);
    const bool Pass=Range && Paused && Mixed && Pickup && Full && Partial && Cancel && Legacy && Retained && Reject && Recovery;
    UE_LOG(LogTemp,Display,TEXT("CF_SEED_SUPPLY_RULES %s range=%d paused=%d mixed=%d pickup=%d full=%d partial=%d cancel=%d legacy=%d retained=%d reject=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Range,Paused,Mixed,Pickup,Full,Partial,Cancel,Legacy,Retained,Reject);
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}
