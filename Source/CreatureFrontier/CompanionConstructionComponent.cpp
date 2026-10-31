#include "CompanionConstructionComponent.h"
#include "CompanionApproach.h"
#include "CreatureCharacter.h"
#include "HomeModule.h"
#include "HomeBuildingComponent.h"
#include "CFPlayerCharacter.h"
#include "CompanionCargoComponent.h"
#include "InventoryComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

AHomeModule* UCompanionConstructionComponent::GetSite() const
{auto* Site=Goal.Get();return IsValid(Site) && !Site->IsActorBeingDestroyed() && Site->bUnderConstruction?Site:nullptr;}
void UCompanionConstructionComponent::Cancel(){Goal.Reset();ApproachTarget.Reset();NextSearch=0;bHasApproach=false;bWorking=false;}
void UCompanionConstructionComponent::Restore(AHomeModule* Site){Cancel();Goal=Site;}
bool UCompanionConstructionComponent::Assign(AHomeModule* Site,AHomeModule* Crate,ACFPlayerCharacter* Hero)
{
    auto* Pet=Cast<ACreatureCharacter>(GetOwner());
    if(!Pet || !Hero || !Pet->HasAuthority() || Pet->Health<=0 || Hero->Health<=0 || UGameplayStatics::IsGamePaused(this) || Pet->Tamer!=Hero->GetController() ||
        Pet->Disposition!=ECreatureDisposition::Tamed || Pet->CarriedFood>0 || (Pet->Cargo->Count()>0 && GetSite()!=Site) || !IsValid(Site) || Site->IsActorBeingDestroyed() ||
        !Site->bUnderConstruction || !Site->Definition || !Site->Definition->IsValidDefinition() || !Site->PersistentId.IsValid() || Site->bPreview || Site->bStaged || !IsValid(Crate) || !Crate->PersistentId.IsValid() || Crate->IsActorBeingDestroyed() || Crate->GetKind()!=EHomeModuleKind::Storage ||
        Crate->bPreview || Crate->bStaged || Site->GetWorld()!=GetWorld() || Crate->GetWorld()!=GetWorld())return false;
    for(TActorIterator<ACreatureCharacter> It(GetWorld());It;++It)if(*It!=Pet && It->Construction->GetSite()==Site)return false;
    Pet->CancelFieldResource();Pet->WorkSite=nullptr;Pet->bResumeWorkAfterRest=false;Pet->ClearCombatOrders();Pet->StopNavigation();
    Site->ConstructionCrateId=Crate->PersistentId;Goal=Site;Pet->SetActivity(ECreatureActivity::Work);return true;
}
bool UCompanionConstructionComponent::Reach(AActor* Target) const
{
    auto* Pet=Cast<ACreatureCharacter>(GetOwner());if(!Pet || !IsValid(Target))return false;
    const auto* Site=Cast<AHomeModule>(Target);const auto Point=Site && Site->bUnderConstruction?Site->ConstructionPoint(Pet->GetActorLocation()):Target->GetActorLocation();
    if(FVector::Dist(Pet->GetActorLocation(),Point)>220)return false;
    FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(ConstructionReach),false,Pet);
    return !GetWorld()->LineTraceSingleByChannel(Hit,Pet->GetActorLocation(),Point+FVector(0,0,40),ECC_Visibility,Query) || Hit.GetActor()==Target;
}
void UCompanionConstructionComponent::Approach(AActor* Target)
{
    auto* Pet=CastChecked<ACreatureCharacter>(GetOwner());const double Now=GetWorld()->GetTimeSeconds();
    if(ApproachTarget!=Target || Now>=NextSearch)
    {
        ++ApproachSearchCount;
        NextSearch=Now+.4;ApproachTarget=Target;
        // 无路结果也缓存至下一次重试，避免清空目标后每帧重新搜索。
        bHasApproach=FindCompanionApproach(Pet,Target,ApproachPoint);
        if(!bHasApproach){Pet->StopNavigation();return;}
    }
    if(bHasApproach && ApproachTarget.IsValid())Pet->NavigateTo(ApproachPoint,15);
}
bool UCompanionConstructionComponent::TickWork(float DeltaSeconds,ACFPlayerCharacter* Hero)
{
    bWorking=false;
    auto* Pet=CastChecked<ACreatureCharacter>(GetOwner());auto* Site=GetSite();
    if(!Site || (Pet->Activity!=ECreatureActivity::Work && !(Pet->Activity==ECreatureActivity::Rest && Pet->bResumeWorkAfterRest))) {Cancel();return false;}
    if(!FMath::IsFinite(DeltaSeconds) || DeltaSeconds<0 || UGameplayStatics::IsGamePaused(this) || !Hero || Hero->Health<=0 || Pet->Health<=0)return true;
    if(Pet->Activity==ECreatureActivity::Rest)
    {
        Pet->StopNavigation();Pet->Stamina=FMath::Min(100.f,Pet->Stamina+DeltaSeconds*4);
        if(Pet->Stamina>=80 && Pet->Nutrition>=20 && Pet->Health>=Pet->GetMaxHealth()*.8f){Pet->bResumeWorkAfterRest=false;Pet->SetActivity(ECreatureActivity::Work);}
        return true;
    }
    if(Pet->Nutrition<=0 || Pet->Stamina<=0){Pet->bResumeWorkAfterRest=true;Pet->SetActivity(ECreatureActivity::Rest);Pet->StopNavigation();return true;}
    const int32 Missing=Site->Definition->MaterialCost-Site->StorageInventory->Count(EFrontierItem::Material);
    AHomeModule* Crate=nullptr;
    if(Missing>0 && Pet->Cargo->Count()==0)
    {
        for(TActorIterator<AHomeModule> It(GetWorld());It;++It)if(It->PersistentId==Site->ConstructionCrateId && It->GetKind()==EHomeModuleKind::Storage && !It->bPreview && !It->bStaged)Crate=*It;
        if(!Crate)Site->ConstructionCrateId.Invalidate();
        // 无料可搬时保留原任务，不空耗施工体力，也不免费恢复体力。
        // 补料后下一次正常更新继续原任务；已有携物仍须交付，不能误判为缺料。
        if(!Crate || Crate->StorageInventory->Count(EFrontierItem::Material)==0)
        {
            // 主人正面近距离通行时复用原胶囊扫掠与完整路径让路，不把缺料等待变成路障。
            // 只在空手缺料分支允许；休息、携料、施工和需求优先级均不改变。
            FVector Yield;if(Pet->FindOwnerYield(Yield,true))Pet->NavigateTo(Yield,12);else Pet->StopNavigation();
            return true;
        }
    }
    Pet->Stamina=FMath::Max(0.f,Pet->Stamina-DeltaSeconds*1.5f);
    if(Pet->Stamina<=0){Pet->bResumeWorkAfterRest=true;Pet->SetActivity(ECreatureActivity::Rest);Pet->StopNavigation();return true;}
    if(Pet->Cargo->Count()>0)
    {
        if(!Reach(Site)){Approach(Site);return true;}Pet->StopNavigation();
        const int32 Amount=FMath::Min(Missing,Pet->CargoInventory->Count(EFrontierItem::Material));
        if(Amount>0)Pet->CargoInventory->TransferTo(Site->StorageInventory,{{EFrontierItem::Material,Amount}});
        return true;
    }
    if(Missing>0)
    {
        if(!Reach(Crate)){Approach(Crate);return true;}Pet->StopNavigation();
        const int32 Amount=FMath::Min3(Missing,UCompanionCargoComponent::Capacity,Crate->StorageInventory->Count(EFrontierItem::Material));
        Crate->StorageInventory->TransferTo(Pet->CargoInventory,{{EFrontierItem::Material,Amount}});return true;
    }
    if(!Reach(Site) || !Site->ClearsCompletedPawn(Pet,Pet->GetActorLocation())){Approach(Site);return true;}Pet->StopNavigation();bWorking=true;Site->ConstructionWork=FMath::Min(6.f,Site->ConstructionWork+DeltaSeconds*(Pet->Nutrition<20?.5f:1.f));
    if(Site->ConstructionWork>=6 && Hero->HomeBuilding->CompleteConstruction(Site)){Cancel();Pet->bResumeWorkAfterRest=false;Pet->SetActivity(ECreatureActivity::Follow);}
    return true;
}
