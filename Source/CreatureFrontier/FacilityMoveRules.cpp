#include "CFPlayerCharacter.h"
#include "BaseBuildingComponent.h"
#include "WorkStation.h"
#include "FarmCropComponent.h"
#include "CreatureCharacter.h"
#include "InventoryComponent.h"
#include "Camera/CameraComponent.h"

void RunFacilityMoveRules(UWorld* World,ACFPlayerCharacter* Hero)
{
    // 隔离逻辑夹具：位置和库存明确设置，不证明真实采集、步行或冷启动恢复。
    Hero->SetActorLocation(FVector(-550,-1100,40));
    Hero->Inventory->Restore({{EFrontierItem::Material,0}});
    const FTransform Original(FRotator::ZeroRotator,FVector(-700,-700,-15),FVector(2,1,.7));
    auto* Site=World->SpawnActorDeferred<AWorkStation>(AWorkStation::StaticClass(),Original,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn,ESpawnActorScaleMethod::OverrideRootScale);
    Site->bPlayerBuilt=true;Site->PersistentId=FGuid::NewGuid();Site->RequiredSpecialty=EWorkSpecialty::Crafting;
    Site->FinishSpawning(Original);
    Site->StoredFood=3;Site->CraftRecipeId=TEXT("Medicine");Site->CraftWork=2.5f;
    Site->Crop->Seeds=2;Site->Crop->bPlanted=true;Site->Crop->GrowthWork=3;
    const FGuid Id=Site->PersistentId;
    auto* Pet=World->SpawnActor<ACreatureCharacter>(FVector(-900,-900,40),FRotator::ZeroRotator);
    Pet->Disposition=ECreatureDisposition::Tamed;Pet->Tamer=Hero->GetController();Pet->WorkSite=Site;Pet->Activity=ECreatureActivity::Rest;
    auto* Builder=Hero->Building.Get();
    auto Aim=[&](FVector Point){Hero->FirstPersonCamera->SetWorldRotation((Point-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());};
    auto State=[&](){return Site->PersistentId==Id && Site->StoredFood==3 && Site->CraftRecipeId==TEXT("Medicine") && Site->CraftWork==2.5f && Site->Crop->Seeds==2 && Site->Crop->bPlanted && Site->Crop->GrowthWork==3 && Pet->WorkSite==Site && Pet->Activity==ECreatureActivity::Rest && Hero->Inventory->Count(EFrontierItem::Material)==0;};
    Builder->Toggle();Aim(Site->GetActorLocation());Builder->ReplaceHome();
    const bool Started=Builder->IsRelocating() && Site->GetActorTransform().Equals(Original) && State();
    Builder->Cancel();const bool Cancelled=!Builder->IsRelocating() && Site->GetActorTransform().Equals(Original) && State();
    Builder->Toggle();Aim(Site->GetActorLocation());Builder->ReplaceHome();
    FString Reason;
    const FTransform Target(FRotator::ZeroRotator,FVector(-400,-700,-15),FVector(2,1,.7));
    auto* Blocker=World->SpawnActorDeferred<AWorkStation>(AWorkStation::StaticClass(),Target,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn,ESpawnActorScaleMethod::OverrideRootScale);
    Blocker->FinishSpawning(Target);
    const bool Occupied=!Builder->ValidatePlacement(Target,Reason) && State() && Site->GetActorTransform().Equals(Original);
    Blocker->Destroy();
    const bool NoDuplicate=Builder->BuildAt(Target)==nullptr && State();
    const bool Valid=Builder->ValidatePlacement(Target,Reason);
    Aim(FVector(-400,-700,-50));Builder->Confirm();
    const bool Moved=!Builder->IsActive() && Site->GetActorTransform().Equals(Target,.1) && State();
    const bool Pass=Started && Cancelled && Occupied && NoDuplicate && Valid && Moved;
    const TCHAR* Result=Pass?TEXT("PASS"):TEXT("FAIL");
    UE_LOG(LogTemp,Display,TEXT("CF_FACILITY_MOVE_RULES %s started=%d cancelled=%d occupied=%d noDuplicate=%d valid=%d moved=%d feedback=%s"),Result,Started,Cancelled,Occupied,NoDuplicate,Valid,Moved,*Hero->Feedback);
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}
