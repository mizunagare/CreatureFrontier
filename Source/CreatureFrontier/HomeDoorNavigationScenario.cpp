#include "HomeDoorScenario.h"
#include "HomeModule.h"
#include "HomeBuildingComponent.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "InventoryComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "WildEncounter.h"
#include "EngineUtils.h"
#include "TimerManager.h"

namespace CFDoorNavigationQA
{
FVector Point(FVector Position,float Yaw)
{const FVector Pivot(-400,-800,0);return Pivot+FRotator(0,Yaw,0).RotateVector(Position-Pivot);}
FVector Original(FVector Position,float Yaw)
{const FVector Pivot(-400,-800,0);return Pivot+FRotator(0,Yaw,0).UnrotateVector(Position-Pivot);}
bool PathInside(UWorld* World,ACreatureCharacter* Pet,float Yaw)
{
    auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Pet->GetActorLocation(),Point(FVector(-400,-840,60),Yaw),Pet);
    return Path && Path->IsValid() && !Path->IsPartial();
}
bool Toggle(ACFPlayerCharacter* Hero,AHomeModule* Door,float Yaw)
{
    const auto Rotation=(Point(FVector(-555,-1000,120),Yaw)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
    Hero->GetController()->SetControlRotation(Rotation);Hero->FirstPersonCamera->SetWorldRotation(Rotation);
    const bool Result=Door->ToggleDoor(Hero);
    return Result;
}
void Facing(ACFPlayerCharacter* Hero,float Yaw)
{Hero->GetController()->SetControlRotation(FRotator(0,Yaw,0));Hero->FirstPersonCamera->SetWorldRotation(FRotator(0,Yaw,0));}
}
AHomeModule* CFDoorNavigationQA::Room(UWorld* World,ACFPlayerCharacter* Hero,float Orientation)
{
    // 隔离门与导航，不把固定房间夹具宣称为自然冒险或群体导航验收。
    for(TActorIterator<AWildEncounter> It(World);It;++It)It->SetActorTickEnabled(false);
    for(TActorIterator<ACreatureCharacter> It(World);It;++It){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();}
    Hero->GetCharacterMovement()->DisableMovement();Hero->SetActorLocation(CFDoorNavigationQA::Point(FVector(-400,-1250,60),Orientation));
    Hero->Inventory->Restore({{EFrontierItem::Material,300}});
    auto Build=[Hero,Orientation](EHomeModuleKind Kind,FVector Position,FGuid Floor=FGuid(),double Yaw=0)
    {Hero->HomeBuilding->Selected=Kind;const bool Wall=Kind==EHomeModuleKind::Wall || Kind==EHomeModuleKind::Door;const double Angle=Wall?FMath::Fmod(Yaw+Orientation,180.):0;return Hero->HomeBuilding->BuildAt(FTransform(FRotator(0,Angle,0),CFDoorNavigationQA::Point(Position,Orientation)),Floor);};
    auto* Floor=Build(EHomeModuleKind::Floor,FVector(-400,-800,-40));
    auto* Door=Floor?Build(EHomeModuleKind::Door,FVector(-400,-1000,-30),Floor->PersistentId):nullptr;
    bool Room=Door && Build(EHomeModuleKind::Wall,FVector(-600,-800,-30),Floor->PersistentId,90) &&
        Build(EHomeModuleKind::Wall,FVector(-200,-800,-30),Floor->PersistentId,90) &&
        Build(EHomeModuleKind::Wall,FVector(-400,-600,-30),Floor->PersistentId) &&
        Build(EHomeModuleKind::Roof,FVector(-400,-800,280),Floor->PersistentId);
    return Room?Door:nullptr;
}
void RunHomeDoorNavigationScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Rotated)
{
    const float Orientation=Rotated?90.f:0.f;
    auto* Door=CFDoorNavigationQA::Room(World,Hero,Orientation);
    auto* Definition=LoadObject<UCreatureDefinition>(nullptr,TEXT("/Game/Creatures/DA_Mosskip.DA_Mosskip"));
    if(!Door || !Definition){UE_LOG(LogTemp,Error,TEXT("CF_DOOR_NAV FAIL fixture %s"),*Hero->Feedback);FPlatformMisc::RequestExitWithStatus(false,1);return;}
    const auto Poll=MakeShared<FTimerHandle>();
    World->GetTimerManager().SetTimer(*Poll,[World,Hero,Door,Definition,Poll,Orientation,Phase=0,At=World->GetTimeSeconds(),Pet=static_cast<ACreatureCharacter*>(nullptr),
        Previous=FVector::ZeroVector,Travel=0.,ClosedMaxY=-DBL_MAX,Closed=false,Opened=false,Entered=false,Occupied=false,Exited=false,Continuous=true]() mutable
    {
        const double Elapsed=World->GetTimeSeconds()-At;
        auto Finish=[&](bool Passed)
        {
            UE_LOG(LogTemp,Display,TEXT("CF_DOOR_NAV %s yaw=%.0f phase=%d closed=%d open=%d entered=%d occupied=%d exited=%d continuous=%d travel=%.1f pet=%s feedback=%s"),
                Passed?TEXT("PASS"):TEXT("FAIL"),Orientation,Phase,Closed,Opened,Entered,Occupied,Exited,Continuous,Travel,Pet?*Pet->GetActorLocation().ToString():TEXT("无"),*Hero->Feedback);
            FTimerHandle Handle=*Poll;World->GetTimerManager().ClearTimer(Handle);FPlatformMisc::RequestExitWithStatus(false,Passed?0:1);
        };
        if(Pet)
        {
            const FVector Now=Pet->GetActorLocation();const double Step=FVector::Dist2D(Now,Previous);
            Travel+=Step;Continuous &= Step<120;Previous=Now;
            if(Pet->Health<=0 || Hero->Health<=0 || !Continuous){Finish(false);return;}
        }
        if(Phase==0 && Elapsed>=2)
        {
            Hero->SetActorLocation(CFDoorNavigationQA::Point(FVector(-400,-680,60),Orientation));CFDoorNavigationQA::Facing(Hero,90+Orientation);
            const FTransform Transform(CFDoorNavigationQA::Point(FVector(-400,-1350,60),Orientation));
            Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if(!Pet){Finish(false);return;}
            Pet->Definition=Definition;Pet->FinishSpawning(Transform);Pet->Tame(Hero->GetController());Pet->SetActivity(ECreatureActivity::Follow);
            Previous=Pet->GetActorLocation();At=World->GetTimeSeconds();Phase=1;return;
        }
        if(Phase==1)
        {
            ClosedMaxY=FMath::Max(ClosedMaxY,CFDoorNavigationQA::Original(Pet->GetActorLocation(),Orientation).Y);
            if(Elapsed<4)return;
            Closed=Door->DoorState==1 && ClosedMaxY<-1030 && Pet->Activity==ECreatureActivity::Follow &&
                CFDoorNavigationQA::Original(Pet->GetFollowDestination(),Orientation).Y>-950 && !CFDoorNavigationQA::PathInside(World,Pet,Orientation);
            UE_LOG(LogTemp,Display,TEXT("CF_DOOR_NAV_CLOSED %s maxY=%.1f destination=%s"),Closed?TEXT("PASS"):TEXT("FAIL"),ClosedMaxY,*Pet->GetFollowDestination().ToString());
            if(!Closed){Finish(false);return;}
            // 只调整玩家取景/开门位置；伙伴此后始终由真实导航和碰撞移动。
            Hero->SetActorLocation(CFDoorNavigationQA::Point(FVector(-400,-720,60),Orientation));Opened=CFDoorNavigationQA::Toggle(Hero,Door,Orientation) && Door->DoorState==2;
            CFDoorNavigationQA::Facing(Hero,90+Orientation);
            if(!Opened){Finish(false);return;}
            At=World->GetTimeSeconds();Phase=2;return;
        }
        if(Phase==2)
        {
            if(CFDoorNavigationQA::Original(Pet->GetActorLocation(),Orientation).Y<-930 && Elapsed<12)return;
            Entered=CFDoorNavigationQA::Original(Pet->GetActorLocation(),Orientation).Y>-930 && Pet->GetActorLocation().Z>40 && CFDoorNavigationQA::PathInside(World,Pet,Orientation);
            if(!Entered){Finish(false);return;}
            Occupied=!CFDoorNavigationQA::Toggle(Hero,Door,Orientation) && Door->DoorState==2 && Hero->Feedback.Contains(TEXT("活动范围"));
            if(!Occupied){Finish(false);return;}
            Hero->SetActorLocation(CFDoorNavigationQA::Point(FVector(-400,-1250,60),Orientation));CFDoorNavigationQA::Facing(Hero,-90+Orientation);
            At=World->GetTimeSeconds();Phase=3;return;
        }
        if(Phase==3)
        {
            if(CFDoorNavigationQA::Original(Pet->GetActorLocation(),Orientation).Y>-1050 && Elapsed<12)return;
            Exited=CFDoorNavigationQA::Original(Pet->GetActorLocation(),Orientation).Y<-1050;
            const bool Reclosed=Exited && CFDoorNavigationQA::Toggle(Hero,Door,Orientation) && Door->DoorState==1;
            Finish(Closed && Opened && Entered && Occupied && Exited && Reclosed && Travel>400);return;
        }
    },.1f,true);
}
