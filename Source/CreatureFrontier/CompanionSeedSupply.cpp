#include "CreatureCharacter.h"
#include "CFPlayerCharacter.h"
#include "HomeModule.h"
#include "WorkStation.h"
#include "FarmCropComponent.h"
#include "InventoryComponent.h"
#include "CompanionCargoComponent.h"
#include "CompanionConstructionComponent.h"
#include "FrontierSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Controller.h"
#include "EngineUtils.h"
#include "CompanionApproach.h"

namespace
{
UInventoryComponent* SeedStock(AActor* Source)
{
    if(!IsValid(Source) || Source->IsActorBeingDestroyed())return nullptr;
    if(auto* Crate=Cast<AHomeModule>(Source))
        return Crate->GetKind()==EHomeModuleKind::Storage && !Crate->bPreview && !Crate->bStaged && !Crate->bUnderConstruction?Crate->StorageInventory.Get():nullptr;
    if(auto* Workshop=Cast<AWorkStation>(Source))
        return Workshop->bPlayerBuilt && Workshop->PersistentId.IsValid() && Workshop->RequiredSpecialty==EWorkSpecialty::Crafting?Workshop->CraftInventory.Get():nullptr;
    return nullptr;
}
bool SeedWorker(const ACreatureCharacter* Pet)
{
    const auto* Hero=Pet->Tamer?Cast<ACFPlayerCharacter>(Pet->Tamer->GetPawn()):nullptr;
    return IsInGameThread() && Pet->HasAuthority() && !Pet->IsActorBeingDestroyed() && Pet->Health>0 &&
        Pet->Disposition==ECreatureDisposition::Tamed && Hero && Hero->Health>0 &&
        Pet->Activity==ECreatureActivity::Work && IsValid(Pet->WorkSite) &&
        !Pet->WorkSite->IsActorBeingDestroyed() && Pet->WorkSite->RequiredSpecialty==EWorkSpecialty::Transport &&
        !Pet->GetFieldResource() && !Pet->Cargo->HasOrder() && !Pet->Construction->GetSite() &&
        Pet->CarriedFood==0 && Pet->Cargo->Count()==0 && !UGameplayStatics::IsGamePaused(Pet);
}
bool SeedReach(const ACreatureCharacter* Pet,const AActor* Target)
{
    if(!IsValid(Target) || Target->IsActorBeingDestroyed() || Target->GetWorld()!=Pet->GetWorld() || Pet->GetDistanceTo(Target)>220)return false;
    FHitResult Hit;FCollisionQueryParams Params(SCENE_QUERY_STAT(SeedSupplyReach),false,Pet);
    return !Pet->GetWorld()->LineTraceSingleByChannel(Hit,Pet->GetActorLocation(),Target->GetActorLocation()+FVector(0,0,30),ECC_Visibility,Params) || Hit.GetActor()==Target;
}
}

bool ACreatureCharacter::PickUpSeeds(AActor* Source,int32 Amount)
{
    auto* Stock=SeedStock(Source);
    if(!SeedWorker(this) || CarriedSeeds!=0 || Amount<1 || Amount>3 || !SeedReach(this,Source) ||
        !IsValid(Stock) || !UInventoryComponent::Validate(Stock->GetStacks()))return false;
    // 所有失败条件在扣物前检查；游戏线程内不跨回调提交两个状态。
    if(!Stock->Consume(EFrontierItem::Seeds,Amount))return false;
    CarriedSeeds=Amount;return true;
}

bool ACreatureCharacter::DeliverSeeds(AWorkStation* Farm)
{
    if(!SeedWorker(this) || CarriedSeeds<1 || CarriedSeeds>3 || !SeedReach(this,Farm) ||
        Farm->RequiredSpecialty!=EWorkSpecialty::Farming || !IsValid(Farm->Crop) || Farm->Crop->Seeds<0 || Farm->Crop->Seeds>=30)return false;
    const int32 Amount=FMath::Min(CarriedSeeds,30-Farm->Crop->Seeds);
    Farm->Crop->Seeds+=Amount;CarriedSeeds-=Amount;Farm->RefreshFarm();return true;
}

bool ACreatureCharacter::RecoverSeeds(ACFPlayerCharacter* Hero)
{
    // 取消、目标拆除或伙伴倒地后仍可取回；主人倒地及隔墙时拒绝。
    if(!IsInGameThread() || !HasAuthority() || IsActorBeingDestroyed() || !IsValid(Hero) || Hero->GetWorld()!=GetWorld() ||
        Hero->Health<=0 || Tamer!=Hero->GetController() || Disposition!=ECreatureDisposition::Tamed ||
        UGameplayStatics::IsGamePaused(this) || Hero->GetDistanceTo(this)>600 || CarriedSeeds<1 || CarriedSeeds>3)return false;
    FHitResult Hit;FCollisionQueryParams Params(SCENE_QUERY_STAT(SeedSupplyRecover),false,Hero);
    if(GetWorld()->LineTraceSingleByChannel(Hit,Hero->GetPawnViewLocation(),GetActorLocation(),ECC_Visibility,Params) && Hit.GetActor()!=this)return false;
    const int32 Amount=FMath::Min(CarriedSeeds,UInventoryComponent::StackLimit-Hero->Inventory->Count(EFrontierItem::Seeds));
    if(Amount<=0 || !Hero->Inventory->AddItem(EFrontierItem::Seeds,Amount))return false;
    CarriedSeeds-=Amount;Hero->Feedback=FString::Printf(TEXT("已取回%d份种子，剩余携种%d"),Amount,CarriedSeeds);return true;
}

