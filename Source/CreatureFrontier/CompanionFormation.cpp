#include "CreatureCharacter.h"
#include "CFPlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace FrontierFollowPath
{
const ACharacter* Blocker(const ACreatureCharacter* Self,FVector Start,FVector End)
{
    Start.Z=0; End.Z=0;
    const ACharacter* Result=nullptr; float Nearest=MAX_flt;
    for(TActorIterator<ACharacter> It(Self->GetWorld());It;++It)
    {
        if(*It==Self || !It->GetCapsuleComponent() || It->GetCapsuleComponent()->GetCollisionEnabled()==ECollisionEnabled::NoCollision) continue;
        if(FMath::Abs(It->GetActorLocation().Z-Self->GetActorLocation().Z)>Self->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+It->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) continue;
        FVector Position=It->GetActorLocation(); Position.Z=0;
        if(FVector::DotProduct(End-Start,Start-Position)>=0) continue;
        const float Radius=Self->GetCapsuleComponent()->GetScaledCapsuleRadius()+It->GetCapsuleComponent()->GetScaledCapsuleRadius()+12;
        const float Distance=FVector::DistSquared2D(Start,Position);
        if(Distance<Nearest && FVector::Dist2D(Position,FMath::ClosestPointOnSegment(Position,Start,End))<Radius)
        { Result=*It; Nearest=Distance; }
    }
    return Result;
}
bool ClearPath(const ACreatureCharacter* Self,const UNavigationPath* Path)
{
    if(!Path || !Path->IsValid() || Path->IsPartial() || Path->PathPoints.Num()<2) return false;
    for(int32 Index=1;Index<Path->PathPoints.Num();++Index)
        if(Blocker(Self,Path->PathPoints[Index-1],Path->PathPoints[Index])) return false;
    return true;
}
}

const ACharacter* ACreatureCharacter::FindFollowPathBlocker(const FVector& Destination) const
{
    return FrontierFollowPath::Blocker(this,GetActorLocation(),Destination);
}
bool ACreatureCharacter::FindFollowPathDetour(const FVector& Destination,const ACharacter* Blocker,FVector& Out) const
{
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if(!Nav || !IsValid(Blocker) || GetDistanceTo(Blocker)>350) return false;
    const float Radius=GetCapsuleComponent()->GetScaledCapsuleRadius()+Blocker->GetCapsuleComponent()->GetScaledCapsuleRadius()+80;
    float Best=MAX_flt;
    for(int32 Index=0;Index<16;++Index)
    {
        const float Angle=(Index%8)*PI/4;
        const float SearchRadius=Radius+(Index/8)*200;
        const FVector Candidate=Blocker->GetActorLocation()+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*SearchRadius;
        FNavLocation Projected;
        if(!Nav->ProjectPointToNavigation(Candidate,Projected,FVector(80,80,200))) continue;
        auto* Approach=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),GetActorLocation(),Projected.Location,const_cast<ACreatureCharacter*>(this));
        if(!FrontierFollowPath::ClearPath(this,Approach)) continue;
        auto* Departure=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Projected.Location,Destination,const_cast<ACreatureCharacter*>(this));
        if(!FrontierFollowPath::ClearPath(this,Departure)) continue;
        const float Cost=Approach->GetPathLength()+Departure->GetPathLength();
        if(Cost<Best) { Best=Cost; Out=Projected.Location; }
    }
    if(Best<MAX_flt && FParse::Param(FCommandLine::Get(),TEXT("CFFormationTrace")))
        UE_LOG(LogTemp,Display,TEXT("CF_FOLLOW_DETOUR pet=%s via=%s goal=%s length=%.1f"),*GetName(),*Out.ToString(),*Destination.ToString(),Best);
    return Best<MAX_flt;
}

