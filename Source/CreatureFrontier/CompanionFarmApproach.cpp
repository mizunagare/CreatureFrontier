#include "CreatureCharacter.h"
#include "WorkStation.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Components/CapsuleComponent.h"

bool ACreatureCharacter::CanReachFarm(const AWorkStation* Site) const
{
    if(!IsValid(Site) || GetDistanceTo(Site)>220) return false;
    FHitResult Hit;FCollisionQueryParams Params(SCENE_QUERY_STAT(FarmApproachSight),false,this);
    return !GetWorld()->LineTraceSingleByChannel(Hit,GetActorLocation(),Site->GetActorLocation()+FVector(0,0,30),ECC_Visibility,Params) || Hit.GetActor()==Site;
}

void ACreatureCharacter::ApproachFarm(AWorkStation* Site)
{
    const double Now=GetWorld()->GetTimeSeconds();
    if(Now<NextNavigationUpdate)return;
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if(!Nav || !IsValid(Site)){StopNavigation();NextNavigationUpdate=Now+.4;return;}
    // 设施中心不是站立点；限制垂直投影，避免选择同一水平位置上方的屋顶。
    bool Found=false;FVector Best=FVector::ZeroVector;double BestLength=DBL_MAX;
    for(int32 Index=0;Index<8;++Index)
    {
        const double Angle=Index*PI/4;
        const FVector Candidate=Site->GetActorLocation()+FVector(FMath::Cos(Angle)*170,FMath::Sin(Angle)*170,0);
        FNavLocation Projected;
        if(!Nav->ProjectPointToNavigation(Candidate,Projected,FVector(35,35,80)))continue;
        const FVector Center=Projected.Location+FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        if(FVector::Dist(Center,Site->GetActorLocation())>210)continue;
        FHitResult Hit;FCollisionQueryParams Params(SCENE_QUERY_STAT(FarmStandSight),false,this);
        if(GetWorld()->LineTraceSingleByChannel(Hit,Center,Site->GetActorLocation()+FVector(0,0,30),ECC_Visibility,Params) && Hit.GetActor()!=Site)continue;
        const auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),GetActorLocation(),Projected.Location,this);
        if(!Path || !Path->IsValid() || Path->IsPartial())continue;
        const double Length=Path->GetPathLength();
        if(Length<BestLength){BestLength=Length;Best=Projected.Location;Found=true;}
    }
    if(Found)NavigateTo(Best,15);
    else {StopNavigation();NextNavigationUpdate=Now+.4;}
}
