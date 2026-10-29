#include "CompanionApproach.h"
#include "CreatureCharacter.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Components/CapsuleComponent.h"
#include "HomeModule.h"
bool FindCompanionApproach(ACreatureCharacter* Pet,AActor* Target,FVector& Out)
{
    if(!IsValid(Pet) || !IsValid(Target) || Pet->GetWorld()!=Target->GetWorld())return false;
    auto* World=Pet->GetWorld();auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);double Best=DBL_MAX;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CompanionApproach),false,Pet);const auto* Shape=Pet->GetCapsuleComponent();
    for(int32 Index=0;Nav && Index<8;++Index)
    {
        const auto Candidate=Target->GetActorLocation()+FVector(FMath::Cos(Index*PI/4)*170,FMath::Sin(Index*PI/4)*170,0);FNavLocation Floor;
        if(!Nav->ProjectPointToNavigation(Candidate,Floor,FVector(35,35,80)))continue;
        const auto Center=Floor.Location+FVector(0,0,Shape->GetScaledCapsuleHalfHeight());FHitResult Hit;
        if(const auto* Site=Cast<AHomeModule>(Target);Site && Site->bUnderConstruction && !Site->ClearsCompletedPawn(Pet,Center))continue;
        if(FVector::Dist(Center,Target->GetActorLocation())>210 || World->OverlapBlockingTestByChannel(Center,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Shape->GetScaledCapsuleRadius()-1,Shape->GetScaledCapsuleHalfHeight()-1),Query))continue;
        if(World->LineTraceSingleByChannel(Hit,Center,Target->GetActorLocation()+FVector(0,0,40),ECC_Visibility,Query) && Hit.GetActor()!=Target)continue;
        const auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Pet->GetActorLocation(),Floor.Location,Pet);
        if(!Path || !Path->IsValid() || Path->IsPartial())continue;
        if(Path->GetPathLength()<Best){Best=Path->GetPathLength();Out=Floor.Location;}
    }
    return Best<DBL_MAX;
}
