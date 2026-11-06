#include "CreatureCharacter.h"
#include "WorkStation.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Components/CapsuleComponent.h"
#include "CompanionClearance.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "GameFramework/Controller.h"
#include "CFPlayerCharacter.h"
#include "FarmCropComponent.h"

bool ACreatureCharacter::CanReachWorkSite(const AWorkStation* Site) const
{
    if(!IsValid(Site) || GetDistanceTo(Site)>220) return false;
    FHitResult Hit;FCollisionQueryParams Params(SCENE_QUERY_STAT(FarmApproachSight),false,this);
    return !GetWorld()->LineTraceSingleByChannel(Hit,GetActorLocation(),Site->GetActorLocation()+FVector(0,0,30),ECC_Visibility,Params) || Hit.GetActor()==Site;
}

void ACreatureCharacter::ApproachWorkSite(AWorkStation* Site)
{
    const double Now=GetWorld()->GetTimeSeconds();
    if(Now<NextNavigationUpdate)return;
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if(!Nav || !IsValid(Site)){StopNavigation();NextNavigationUpdate=Now+.4;return;}
    if(WorkDetourSite.Get()==Site && Now<WorkDetourExpires && FVector::Dist2D(GetActorLocation(),WorkDetourGoal)>20)
    {
        auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),GetActorLocation(),WorkDetourGoal,this);
        if(FrontierFollowPath::ClearPath(this,Path)){NavigateTo(WorkDetourGoal,15);return;}
    }
    WorkDetourSite.Reset();
    // 设施中心不是站立点；限制垂直投影，避免选择同一水平位置上方的屋顶。
    bool Found=false,CharacterBlocked=false;FVector Best=FVector::ZeroVector;double BestLength=DBL_MAX;
    struct FBlockedStand{const ACreatureCharacter* Peer;FVector Goal;double Length;};
    TArray<FBlockedStand> BlockedStands;
    for(int32 Index=0;Index<8;++Index)
    {
        const double Angle=Index*PI/4;
        const FVector Candidate=Site->GetActorLocation()+FVector(FMath::Cos(Angle)*170,FMath::Sin(Angle)*170,0);
        FNavLocation Projected;
        if(!Nav->ProjectPointToNavigation(Candidate,Projected,FVector(35,35,80)))continue;
        const FVector Center=Projected.Location+FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        if(FVector::Dist(Center,Site->GetActorLocation())>210)continue;
        FHitResult Hit;FCollisionQueryParams Params(SCENE_QUERY_STAT(FarmStandSight),false,this);
        // 设施旁的导航面不代表角色能站下，尤其不能选择主人正占据的门边位置。
        const auto* Shape=GetCapsuleComponent();
        if(GetWorld()->OverlapBlockingTestByChannel(Center,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Shape->GetScaledCapsuleRadius()-1,Shape->GetScaledCapsuleHalfHeight()-1),Params))
        {
            if(FParse::Param(FCommandLine::Get(),TEXT("CFFarmApproachTrace")))UE_LOG(LogTemp,Display,TEXT("CF_FARM_APPROACH_REJECT reason=occupied goal=%s"),*Center.ToString());
            continue;
        }
        if(GetWorld()->LineTraceSingleByChannel(Hit,Center,Site->GetActorLocation()+FVector(0,0,30),ECC_Visibility,Params) && Hit.GetActor()!=Site)continue;
        const auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),GetActorLocation(),Projected.Location,this);
        if(!Path || !Path->IsValid() || Path->IsPartial())continue;
        // 最短的静态导航路径仍可能穿过玩家；复用原人物路径检查，不忽略真实碰撞。
        const double Length=Path->GetPathLength();
        if(!FrontierFollowPath::ClearPath(this,Path))
        {
            CharacterBlocked=true;
            if(Activity==ECreatureActivity::Work || (Activity==ECreatureActivity::Rest && bResumeWorkAfterRest))
                for(int32 Segment=1;Segment<Path->PathPoints.Num();++Segment)
                    if(const auto* Body=FrontierFollowPath::Blocker(this,Path->PathPoints[Segment-1],Path->PathPoints[Segment]))
                    {
                        const auto* Peer=Cast<ACreatureCharacter>(Body);
                        // 待复工者回休息点时，也允许绕过空圃旁静止等待的伙伴；不改变双方指派。
                        const bool EmptyFarmWait=Peer && Peer->Activity==ECreatureActivity::Work && IsValid(Peer->WorkSite) && Peer->WorkSite->RequiredSpecialty==EWorkSpecialty::Farming && !Peer->WorkSite->Crop->IsOccupied() && Peer->GetDistanceTo(Peer->WorkSite)<=220;
                        const bool ManualRest=Peer && Peer->Activity==ECreatureActivity::Rest && !Peer->bResumeWorkAfterRest;
                        if(FParse::Param(FCommandLine::Get(),TEXT("CFFarmApproachTrace")))UE_LOG(LogTemp,Display,TEXT("CF_WORK_PATH_BLOCKER self=%s blocker=%s emptyFarm=%d manualRest=%d"),*GetName(),*GetNameSafe(Body),EmptyFarmWait,ManualRest);
                        if(Peer && Peer->Tamer==Tamer && Peer->Disposition==ECreatureDisposition::Tamed && Peer->Health>0 && (ManualRest || (Activity==ECreatureActivity::Rest && bResumeWorkAfterRest && EmptyFarmWait)) && !Peer->FeedingSite.IsValid() && Peer->GetVelocity().Size2D()<10)
                        {BlockedStands.Add({Peer,Projected.Location,Length});}
                        break;
                    }
            if(FParse::Param(FCommandLine::Get(),TEXT("CFFarmApproachTrace")))UE_LOG(LogTemp,Display,TEXT("CF_FARM_APPROACH_REJECT reason=character-path goal=%s"),*Center.ToString());
            continue;
        }
        if(Length<BestLength){BestLength=Length;Best=Projected.Location;Found=true;}
    }
    if(Found)
    {
        if(auto* FarmOwner=Tamer?Cast<ACFPlayerCharacter>(Tamer->GetPawn()):nullptr;FarmOwner)
        {
            if(FarmOwner->Feedback==TEXT("农圃接近通道被角色占用，请让开通路；伙伴保留原工作"))FarmOwner->Feedback=TEXT("农圃通路已恢复，伙伴继续原工作");
            else if(FarmOwner->Feedback==TEXT("休息设施通道被角色占用，请让开通路；伙伴保留原指令"))FarmOwner->Feedback=TEXT("休息设施通路已恢复，伙伴继续前往");
        }
        NextFarmBlockHint=0;
        if(FParse::Param(FCommandLine::Get(),TEXT("CFFarmApproachTrace")))UE_LOG(LogTemp,Display,TEXT("CF_FARM_APPROACH_SELECT goal=%s length=%.1f"),*Best.ToString(),BestLength);
        NavigateTo(Best,15);
    }
    else
    {
        FVector Detour;
        // 最近站位的绕行失败不代表设施所有方向都不可接近；最多复用原八个有效站位。
        BlockedStands.Sort([](const FBlockedStand& A,const FBlockedStand& B){return A.Length<B.Length;});
        for(const auto& Stand:BlockedStands)if(FindFollowPathDetour(Stand.Goal,Stand.Peer,Detour,600,true))
        {
            WorkDetourSite=Site;WorkDetourGoal=Detour;WorkDetourExpires=Now+6;
            if(FParse::Param(FCommandLine::Get(),TEXT("CFFarmApproachTrace")))UE_LOG(LogTemp,Display,TEXT("CF_WORK_REST_DETOUR actor=%s blocker=%s goal=%s stand=%s"),*GetName(),*GetNameSafe(Stand.Peer),*Detour.ToString(),*Stand.Goal.ToString());
            NavigateTo(Detour,15);return;
        }
        // 无安全接近通道时保留原指令，让开后按原更新继续。
        auto* FarmOwner=Tamer?Cast<ACFPlayerCharacter>(Tamer->GetPawn()):nullptr;
        if(CharacterBlocked && FarmOwner && Now>=NextFarmBlockHint)
        {FarmOwner->Feedback=Activity==ECreatureActivity::Rest?TEXT("休息设施通道被角色占用，请让开通路；伙伴保留原指令"):TEXT("农圃接近通道被角色占用，请让开通路；伙伴保留原工作");NextFarmBlockHint=Now+5;}
        StopNavigation();NextNavigationUpdate=Now+(BlockedStands.Num()>0?1:.4);
    }
}
