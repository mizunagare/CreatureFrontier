#include "CreatureCharacter.h"
#include "WorkStation.h"
#include "EngineUtils.h"
#include "CompanionCargoComponent.h"
#include "CFPlayerCharacter.h"
#include "InventoryComponent.h"
#include "GameFramework/Controller.h"
#include "CompanionApproach.h"

bool ACreatureCharacter::TickHauling(float DeltaSeconds)
{
    // 旧式食物搬运不得悄然覆盖或混合已保留的资源货物。
    if(Cargo->Count()>0) { StopNavigation(); return true; }
    auto Park=[this]()
    {
        FVector Waiting=WorkSite->GetActorLocation()+FVector(-180,0,0); Waiting.Z=GetActorLocation().Z;
        if(FVector::Dist2D(Waiting,GetActorLocation())>70) NavigateTo(Waiting,35); else StopNavigation();
    };
    WorkProgress=0;
    // 空手到岗等待不空耗体力；赶路、已有目标和携货仍按原规则消耗，不免费恢复。
    const bool WaitingEmpty=CarriedFood==0 && CarriedSeeds==0 && !HaulTarget.IsValid() && !SeedSupplyTarget.IsValid() &&
        GetVelocity().SizeSquared2D()<9 && FVector::Dist2D(GetActorLocation(),WorkSite->GetActorLocation()+FVector(-180,0,0))<=70;
    if(!WaitingEmpty)Stamina=FMath::Max(0.f,Stamina-DeltaSeconds*1.5f);
    if(Stamina<=0) { bResumeWorkAfterRest=true; HaulTarget.Reset(); SetActivity(ECreatureActivity::Rest); StopNavigation(); return true; }
    if(HaulPost.Get()!=WorkSite.Get()) { HaulPost=WorkSite.Get(); HaulTarget.Reset(); SeedSupplyTarget.Reset(); FailedFoodTargets.Empty(); HaulRetry=0; }
    HaulRetry=FMath::Max(0.f,HaulRetry-DeltaSeconds);
    if(HaulRetry>0) { Park(); return true; }
    if(CarriedFood==0 && TickSeedSupply()){HaulTarget.Reset();return true;}
    const double Now=GetWorld()->GetTimeSeconds();
    for(auto It=FailedFoodTargets.CreateIterator();It;++It)
        if(!It.Key().IsValid() || It.Value()<=Now)It.RemoveCurrent();
    auto* Hero=Tamer?Cast<ACFPlayerCharacter>(Tamer->GetPawn()):nullptr;
    int32 Meals=0,Residents=0;
    for(TActorIterator<AWorkStation> It(GetWorld());It;++It)
        if(!It->IsActorBeingDestroyed() && It->RequiredSpecialty==EWorkSpecialty::Feeding && It->GetDistanceTo(WorkSite)<=1000)Meals+=It->StoredFood;
    for(TActorIterator<ACreatureCharacter> It(GetWorld());It;++It)
        if(It->Health>0 && It->Disposition==ECreatureDisposition::Tamed && It->Tamer==Tamer && It->GetDistanceTo(WorkSite)<=1000)++Residents;
    // 每次选址和交货都重算口粮；不抽取食槽已有库存，不持久化过期的加工需求。
    const bool PantryReady=Meals>=FMath::Max(1,Residents);
    auto Demand=[&](AWorkStation* Target){return PantryReady?Target->CraftFoodDemand(Hero):0;};
    auto CanReceive=[&](AWorkStation* Target){return !Target->IsActorBeingDestroyed() && !FailedFoodTargets.Contains(Target) && Target->GetDistanceTo(WorkSite)<=1000 &&
        ((Target->RequiredSpecialty==EWorkSpecialty::Feeding && Target->StoredFood<30) || Demand(Target)>0);};
    auto* Site=HaulTarget.Get();
    if(Site && (GetWorld()->GetTimeSeconds()>HaulExpires || GetDistanceTo(WorkSite)>1800 ||
        (CarriedFood>0 ? !CanReceive(Site) : Site->RequiredSpecialty!=EWorkSpecialty::Farming || Site->StoredFood<=0)))
    {
        if(Site->RequiredSpecialty==EWorkSpecialty::Crafting && Now>HaulExpires)FailedFoodTargets.Add(Site,Now+10);
        HaulTarget.Reset(); HaulRetry=2; Park(); return true;
    }
    if(!Site)
    {
        bool HasDestination=false;
        for(TActorIterator<AWorkStation> It(GetWorld());It;++It)
            if(CanReceive(*It)) { HasDestination=true; break; }
        if(!HasDestination) { HaulRetry=2; Park(); return true; }
        float Best=MAX_flt;
        // 口粮充足时优先当前加工缺口，剩余货物仍送食槽。
        if(CarriedFood>0)
            for(TActorIterator<AWorkStation> It(GetWorld());It;++It)
                if(CanReceive(*It) && Demand(*It)>0 && GetDistanceTo(*It)<Best){Site=*It;Best=GetDistanceTo(*It);}
        if(!Site)for(TActorIterator<AWorkStation> It(GetWorld());It;++It)
        {
            const bool Match=CarriedFood>0 ? It->RequiredSpecialty==EWorkSpecialty::Feeding && It->StoredFood<30 : It->RequiredSpecialty==EWorkSpecialty::Farming && It->StoredFood>0;
            if(Match && It->GetDistanceTo(WorkSite)<=1000 && GetDistanceTo(*It)<Best) { Site=*It; Best=GetDistanceTo(*It); }
        }
        if(!Site) { HaulRetry=2; Park(); return true; }
        HaulTarget=Site; HaulExpires=GetWorld()->GetTimeSeconds()+8;
    }
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(HaulingSight),false,this);
    const bool Visible=!GetWorld()->LineTraceSingleByChannel(Hit,GetActorLocation(),Site->GetActorLocation(),ECC_Visibility,Params) || Hit.GetActor()==Site;
    if(GetDistanceTo(Site)<=220 && Visible)
    {
        if(CarriedFood>0)
        {
            if(Site->RequiredSpecialty==EWorkSpecialty::Crafting)
            {
                const int32 Amount=FMath::Min(CarriedFood,Demand(Site));
                if(Amount>0 && Site->CraftInventory->AddItem(EFrontierItem::Food,Amount))CarriedFood-=Amount;
            }
            else
            {
                const int32 Amount=FMath::Min(CarriedFood,30-Site->StoredFood);
                if(Amount>0 && Site->AdjustFoodStock(Amount)) { CarriedFood-=Amount; AddExperience(1); }
            }
        }
        else
        {
            const int32 Amount=FMath::Min(3,Site->StoredFood);
            if(Amount>0 && Site->AdjustFoodStock(-Amount)) CarriedFood+=Amount;
        }
        HaulTarget.Reset(); StopNavigation(); return true;
    }
    if(Site->RequiredSpecialty==EWorkSpecialty::Crafting)
    {
        // 沿用种子补给的真实可达侧位；失败目标暂避十秒，货物继续尝试其他设施或食槽。
        if(Now<NextNavigationUpdate)return true;
        FVector Goal;
        if(FindCompanionApproach(this,Site,Goal))NavigateTo(Goal,15);
        else {FailedFoodTargets.Add(Site,Now+10);HaulTarget.Reset();StopNavigation();}
        return true;
    }
    FVector Direction=GetActorLocation()-Site->GetActorLocation(); Direction.Z=0;
    if(!Direction.Normalize()) Direction=FVector(1,0,0);
    FVector Approach=Site->GetActorLocation()+Direction*150; Approach.Z=GetActorLocation().Z;
    NavigateTo(Approach,35); return true;
}
