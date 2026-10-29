#include "CFPlayerCharacter.h"
#include "PlayerPaceComponent.h"
#include "PlayerGatheringComponent.h"
#include "CreatureCharacter.h"
#include "BaseBuildingComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"
#include "PlayerEquipmentComponent.h"

void ACFPlayerCharacter::InitializeRespawnAnchor()
{
    RespawnAnchor=GetActorTransform();
    for(TActorIterator<APlayerStart> It(GetWorld());It;++It) { RespawnAnchor=It->GetActorTransform(); break; }
}

float ACFPlayerCharacter::GetRespawnProtectionRemaining() const
{
    return GetWorld()?static_cast<float>(FMath::Max(0.0,RespawnProtectionUntil-GetWorld()->GetTimeSeconds())):0;
}

bool ACFPlayerCharacter::FindRespawnLocation(FVector& Location) const
{
    TArray<FVector> Candidates; Candidates.Add(RespawnAnchor.GetLocation());
    for(int32 Radius=0;Radius<=1200;Radius+=200)
        for(int32 Angle=0;Angle<(Radius==0?1:16);++Angle)
        {
            const float Radians=Angle*2*PI/16;
            Candidates.Add(UBaseBuildingComponent::BaseOrigin()+FVector(FMath::Cos(Radians)*Radius,FMath::Sin(Radians)*Radius,0));
        }
    FCollisionQueryParams Params(SCENE_QUERY_STAT(BaseRespawn),false,this);
    const float HalfHeight=GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), Radius=GetCapsuleComponent()->GetScaledCapsuleRadius();
    for(const auto& Point:Candidates)
    {
        FHitResult Ground;
        const FVector Start(Point.X,Point.Y,UBaseBuildingComponent::BaseOrigin().Z+300);
        if(!GetWorld()->LineTraceSingleByChannel(Ground,Start,Start-FVector(0,0,600),ECC_Visibility,Params) ||
            Ground.ImpactNormal.Z<GetCharacterMovement()->GetWalkableFloorZ() || FMath::Abs(Ground.ImpactPoint.Z-UBaseBuildingComponent::BaseOrigin().Z)>20) continue;
        const FVector Candidate=Ground.ImpactPoint+FVector(0,0,HalfHeight+2);
        if(GetWorld()->OverlapBlockingTestByProfile(Candidate,FQuat::Identity,GetCapsuleComponent()->GetCollisionProfileName(),FCollisionShape::MakeCapsule(Radius,HalfHeight),Params)) continue;
        bool Threat=false;
        for(TActorIterator<ACreatureCharacter> It(GetWorld());It;++It)
            if(It->Health>0 && It->Disposition!=ECreatureDisposition::Tamed && FVector::Dist2D(It->GetActorLocation(),Candidate)<800 && FMath::Abs(It->GetActorLocation().Z-Candidate.Z)<300) { Threat=true; break; }
        if(!Threat) { Location=Candidate; return true; }
    }
    return false;
}

bool ACFPlayerCharacter::RespawnAtBase()
{
    if(!HasAuthority() || !GetController()) return false;
    if(Health>0) { Feedback=TEXT("只有倒下时才能按R复活，不会重置世界"); return false; }
    FVector Location;
    if(!FindRespawnLocation(Location)) { Feedback=TEXT("基地被阻挡或不安全，可按F9读取此前存档"); return false; }
    // 保留当前世界；仅取消战斗，不清除工作分配、生命或冷却状态。
    for(TActorIterator<ACreatureCharacter> It(GetWorld());It;++It) It->HandlePlayerDowned(this);
    Pace->StopSlowWalk(); Gathering->Stop(); CancelMeleeAttack(); Building->Cancel(); bRosterOpen=false; bJournalOpen=false;
    GetCharacterMovement()->StopMovementImmediately(); StopJumping();
    SetActorLocationAndRotation(Location,FRotator(0,RespawnAnchor.Rotator().Yaw,0),false,nullptr,ETeleportType::TeleportPhysics);
    GetController()->SetControlRotation(FRotator(0,RespawnAnchor.Rotator().Yaw,0));
    Health=100; GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Equipment->RefreshVisual();
    RespawnProtectionUntil=GetWorld()->GetTimeSeconds()+3;
    Feedback=TEXT("已在基地复活，保留世界但不治疗伙伴。保护3秒，攻击会结束保护");
    return true;
}
