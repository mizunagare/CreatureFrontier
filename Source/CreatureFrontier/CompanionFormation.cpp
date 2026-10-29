#include "CreatureCharacter.h"
#include "CompanionClearance.h"
#include "CFPlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
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
    const auto* Capsule=Self->GetCapsuleComponent();
    const float HeightOffset=Self->GetActorLocation().Z-Path->PathPoints[0].Z;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CompanionDetourClearance),false,Self);
    if(Self->Activity==ECreatureActivity::Combat && Self->FocusTarget)Query.AddIgnoredActor(Self->FocusTarget);
    for(int32 Index=1;Index<Path->PathPoints.Num();++Index)
    {
        FVector Start=Path->PathPoints[Index-1]+FVector(0,0,HeightOffset);
        const FVector End=Path->PathPoints[Index]+FVector(0,0,HeightOffset);
        if(Index==1 && FVector::Dist2D(Start,Self->GetActorLocation())<40)Start=Self->GetActorLocation();
        FHitResult VolumeHit;
        if(FParse::Param(FCommandLine::Get(),TEXT("CFHollowDetourTrace")) && Self->GetWorld()->SweepSingleByChannel(VolumeHit,Start,End,FQuat::Identity,ECC_Pawn,
            FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight()),Query))
        {
            if(FParse::Param(FCommandLine::Get(),TEXT("CFHollowDetourTrace")))
                UE_LOG(LogTemp,Display,TEXT("CF_DETOUR_VOLUME_REJECT actor=%s from=%s to=%s"),*GetNameSafe(VolumeHit.GetActor()),*Start.ToString(),*End.ToString());
            return false;
        }
        if(const auto* Hit=Blocker(Self,Path->PathPoints[Index-1],Path->PathPoints[Index]))
        {
            if(FParse::Param(FCommandLine::Get(),TEXT("CFHollowDetourTrace")))
                UE_LOG(LogTemp,Display,TEXT("CF_DETOUR_REJECT actor=%s segment=%d from=%s to=%s"),*GetNameSafe(Hit),Index,*Path->PathPoints[Index-1].ToString(),*Path->PathPoints[Index].ToString());
            return false;
        }
    }
    return true;
}
bool ClearYieldSegment(const ACreatureCharacter* Self,const FVector& Start,const FVector& End,const FCollisionQueryParams& Query,FHitResult& Hit,bool& StepDown)
{
    StepDown=false;const auto* Capsule=Self->GetCapsuleComponent();
    const auto Volume=FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius()-1,Capsule->GetScaledCapsuleHalfHeight()-1);
    if(!Self->GetWorld()->SweepSingleByChannel(Hit,Start,End,FQuat::Identity,ECC_Pawn,Volume,Query))return true;
    const auto* Movement=Self->GetCharacterMovement();const double Drop=Start.Z-End.Z;
    if(Hit.bStartPenetrating || !Movement || !Movement->IsWalkable(Hit) || Drop<=0 || Drop>Movement->MaxStepHeight)return false;
    // 斜扫会提前切入仍在脚下的地板。只对可行走表面、实际跨阶上限内的下降，
    // 按角色先水平越过边缘再落脚的路径复核；两段都保留完整胶囊碰撞检查。
    const FVector Edge(End.X,End.Y,Start.Z);FHitResult StepHit;
    if(Self->GetWorld()->SweepSingleByChannel(StepHit,Start,Edge,FQuat::Identity,ECC_Pawn,Volume,Query) ||
        Self->GetWorld()->SweepSingleByChannel(StepHit,Edge,End,FQuat::Identity,ECC_Pawn,Volume,Query)){Hit=StepHit;return false;}
    StepDown=true;return true;
}
}

const ACharacter* ACreatureCharacter::FindFollowPathBlocker(const FVector& Destination) const
{
    return FrontierFollowPath::Blocker(this,GetActorLocation(),Destination);
}

