#include "HomeModule.h"
#include "CFPlayerCharacter.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"

TArray<FHomePart> AHomeModule::PlacementParts(EHomeModuleKind Kind)
{
    auto Result=Parts(Kind);
    // 预留整个转动范围的保守包围盒，不允许后来把设施建进活动空间。
    if(Kind==EHomeModuleKind::Door)Result.Add({FVector(0,110,125),FVector(119,119,125)});
    // 每级踏步上方留两米净空，放置时同时检查头顶；实体渲染仍只使用踏步本身。
    if(Kind==EHomeModuleKind::Stairs)
    {
        Result.Add({FVector(-175,0,100),FVector(25,100,100)});
        for(const auto& Step:Parts(Kind))Result.Add({Step.Center+FVector(0,0,Step.Extent.Z+100),FVector(Step.Extent.X,100,100)});
    }
    return Result;
}
FBox AHomeModule::DoorReservation(const FTransform& Transform)
{return FBox(FVector(-118,-8,1),FVector(118,228,249)).TransformBy(Transform.ToMatrixWithScale());}
AHomeModule* AHomeModule::TraceDoor(ACFPlayerCharacter* Hero)
{
    if(!IsValid(Hero) || !Hero->FirstPersonCamera)return nullptr;
    FHitResult Hit;FCollisionQueryParams Params(SCENE_QUERY_STAT(HomeDoorSight),false,Hero);
    const FVector Start=Hero->FirstPersonCamera->GetComponentLocation();
    if(!Hero->GetWorld()->LineTraceSingleByChannel(Hit,Start,Start+Hero->FirstPersonCamera->GetForwardVector()*800,ECC_Visibility,Params))return nullptr;
    auto* Door=Cast<AHomeModule>(Hit.GetActor());
    return Door && Door->GetKind()==EHomeModuleKind::Door && !Door->bPreview && !Door->bStaged?Door:nullptr;
}
bool AHomeModule::ToggleDoor(ACFPlayerCharacter* Hero)
{
    if(!HasAuthority() || !IsValid(Hero) || !Hero->HasAuthority() || Hero->Health<=0 || Hero->GetWorld()!=GetWorld() ||
        IsActorBeingDestroyed() || bPreview || bStaged || !PersistentId.IsValid() || GetKind()!=EHomeModuleKind::Door ||
        (DoorState!=1 && DoorState!=2) || UGameplayStatics::IsGamePaused(this))return false;
    if(Hero->GetDistanceTo(this)>300 || TraceDoor(Hero)!=this)
    {Hero->Feedback=TEXT("开关门需要瞄准3米内可见的实体门");return false;}
    FCollisionQueryParams Params(SCENE_QUERY_STAT(HomeDoorSweep),false,this);
    const FVector Center=GetActorTransform().TransformPosition(FVector(0,110,125));
    if(GetWorld()->OverlapAnyTestByObjectType(Center,GetActorQuat(),FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects),FCollisionShape::MakeBox(FVector(118,118,124)),Params))
    {Hero->Feedback=TEXT("门扇活动范围有生物或障碍，保持原状态");return false;}
    DoorState=DoorState==1?2:1;
    // 首版瞬时切换实际实体姿态；移动网格沿用引擎导航脏区更新，不隐藏碰撞。
    OnConstruction(GetActorTransform());
    Hero->Feedback=DoorState==2?TEXT("门已打开，伙伴可实际通行"):TEXT("门已关闭，门扇保持真实阻挡");
    return true;
}