bool ACreatureCharacter::ValidateSeedCargo(const FCreatureSaveRecord& Record)
{
    if(Record.SeedCargoVersion>1 || Record.CarriedSeeds<0 || Record.CarriedSeeds>3 ||
        (Record.SeedCargoVersion==0 && Record.CarriedSeeds!=0))return false;
    if(Record.CarriedSeeds==0)return true;
    if(!Record.bOwned || Record.Disposition!=ECreatureDisposition::Tamed || Record.CarriedFood!=0 ||
        Record.FieldResourceId.IsValid() || Record.DeliveryCrateId.IsValid() || Record.ConstructionModuleId.IsValid() || Record.bReturningCargo)return false;
    for(const auto& Entry:Record.CargoItems)if(Entry.Value!=0)return false;
    return true;
}

bool ACreatureCharacter::TickSeedSupply()
{
    if(!SeedWorker(this)){if(CarriedSeeds>0){StopNavigation();return true;}return false;}
    const double Now=GetWorld()->GetTimeSeconds();
    auto FailedRecently=[this,Now](const AActor* Actor){return FailedSeedSupplyTarget.Get()==Actor && Now<SeedSupplyRetryUntil;};
    AWorkStation* Farm=nullptr;float BestFarm=MAX_flt;
    for(TActorIterator<AWorkStation> It(GetWorld());It;++It)
        if(!It->IsActorBeingDestroyed() && !FailedRecently(*It) && It->RequiredSpecialty==EWorkSpecialty::Farming && It->GetDistanceTo(WorkSite)<=1000 &&
            (CarriedSeeds>0?It->Crop->Seeds<30:It->Crop->Seeds==0) && GetDistanceTo(*It)<BestFarm)
        {Farm=*It;BestFarm=GetDistanceTo(*It);}
    if(!Farm){SeedSupplyTarget.Reset();if(CarriedSeeds>0){StopNavigation();HaulRetry=2;return true;}return false;}
    AActor* Target=Farm;
    if(CarriedSeeds==0)
    {
        AActor* Source=nullptr;float Best=MAX_flt;
        for(TActorIterator<AHomeModule> It(GetWorld());It;++It)
            if(!It->IsActorBeingDestroyed() && !FailedRecently(*It) && !It->bPreview && !It->bStaged && !It->bUnderConstruction &&
                It->GetKind()==EHomeModuleKind::Storage && It->GetDistanceTo(WorkSite)<=1000 &&
                It->StorageInventory->Count(EFrontierItem::Seeds)>0 && GetDistanceTo(*It)<Best)
            {Source=*It;Best=GetDistanceTo(*It);}
        for(TActorIterator<AWorkStation> It(GetWorld());It;++It)
            if(auto* Stock=SeedStock(*It);Stock && !FailedRecently(*It) && It->GetDistanceTo(WorkSite)<=1000 &&
                Stock->Count(EFrontierItem::Seeds)>0 && GetDistanceTo(*It)<Best)
            {Source=*It;Best=GetDistanceTo(*It);}
        if(!Source){SeedSupplyTarget.Reset();return false;}
        Target=Source;
    }
    auto DeferTarget=[this,Target,Now]()
    {
        // 不可达种箱不能持续抢占正常粮运；未取货时允许原食物分支继续。
        // 已携种则保留货物并稍后重选农圃，绝不混装食物或丢弃种子。
        FailedSeedSupplyTarget=Target;SeedSupplyRetryUntil=Now+10;SeedSupplyTarget.Reset();StopNavigation();
        return CarriedSeeds>0;
    };
    if(SeedSupplyTarget.Get()!=Target){SeedSupplyTarget=Target;SeedSupplyExpires=Now+8;}
    if(GetDistanceTo(WorkSite)>1800 || Now>SeedSupplyExpires)return DeferTarget();
    auto* Stock=SeedStock(Target);
    const bool Done=CarriedSeeds>0?DeliverSeeds(Farm):(Stock && PickUpSeeds(Target,FMath::Min(3,Stock->Count(EFrontierItem::Seeds))));
    if(Done){SeedSupplyTarget.Reset();StopNavigation();return true;}
    if(GetWorld()->GetTimeSeconds()<NextNavigationUpdate)return true;
    FVector Goal;
    if(FindCompanionApproach(this,Target,Goal))NavigateTo(Goal,15);
    else return DeferTarget();
    return true;
}
