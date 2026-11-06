#include "CreatureCharacter.h"
#include "WorkStation.h"
#include "EngineUtils.h"
#include "CompanionCargoComponent.h"

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
    if(HaulPost.Get()!=WorkSite.Get()) { HaulPost=WorkSite.Get(); HaulTarget.Reset(); SeedSupplyTarget.Reset(); HaulRetry=0; }
    HaulRetry=FMath::Max(0.f,HaulRetry-DeltaSeconds);
    if(HaulRetry>0) { Park(); return true; }
    if(CarriedFood==0 && TickSeedSupply()){HaulTarget.Reset();return true;}
    auto* Site=HaulTarget.Get();
    if(Site && (GetWorld()->GetTimeSeconds()>HaulExpires || GetDistanceTo(WorkSite)>1800 ||
        (CarriedFood>0 ? Site->RequiredSpecialty!=EWorkSpecialty::Feeding || Site->StoredFood>=30 : Site->RequiredSpecialty!=EWorkSpecialty::Farming || Site->StoredFood<=0)))
    { HaulTarget.Reset(); HaulRetry=2; Park(); return true; }
    if(!Site)
    {
        bool HasDestination=false;
        for(TActorIterator<AWorkStation> It(GetWorld());It;++It)
            if(It->RequiredSpecialty==EWorkSpecialty::Feeding && It->StoredFood<30 && It->GetDistanceTo(WorkSite)<=1000) { HasDestination=true; break; }
        if(!HasDestination) { HaulRetry=2; Park(); return true; }
        float Best=MAX_flt;
        for(TActorIterator<AWorkStation> It(GetWorld());It;++It)
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
            const int32 Amount=FMath::Min(CarriedFood,30-Site->StoredFood);
            if(Amount>0 && Site->AdjustFoodStock(Amount)) { CarriedFood-=Amount; AddExperience(1); }
        }
        else
        {
            const int32 Amount=FMath::Min(3,Site->StoredFood);
            if(Amount>0 && Site->AdjustFoodStock(-Amount)) CarriedFood+=Amount;
        }
        HaulTarget.Reset(); StopNavigation(); return true;
    }
    FVector Direction=GetActorLocation()-Site->GetActorLocation(); Direction.Z=0;
    if(!Direction.Normalize()) Direction=FVector(1,0,0);
    FVector Approach=Site->GetActorLocation()+Direction*150; Approach.Z=GetActorLocation().Z;
    NavigateTo(Approach,35); return true;
}
