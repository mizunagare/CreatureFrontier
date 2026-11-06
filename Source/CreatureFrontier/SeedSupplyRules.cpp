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
    const bool Workshop=FParse::Param(FCommandLine::Get(),TEXT("CFWorkshopSeedRoute"));
    const bool Route=Workshop || Blocked || FParse::Param(FCommandLine::Get(),TEXT("CFSeedSupplyRoute"));
    auto* Pet=World->SpawnActor<ACreatureCharacter>(FVector(-1100,-1500,40),FRotator::ZeroRotator);
    auto* Post=World->SpawnActor<AWorkStation>(FVector(-1100,-1750,0),FRotator::ZeroRotator);
    auto* Farm=World->SpawnActor<AWorkStation>(FVector(Route?-500:-950,-1500,0),FRotator::ZeroRotator);
    auto* Crate=World->SpawnActor<AHomeModule>(FVector(Workshop?-2100:Route?-1600:-1250,-1500,Blocked?700:0),FRotator::ZeroRotator);
    Post->RequiredSpecialty=EWorkSpecialty::Transport;Farm->RequiredSpecialty=EWorkSpecialty::Farming;
    Crate->Definition=NewObject<UHomeModuleDefinition>(Crate);Crate->Definition->Kind=EHomeModuleKind::Storage;
    Pet->Tamer=Hero->GetController();Pet->Disposition=ECreatureDisposition::Tamed;Pet->Activity=ECreatureActivity::Work;Pet->WorkSite=Post;
    if(FParse::Param(FCommandLine::Get(),TEXT("CFTransportWaiting")))
    {
        // 空箱空圃的隔离等待场景；普通AI自行走到岗位，不冻结模拟或移动伙伴。
        Crate->OnConstruction(Crate->GetActorTransform());
        Pet->Stamina=60;Pet->Nutrition=100;
        struct FWaitingState{double Start=0,Settled=-1,NextLog=0,SupplyAt=0,LastTime=0,Travel=0;float Stamina=0,Nutrition=0,SupplyStamina=0;bool Supplied=false,Picked=false;FVector Last;};
        auto S=MakeShared<FWaitingState>();S->Start=World->GetTimeSeconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([World,Pet,Post,Farm,Crate,S](float)
        {
            const double Now=World->GetTimeSeconds();
            if(S->Supplied)
            {
                const double Step=FVector::Dist(S->Last,Pet->GetActorLocation());
                const bool Continuous=Step<=Pet->GetCharacterMovement()->GetMaxSpeed()*(Now-S->LastTime)+10;
                S->Travel+=Step;S->Last=Pet->GetActorLocation();S->LastTime=Now;S->Picked |= Pet->CarriedSeeds>0;
                const bool Intact=Continuous && Pet->WorkSite==Post && Pet->Activity==ECreatureActivity::Work && Pet->GetActorEnableCollision() && Pet->CarriedFood==0 && Farm->StoredFood==0 && !Farm->Crop->bPlanted && Crate->StorageInventory->Count(EFrontierItem::Seeds)+Pet->CarriedSeeds+Farm->Crop->Seeds==3;
                const bool Delivered=S->Picked && Farm->Crop->Seeds==3 && Pet->CarriedSeeds==0 && S->Travel>50 && Pet->Stamina<S->SupplyStamina-.1f;
                if(!Intact || Delivered || Now-S->SupplyAt>25)
                {
                    const bool Pass=Intact && Delivered;
                    UE_LOG(LogTemp,Display,TEXT("CF_TRANSPORT_RESUPPLY %s invariant=%d picked=%d farmSeeds=%d travel=%.2f staminaBefore=%.3f staminaAfter=%.3f"),Pass?TEXT("PASS"):TEXT("FAIL"),Intact,S->Picked,Farm->Crop->Seeds,S->Travel,S->SupplyStamina,Pet->Stamina);
                    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
                }
                return true;
            }
            const bool Stationary=Pet->GetVelocity().Size2D()<3 && FVector::Dist2D(Pet->GetActorLocation(),Post->GetActorLocation()+FVector(-180,0,0))<=70;
            if(Now>=S->NextLog)
            {
                S->NextLog=Now+5;FHitResult Floor;FCollisionQueryParams Query(SCENE_QUERY_STAT(WaitingFloor),false,Pet);
                World->LineTraceSingleByChannel(Floor,Pet->GetActorLocation(),Pet->GetActorLocation()-FVector(0,0,400),ECC_Visibility,Query);
                UE_LOG(LogTemp,Display,TEXT("CF_TRANSPORT_WAIT_STATE pos=%s velocity=%s distance=%.2f stamina=%.3f floor=%s floorZ=%.2f crate=%s farm=%s post=%s"),*Pet->GetActorLocation().ToString(),*Pet->GetVelocity().ToString(),FVector::Dist2D(Pet->GetActorLocation(),Post->GetActorLocation()+FVector(-180,0,0)),Pet->Stamina,*GetNameSafe(Floor.GetActor()),Floor.ImpactPoint.Z,*Crate->GetName(),*Farm->GetName(),*Post->GetName());
            }
            const bool Intact=Pet->Activity==ECreatureActivity::Work && Pet->WorkSite==Post && Pet->GetActorEnableCollision() && Pet->CarriedSeeds==0 && Pet->CarriedFood==0 && Farm->StoredFood==0 && Crate->StorageInventory->Count(EFrontierItem::Seeds)==0;
            if(S->Settled<0 && Stationary){S->Settled=Now;S->Stamina=Pet->Stamina;S->Nutrition=Pet->Nutrition;}
            if(!Intact || Now-S->Start>20 || (S->Settled>=0 && Now-S->Settled>=3))
            {
                const bool Pass=Intact && Stationary && S->Settled>=0 && FMath::IsNearlyEqual(Pet->Stamina,S->Stamina,.05f) && Pet->Nutrition<S->Nutrition;
                UE_LOG(LogTemp,Display,TEXT("CF_TRANSPORT_WAIT %s intact=%d stationary=%d staminaBefore=%.3f staminaAfter=%.3f nutritionBefore=%.3f nutritionAfter=%.3f"),Pass?TEXT("PASS"):TEXT("FAIL"),Intact,Stationary,S->Stamina,Pet->Stamina,S->Nutrition,Pet->Nutrition);
                if(Pass)
                {
                    // 仅为隔离复工测试投入一次3种，后续完全由普通AI搬运；不冒称玩家制种来源。
                    if(!Crate->StorageInventory->AddItem(EFrontierItem::Seeds,3)){FPlatformMisc::RequestExitWithStatus(false,1);return false;}
                    S->Supplied=true;S->SupplyAt=Now;S->SupplyStamina=Pet->Stamina;S->Last=Pet->GetActorLocation();S->LastTime=Now;return true;
                }
                FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
            }
            return true;
        }));return;
    }
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
        Crate->OnConstruction(Crate->GetActorTransform());Farm->RefreshFarm();
        UInventoryComponent* RouteStock=Crate->StorageInventory;
        if(Workshop)
        {
            auto* Source=World->SpawnActor<AWorkStation>(FVector(-1600,-1500,0),FRotator::ZeroRotator);
            Source->bPlayerBuilt=true;Source->PersistentId=FGuid::NewGuid();Source->RequiredSpecialty=EWorkSpecialty::Crafting;RouteStock=Source->CraftInventory;
            UE_LOG(LogTemp,Display,TEXT("CF_WORKSHOP_SEED_SOURCE fixture=1 seeds=3 actualCraft=0"));
        }
        RouteStock->Restore({{EFrontierItem::Seeds,3}});
        struct FRouteState{FVector Last;double LastTime=0,Start=0,NextLog=0,Travel=0;bool Picked=false;};
        auto S=MakeShared<FRouteState>();S->Last=Pet->GetActorLocation();S->Start=World->GetTimeSeconds();S->LastTime=S->Start;
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([World,Pet,Post,Farm,RouteStock,S](float)
        {
            const double Now=World->GetTimeSeconds(),Step=FVector::Dist(S->Last,Pet->GetActorLocation());
            const bool Continuous=Step<=Pet->GetCharacterMovement()->GetMaxSpeed()*(Now-S->LastTime)+10;
            S->Travel+=Step;S->Last=Pet->GetActorLocation();S->LastTime=Now;
            if(Pet->CarriedSeeds>0 && !S->Picked){S->Picked=true;UE_LOG(LogTemp,Display,TEXT("CF_SEED_ROUTE_PICKUP pos=%s carried=%d sourceStock=%d"),*S->Last.ToString(),Pet->CarriedSeeds,RouteStock->Count(EFrontierItem::Seeds));}
            const bool Invariant=Continuous && Pet->WorkSite==Post && Pet->GetActorEnableCollision() && Pet->IsActorTickEnabled() && Pet->GetCharacterMovement()->MovementMode!=MOVE_None && Pet->CarriedFood==0 && Farm->StoredFood==0 && !Farm->Crop->bPlanted && RouteStock->Count(EFrontierItem::Seeds)+Pet->CarriedSeeds+Farm->Crop->Seeds==3;
            const bool Delivered=S->Picked && Pet->CarriedSeeds==0 && Farm->Crop->Seeds==3 && RouteStock->Count(EFrontierItem::Seeds)==0 && S->Travel>800 && Pet->GetDistanceTo(Farm)<=220;
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