bool ACreatureCharacter::HasClearFollowApproach(const FVector& Destination) const
{
    // Local steering needs a clear LOOKAHEAD corridor, not proximity to the final goal.
    // Otherwise expanded RVO radii can deadlock a physically clear approach outside docking range.
    const FVector LookAhead=GetActorLocation()+(Destination-GetActorLocation()).GetClampedToMaxSize2D(220.f);
    return !FindFollowPathBlocker(LookAhead);
}

FVector ACreatureCharacter::FindFollowApproach(const FVector& Destination) const
{
    FVector Start=GetActorLocation(),End=Destination; Start.Z=0; End.Z=0;
    const float Length=FVector::Dist2D(Start,End);
    if(Length>350 || Length<1) return Destination;
    for(TActorIterator<ACharacter> It(GetWorld());It;++It)
    {
        if(*It==this || !It->GetCapsuleComponent() || It->GetCapsuleComponent()->GetCollisionEnabled()==ECollisionEnabled::NoCollision) continue;
        if(FMath::Abs(It->GetActorLocation().Z-GetActorLocation().Z)>GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+It->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) continue;
        FVector Position=It->GetActorLocation(); Position.Z=0;
        const float Radius=GetCapsuleComponent()->GetScaledCapsuleRadius()+It->GetCapsuleComponent()->GetScaledCapsuleRadius()+12;
        if(FVector::Dist2D(End,Position)<Radius)
        {
            FVector Away=(End-Position).GetSafeNormal2D(); if(Away.IsNearlyZero()) Away=FVector::RightVector;
            FVector Alternate=Position+Away*(Radius+60); Alternate.Z=Destination.Z; return Alternate;
        }
    }
    // Segment blockers are resolved from the REAL nav path in NavigateTo. A perpendicular
    // raw ray waypoint here can lie inside static geometry and project onto our current poly.
    return Destination;
}

FVector ACreatureCharacter::GetFollowDestination() const
{
    const auto* Hero=Tamer?Cast<ACFPlayerCharacter>(Tamer->GetPawn()):nullptr;
    if(!Hero) return GetActorLocation();
    auto Followers=Hero->GetCompanionRoster();
    Followers.RemoveAll([](const ACreatureCharacter* Pet)
    {
        return Pet->Health<=0 || (Pet->Activity!=ECreatureActivity::Follow && Pet->Activity!=ECreatureActivity::Idle);
    });
    const int32 Slot=FMath::Max(0,Followers.IndexOfByKey(this));
    const int32 Column=Slot%3,Row=Slot/3;
    const FRotator Facing(0,Hero->GetController()?Hero->GetController()->GetControlRotation().Yaw:Hero->GetActorRotation().Yaw,0);
    float Side=Column==0?0.f:Column==1?-130.f:130.f;
    const FVector Center=Hero->GetActorLocation()+Facing.RotateVector(FVector(-170.f-Row*150.f,0,0));
    // Keep the center approach corridor open. Side followers wait farther out
    // until their row's center has arrived, then dock inward without crossing it.
    if(Column!=0 && FVector::Dist2D(Followers[Row*3]->GetActorLocation(),Center)>65.f)
        Side+=Column==1?-150.f:150.f;
    const FVector Local(-170.f-Row*150.f,Side,0);
    const FVector Current=Facing.UnrotateVector(GetActorLocation()-Hero->GetActorLocation());
    const float Clearance=Hero->GetCapsuleComponent()->GetScaledCapsuleRadius()+GetCapsuleComponent()->GetScaledCapsuleRadius()+40;
    if(Current.X>0)
    {
        const float CrossingY=Current.Y+(Side-Current.Y)*(Current.X/(Current.X-Local.X));
        if(FMath::Abs(CrossingY)<Clearance)
        {
            const float Direction=Side<0?-1.f:Side>0?1.f:Current.Y<0?-1.f:1.f;
            return FindFollowApproach(Hero->GetActorLocation()+Facing.RotateVector(FVector(0,Direction*(Clearance+70),0)));
        }
    }
    return FindFollowApproach(Hero->GetActorLocation()+Facing.RotateVector(Local));
}
