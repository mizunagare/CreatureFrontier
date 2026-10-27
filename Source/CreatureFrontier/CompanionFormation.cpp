#include "CreatureCharacter.h"
#include "CFPlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"

bool ACreatureCharacter::HasClearFollowApproach(const FVector& Destination) const
{
    if(FVector::Dist2D(GetActorLocation(),Destination)>220) return false;
    FVector Start=GetActorLocation(),End=Destination; Start.Z=0; End.Z=0;
    for(TActorIterator<ACharacter> It(GetWorld());It;++It)
    {
        if(*It==this || !It->GetCapsuleComponent() || It->GetCapsuleComponent()->GetCollisionEnabled()==ECollisionEnabled::NoCollision) continue;
        if(FMath::Abs(It->GetActorLocation().Z-GetActorLocation().Z)>GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+It->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) continue;
        FVector Position=It->GetActorLocation(); Position.Z=0;
        if(FVector::DotProduct(End-Start,Start-Position)>=0) continue; // Moving away is safe even when capsules are touching.
        const float Radius=GetCapsuleComponent()->GetScaledCapsuleRadius()+It->GetCapsuleComponent()->GetScaledCapsuleRadius()+12;
        if(FVector::Dist2D(Position,FMath::ClosestPointOnSegment(Position,Start,End))<Radius) return false;
    }
    return true;
}

FVector ACreatureCharacter::FindFollowApproach(const FVector& Destination) const
{
    FVector Start=GetActorLocation(),End=Destination; Start.Z=0; End.Z=0;
    const float Length=FVector::Dist2D(Start,End);
    if(Length>350 || Length<1) return Destination;
    const FVector Direction=(End-Start).GetSafeNormal2D();
    const FVector Perpendicular(-Direction.Y,Direction.X,0);
    const ACharacter* Blocker=nullptr; float First=MAX_flt;
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
        if(FVector::DotProduct(End-Start,Start-Position)>=0) continue;
        const float Along=FVector::DotProduct(Position-Start,Direction);
        if(Along<First && FVector::Dist2D(Position,FMath::ClosestPointOnSegment(Position,Start,End))<Radius)
        { Blocker=*It; First=Along; }
    }
    if(!Blocker) return Destination;
    const float AwaySide=FVector::DotProduct(Blocker->GetActorLocation()-GetActorLocation(),Perpendicular)>=0?-1.f:1.f;
    const float Radius=GetCapsuleComponent()->GetScaledCapsuleRadius()+Blocker->GetCapsuleComponent()->GetScaledCapsuleRadius()+70;
    FVector Waypoint=Blocker->GetActorLocation()+Perpendicular*(AwaySide*Radius); Waypoint.Z=Destination.Z;
    return Waypoint;
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
