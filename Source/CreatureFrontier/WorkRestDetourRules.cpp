#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "WorkStation.h"
#include "BaseBuildingComponent.h"
#include "InventoryComponent.h"
#include "InputRoutingQA.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "Containers/Ticker.h"

namespace
{
struct FWorkRestRules
{
    UWorld* World=nullptr;ACFPlayerCharacter* Hero=nullptr;
    TWeakObjectPtr<ACreatureCharacter> Worker;TWeakObjectPtr<AWorkStation> Farm,Trough;TWeakObjectPtr<AStaticMeshActor> Wall;
    FVector Origin;double At=0,Deadline=0;int32 Phase=0;bool Blocked=false,Cancelled=false,Removed=false;
    bool End(bool Pass,const TCHAR* Why)
    {
        UE_LOG(LogTemp,Display,TEXT("CF_WORK_REST_RULES %s reason=%s blocked=%d cancelled=%d removed=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Why,Blocked,Cancelled,Removed);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    bool Load()
    {
        if(Wall.IsValid())Wall->Destroy();Wall.Reset();Worker.Reset();Farm.Reset();Trough.Reset();
        if(!Hero->LoadProgress(TEXT("WorkRestDetourInput")))return false;
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController() && It->Definition && It->Definition->Form==ECreatureForm::Leaf)Worker=*It;
        for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt){if(It->RequiredSpecialty==EWorkSpecialty::Farming)Farm=*It;if(It->RequiredSpecialty==EWorkSpecialty::Feeding)Trough=*It;}
        if(!Worker.IsValid() || !Farm.IsValid() || !Trough.IsValid() || Trough->StoredFood!=2 || Hero->Inventory->Count(EFrontierItem::Material)!=0 || Worker->WorkSite!=Farm.Get() || Worker->Activity!=ECreatureActivity::Work)return false;
        Origin=Worker->GetActorLocation();At=World->GetTimeSeconds();return true;
    }
    bool Tick()
    {
        if(FPlatformTime::Seconds()>Deadline || !Worker.IsValid() || !Farm.IsValid())return End(false,TEXT("lifetime"));
        auto* AI=Cast<AAIController>(Worker->GetController());if(!AI)return End(false,TEXT("controller"));
        if(Phase==1 && Worker->GetActorLocation().X < -180+Worker->GetCapsuleComponent()->GetScaledCapsuleRadius()-1)return End(false,TEXT("crossed-wall"));
        if(Phase==0)
        {
            // 显式添加带碰撞的门口障碍夹具，不修改原角色、任务、饥饿或设施库存。
            auto* Block=World->SpawnActor<AStaticMeshActor>();Wall=Block;auto* Mesh=Block->GetStaticMeshComponent();Mesh->SetMobility(EComponentMobility::Movable);Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Mesh->SetCollisionProfileName(TEXT("BlockAll"));Block->SetActorTransform(FTransform(FRotator::ZeroRotator,FVector(-200,-400,130),FVector(.4,2,3.2)));
            Phase=1;At=World->GetTimeSeconds();return true;
        }
        if(Phase==1)
        {
            if(World->GetTimeSeconds()-At<2)return true;
            UE_LOG(LogTemp,Display,TEXT("CF_WORK_REST_BLOCK_STATE origin=%s worker=%s distance=%.2f activity=%d job=%d xp=%d stock=%d material=%d"),*Origin.ToString(),*Worker->GetActorLocation().ToString(),FVector::Dist2D(Origin,Worker->GetActorLocation()),static_cast<int32>(Worker->Activity),Worker->WorkSite==Farm.Get(),Worker->Experience,Trough->StoredFood,Hero->Inventory->Count(EFrontierItem::Material));
            Blocked=Worker->GetActorLocation().X >= -180+Worker->GetCapsuleComponent()->GetScaledCapsuleRadius()-1 && Worker->WorkSite==Farm.Get() && Worker->Activity==ECreatureActivity::Work && Worker->Experience==40 && Trough->StoredFood==2 && Hero->Inventory->Count(EFrontierItem::Material)==0;
            if(!Blocked)return End(false,TEXT("blocked"));if(!Load())return End(false,TEXT("reload-cancel"));Phase=2;return true;
        }
        if(Phase==2 || Phase==3)
        {
            if(UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World) || AI->GetMoveStatus()!=EPathFollowingStatus::Moving || FVector::Dist2D(Origin,Worker->GetActorLocation())<10){if(World->GetTimeSeconds()-At>4)return End(false,TEXT("no-active-detour"));return true;}
            if(Phase==2)
            {
                Cancelled=CFInputQA::Press(Hero,EKeys::F) && Worker->Activity==ECreatureActivity::Follow && !Worker->WorkSite && !Worker->bResumeWorkAfterRest && AI->GetMoveStatus()==EPathFollowingStatus::Idle && Trough->StoredFood==2 && Hero->Inventory->Count(EFrontierItem::Material)==0;
                if(!Cancelled)return End(false,TEXT("cancel"));if(!Load())return End(false,TEXT("reload-remove"));Phase=3;return true;
            }
            Removed=Hero->Building->DismantleStation(Farm.Get()) && Worker->Activity==ECreatureActivity::Follow && !Worker->WorkSite && !Worker->bResumeWorkAfterRest && AI->GetMoveStatus()==EPathFollowingStatus::Idle && Hero->Inventory->Count(EFrontierItem::Material)==9 && Trough->StoredFood==2;
            return End(Blocked && Cancelled && Removed,TEXT("complete"));
        }
        return End(false,TEXT("phase"));
    }
};
}
void RunWorkRestDetourRules(UWorld* World,ACFPlayerCharacter* Hero)
{
    auto S=MakeShared<FWorkRestRules>();S->World=World;S->Hero=Hero;S->Deadline=FPlatformTime::Seconds()+30;
    if(!S->Load()){S->End(false,TEXT("source"));return;}
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){return S->Tick();}));
}