bool ACreatureCharacter::FindOwnerYield(FVector& Out)
{
    const auto* Hero=Tamer?Cast<ACFPlayerCharacter>(Tamer->GetPawn()):nullptr;if(!Hero || Activity!=ECreatureActivity::Follow)return false;
    const double Now=GetWorld()->GetTimeSeconds();
    if(Now<OwnerYieldUntil && FVector::Dist2D(GetActorLocation(),OwnerYieldGoal)>12){Out=OwnerYieldGoal;return true;}
    const FVector Intent=Hero->GetLastMovementInputVector().GetSafeNormal2D(),Offset=GetActorLocation()-Hero->GetActorLocation();
    const auto* Shape=GetCapsuleComponent();const float Combined=Shape->GetScaledCapsuleRadius()+Hero->GetCapsuleComponent()->GetScaledCapsuleRadius();
    if(Intent.IsNearlyZero() || FMath::Abs(Offset.Z)>40 || Offset.Size2D()>Combined+30 || FVector::DotProduct(Intent,Offset.GetSafeNormal2D())<.65 || Now<NextOwnerYieldSearch)return false;
    NextOwnerYieldSearch=Now+.4;auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());if(!Nav)return false;
    const bool Trace=FParse::Param(FCommandLine::Get(),TEXT("CFRenovationTrace"));
    const FVector Side(-Intent.Y,Intent.X,0);double Best=DBL_MAX;FVector Goal=FVector::ZeroVector;bool ForwardFallback=false;
    // 优先侧步；窄通道两侧都被设施封住时，才短暂向前让行。
    // 所有候选仍检查同层投影、完整路径与真实胶囊扫掠，不穿过主人或跳到其他楼层。
    for(int32 Index=0;Index<8;++Index)
    {
        if(Index==4){if(Best<DBL_MAX)break;ForwardFallback=true;}
        const FVector CandidateOffset=Index<4?Side*(Index%2?-1:1)*(90+(Index/2)*40):Index<6?Intent*(130+(Index-4)*100):Intent*170+Side*(Index%2?-60:60);
        const auto Candidate=GetActorLocation()+CandidateOffset-FVector(0,0,Shape->GetScaledCapsuleHalfHeight());
        FNavLocation Projected;if(!Nav->ProjectPointToNavigation(Candidate,Projected,FVector(35,35,60))){if(Trace)UE_LOG(LogTemp,Display,TEXT("CF_OWNER_YIELD_CANDIDATE index=%d reason=projection"),Index);continue;}
        const auto Center=Projected.Location+FVector(0,0,Shape->GetScaledCapsuleHalfHeight());
        if(FVector::Dist2D(Center,Hero->GetActorLocation())<Combined+24 || FVector::Dist2D(Center,GetActorLocation())<45){if(Trace)UE_LOG(LogTemp,Display,TEXT("CF_OWNER_YIELD_CANDIDATE index=%d reason=distance"),Index);continue;}
        FCollisionQueryParams Query(SCENE_QUERY_STAT(OwnerYield),false,this);
        if(GetWorld()->OverlapBlockingTestByChannel(Center,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Shape->GetScaledCapsuleRadius()-1,Shape->GetScaledCapsuleHalfHeight()-1),Query)){if(Trace)UE_LOG(LogTemp,Display,TEXT("CF_OWNER_YIELD_CANDIDATE index=%d reason=overlap goal=%s"),Index,*Center.ToString());continue;}
        const auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),GetActorLocation(),Projected.Location,this);
        if(!FrontierFollowPath::ClearPath(this,Path)){if(Trace)UE_LOG(LogTemp,Display,TEXT("CF_OWNER_YIELD_CANDIDATE index=%d reason=path"),Index);continue;}
        bool Clear=true;const auto Height=GetActorLocation().Z-Path->PathPoints[0].Z;
        for(int32 Point=1;Point<Path->PathPoints.Num();++Point)
        {
            FHitResult Hit;bool StepDown=false;const auto Start=Path->PathPoints[Point-1]+FVector(0,0,Height),End=Path->PathPoints[Point]+FVector(0,0,Height);
            if(!FrontierFollowPath::ClearYieldSegment(this,Start,End,Query,Hit,StepDown)){
                if(Trace)UE_LOG(LogTemp,Display,TEXT("CF_OWNER_YIELD_SWEEP index=%d actor=%s component=%s from=%s to=%s normal=%s penetration=%d depth=%.2f"),Index,*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()),*Start.ToString(),*End.ToString(),*Hit.ImpactNormal.ToString(),Hit.bStartPenetrating,Hit.PenetrationDepth);
                Clear=false;break;
            }
            if(Trace && StepDown)UE_LOG(LogTemp,Display,TEXT("CF_OWNER_YIELD_STEPDOWN index=%d drop=%.1f limit=%.1f swept=2"),Index,Start.Z-End.Z,GetCharacterMovement()->MaxStepHeight);
        }
        if(Trace)UE_LOG(LogTemp,Display,TEXT("CF_OWNER_YIELD_CANDIDATE index=%d reason=%s goal=%s"),Index,Clear?TEXT("accepted"):TEXT("sweep"),*Center.ToString());
        if(Clear && Path->GetPathLength()<Best){Best=Path->GetPathLength();Goal=Projected.Location;}
    }
    if(Best==DBL_MAX)return false;OwnerYieldGoal=Goal;OwnerYieldUntil=Now+1.2;Out=Goal;
    if(Trace || FParse::Param(FCommandLine::Get(),TEXT("CFHomeExpansionRead")) || FParse::Param(FCommandLine::Get(),TEXT("CFHomeExpansionTest")))
    {UE_LOG(LogTemp,Display,TEXT("CF_OWNER_YIELD pet=%s goal=%s length=%.1f fallback=%d"),*PersistentId.ToString(),*Goal.ToString(),Best,ForwardFallback);}
    return true;
}
bool ACreatureCharacter::FindCombatRetreat(const FVector& Destination,const ACharacter* Obstacle,TArray<FVector>& Out) const
{
    const double SearchStarted=FPlatformTime::Seconds();
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if(!Nav || !IsValid(Obstacle) || GetDistanceTo(Obstacle)>150)return false;
    const FVector Origin=GetActorLocation();const auto* Shape=GetCapsuleComponent();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CompanionCombatRetreat),false,this);
    Query.AddIgnoredActor(FocusTarget);
    // 有界局部搜索：每条边都检验真实体积和同层导航面，不把导航折点当作可穿墙短段。
    constexpr int32 Width=33,Center=16,Count=Width*Width;constexpr float Step=60;
    TArray<int32> Parent;Parent.Init(-1,Count);TArray<int32> Queue;
    const int32 First=Center*Width+Center;Parent[First]=First;Queue.Add(First);
    auto Position=[Origin](int32 I){return Origin+FVector((I%Width-Center)*Step,(I/Width-Center)*Step,0);};
    int32 Found=-1;
    for(int32 Head=0;Head<Queue.Num() && Head<Count;++Head)
    {
        const int32 Current=Queue[Head];const FVector Start=Position(Current);
        if(Current!=First && FVector::Dist2D(Start,Destination)<90){Found=Current;break;}
        for(int32 DY=-1;DY<=1;++DY)for(int32 DX=-1;DX<=1;++DX)
        {
            if(!DX && !DY)continue;const int32 X=Current%Width+DX,Y=Current/Width+DY;
            if(X<0 || X>=Width || Y<0 || Y>=Width)continue;
            const int32 Next=Y*Width+X;if(Parent[Next]!=-1)continue;
            const FVector End=Position(Next);bool Supported=true;
            for(float T:{.5f,1.f})
            {
                const FVector Sample=FMath::Lerp(Start,End,T);FNavLocation Projected;
                if(!Nav->ProjectPointToNavigation(Sample,Projected,FVector(20,20,120)) ||
                    FVector::Dist2D(Sample,Projected.Location)>15 || FMath::Abs(Projected.Location.Z-(Origin.Z-Shape->GetScaledCapsuleHalfHeight()))>25)
                {Supported=false;break;}
            }
            if(!Supported)continue;
            FHitResult Hit;
            if(GetWorld()->SweepSingleByChannel(Hit,Start,End,FQuat::Identity,ECC_Pawn,
                FCollisionShape::MakeCapsule(Shape->GetScaledCapsuleRadius(),Shape->GetScaledCapsuleHalfHeight()),Query))continue;
            Parent[Next]=Current;Queue.Add(Next);
        }
    }
    Out.Reset();
    if(Found>=0)for(int32 I=Found;I!=First;I=Parent[I])Out.Insert(Position(I),0);
    if(FParse::Param(FCommandLine::Get(),TEXT("CFHollowDetourTrace")))
        UE_LOG(LogTemp,Display,TEXT("CF_COMBAT_ROUTE_SEARCH elapsedMs=%.3f discovered=%d limit=%d points=%d found=%d"),
            (FPlatformTime::Seconds()-SearchStarted)*1000,Queue.Num(),Count,Out.Num(),Found>=0);
    return Out.Num()>0;
}
bool ACreatureCharacter::FindFollowPathDetour(const FVector& Destination,const ACharacter* Blocker,FVector& Out) const
{
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if(!Nav || !IsValid(Blocker) || GetDistanceTo(Blocker)>350) return false;
    const float Radius=GetCapsuleComponent()->GetScaledCapsuleRadius()+Blocker->GetCapsuleComponent()->GetScaledCapsuleRadius()+80;
    float Best=MAX_flt;
    const bool Trace=FParse::Param(FCommandLine::Get(),TEXT("CFHollowDetourTrace"));
    for(int32 Index=0;Index<16;++Index)
    {
        const float Angle=(Index%8)*PI/4;
        const float SearchRadius=Radius+(Index/8)*200;
        const FVector Candidate=Blocker->GetActorLocation()+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*SearchRadius;
        FNavLocation Projected;
        if(!Nav->ProjectPointToNavigation(Candidate,Projected,FVector(80,80,200)))
        {if(Trace)UE_LOG(LogTemp,Display,TEXT("CF_DETOUR_CANDIDATE index=%d reason=projection"),Index);continue;}
        auto* Approach=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),GetActorLocation(),Projected.Location,const_cast<ACreatureCharacter*>(this));
        if(!FrontierFollowPath::ClearPath(this,Approach))
        {if(Trace)UE_LOG(LogTemp,Display,TEXT("CF_DETOUR_CANDIDATE index=%d reason=approach projected=%s valid=%d partial=%d"),Index,*Projected.Location.ToString(),Approach && Approach->IsValid(),Approach && Approach->IsPartial());continue;}
        auto* Departure=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Projected.Location,Destination,const_cast<ACreatureCharacter*>(this));
        if(!FrontierFollowPath::ClearPath(this,Departure))
        {if(Trace)UE_LOG(LogTemp,Display,TEXT("CF_DETOUR_CANDIDATE index=%d reason=departure projected=%s valid=%d partial=%d"),Index,*Projected.Location.ToString(),Departure && Departure->IsValid(),Departure && Departure->IsPartial());continue;}
        if(Trace)UE_LOG(LogTemp,Display,TEXT("CF_DETOUR_CANDIDATE index=%d reason=accepted projected=%s"),Index,*Projected.Location.ToString());
        const float Cost=Approach->GetPathLength()+Departure->GetPathLength();
        if(Cost<Best) { Best=Cost; Out=Projected.Location; }
    }
    if(Best<MAX_flt && FParse::Param(FCommandLine::Get(),TEXT("CFFormationTrace")))
        UE_LOG(LogTemp,Display,TEXT("CF_FOLLOW_DETOUR pet=%s via=%s goal=%s length=%.1f"),*GetName(),*Out.ToString(),*Destination.ToString(),Best);
    return Best<MAX_flt;
}

bool ACreatureCharacter::HasClearFollowApproach(const FVector& Destination) const
{
    // 局部转向需要前方通道畅通，不能只检查是否接近最终目标。
    // 否则扩大的 RVO 半径可能让停靠范围外本可通行的接近路线陷入僵局。
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
    // NavigateTo 根据真实导航路径处理分段障碍；此处垂直射线生成的临时点
    // 可能落入静态几何体内部，并投影回当前导航多边形。
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
    // 保持中央接近通道畅通；两侧伙伴在远处等待本排中央伙伴抵达，
    // 随后向内停靠，避免横穿中央通道。
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
