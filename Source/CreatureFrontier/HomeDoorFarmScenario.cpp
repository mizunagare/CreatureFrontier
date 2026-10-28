#include "HomeDoorScenario.h"
#include "HomeModule.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "WorkStation.h"
#include "FarmCropComponent.h"
#include "BaseBuildingComponent.h"
#include "Camera/CameraComponent.h"
#include "TimerManager.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "WildEncounter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InventoryComponent.h"

void RunHomeDoorFarmScenario(UWorld* World,ACFPlayerCharacter* Hero,int32 Mode)
{
    // 固定房间只验证单伙伴工作，不替代自由放置、长期运输或自然冒险。
    auto Finish=[Mode](bool Pass)
    {UE_LOG(LogTemp,Display,TEXT("CF_DOOR_FARM_MODE %s mode=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Mode);UE_LOG(LogTemp,Display,TEXT("CF_DOOR_FARM %s"),Pass?TEXT("PASS"):TEXT("FAIL"));FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);};
    AHomeModule* Door=nullptr;AWorkStation* Farm=nullptr;ACreatureCharacter* RestoredPet=nullptr;
    if(Mode==2)
    {
        if(!Hero->LoadProgress(TEXT("DoorFarmBlocked"))){Finish(false);return;}
        for(TActorIterator<AWildEncounter> It(World);It;++It)It->SetActorTickEnabled(false);
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)
            if(It->Tamer==Hero->GetController())RestoredPet=*It;
            else {It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();}
        for(TActorIterator<AHomeModule> It(World);It;++It)if(It->GetKind()==EHomeModuleKind::Door)Door=*It;
        for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt && It->RequiredSpecialty==EWorkSpecialty::Farming)Farm=*It;
        if(!Door || Door->DoorState!=1 || !Farm || !RestoredPet || RestoredPet->WorkSite!=Farm || RestoredPet->Activity!=ECreatureActivity::Work ||
            Farm->Crop->Seeds!=1 || Farm->StoredFood!=0 || Farm->Crop->bPlanted || Farm->Crop->GrowthWork!=0){Finish(false);return;}
        Hero->GetCharacterMovement()->DisableMovement();
    }
    else Door=CFDoorNavigationQA::Room(World,Hero,0);
    const bool NoRoof=FParse::Param(FCommandLine::Get(),TEXT("CFDoorFarmNoRoof"));
    int32 RemovedRoofs=0;
    if(NoRoof)for(TActorIterator<AHomeModule> It(World);It;++It)if(It->GetKind()==EHomeModuleKind::Roof){It->Destroy();++RemovedRoofs;}
    UE_LOG(LogTemp,Display,TEXT("CF_DOOR_FARM_FIXTURE roof=%d removed=%d"),!NoRoof,RemovedRoofs);
    auto* Definition=LoadObject<UCreatureDefinition>(nullptr,TEXT("/Game/Creatures/DA_Mosskip.DA_Mosskip"));
    if(!Door || !Definition || (NoRoof && RemovedRoofs!=1)){Finish(false);return;}
    const FTransform FarmTransform(FRotator::ZeroRotator,FVector(-400,-700,5),FVector(2,1,.7));
    if(Mode!=2){Hero->Building->Selected=EWorkSpecialty::Farming;Farm=Hero->Building->BuildAt(FarmTransform);}
    if(!Farm){UE_LOG(LogTemp,Display,TEXT("CF_DOOR_FARM_FIXTURE %s"),*Hero->Feedback);Finish(false);return;}
    if(Mode!=2)Farm->Crop->Seeds=1;
    const auto Timer=MakeShared<FTimerHandle>();
    World->GetTimerManager().SetTimer(*Timer,[World,Hero,Door,Farm,FarmWeak=TWeakObjectPtr<AWorkStation>(Farm),Definition,Timer,Finish,Mode,Phase=Mode==2?1:0,At=World->GetTimeSeconds(),Pet=RestoredPet,Previous=RestoredPet?RestoredPet->GetActorLocation():FVector::ZeroVector,Travel=0.,Continuous=true,ExpectedMaterials=0]() mutable
    {
        auto End=[&](bool Pass)
        {
            const auto* LiveFarm=FarmWeak.Get();
            UE_LOG(LogTemp,Display,TEXT("CF_DOOR_FARM_STATE phase=%d food=%d seeds=%d growth=%.2f travel=%.1f continuous=%d pet=%s"),Phase,LiveFarm?LiveFarm->StoredFood:-1,LiveFarm?LiveFarm->Crop->Seeds:-1,LiveFarm?LiveFarm->Crop->GrowthWork:-1,Travel,Continuous,Pet?*Pet->GetActorLocation().ToString():TEXT("无"));
            FTimerHandle Handle=*Timer;World->GetTimerManager().ClearTimer(Handle);Finish(Pass);
        };
        const double Elapsed=World->GetTimeSeconds()-At;
        if(Pet){const auto Now=Pet->GetActorLocation();const double Step=FVector::Dist2D(Now,Previous);Travel+=Step;Previous=Now;Continuous &= Step<120;if(!Continuous || Pet->Health<=0 || Hero->Health<=0){End(false);return;}}
        if(Phase==4)
        {
            if(Elapsed<8)return;
            End(!FarmWeak.IsValid() && !Pet->WorkSite && Pet->Activity==ECreatureActivity::Follow &&
                Hero->Inventory->Count(EFrontierItem::Material)==ExpectedMaterials && Hero->Inventory->Count(EFrontierItem::Seeds)==1);return;
        }
        if(Phase==3)
        {
            if(Elapsed<8)return;
            End(Farm->StoredFood==0 && Farm->Crop->Seeds==1 && !Farm->Crop->bPlanted && Farm->Crop->GrowthWork==0 &&
                !Pet->WorkSite && Pet->Activity==ECreatureActivity::Follow);return;
        }
        if(Phase==0 && Elapsed>=2)
        {
            Hero->SetActorLocation(FVector(-510,-730,60));
            const FTransform Transform(FVector(-400,-1350,60));
            Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if(!Pet){End(false);return;}
            Pet->Definition=Definition;Pet->FinishSpawning(Transform);Pet->Tame(Hero->GetController());Previous=Pet->GetActorLocation();
            // 使用玩家现有工作命令；看向上方避免优先命中门或野外资源。
            Hero->GetController()->SetControlRotation(FRotator(70,0,0));Hero->FirstPersonCamera->SetWorldRotation(FRotator(70,0,0));Hero->CommandWork();
            if(Pet->WorkSite!=Farm || Pet->Activity!=ECreatureActivity::Work){End(false);return;}
            Phase=1;At=World->GetTimeSeconds();return;
        }
        if(Phase==1)
        {
            const bool Unchanged=Farm->StoredFood==0 && Farm->Crop->Seeds==1 && !Farm->Crop->bPlanted && Farm->Crop->GrowthWork==0;
            if(!Unchanged || Pet->GetActorLocation().Y>-1000){End(false);return;}
            if(Elapsed<12)return;
            UE_LOG(LogTemp,Display,TEXT("CF_DOOR_FARM_CLOSED PASS"));
            if(Mode==1)
            {
                Hero->SetActorLocation(FVector(-530,-900,60));
                End(Hero->SaveProgress(TEXT("DoorFarmBlocked")));return;
            }
            if(Mode==4)
            {
                const int32 Before=Hero->Inventory->Count(EFrontierItem::Material);
                if(Hero->Building->DismantleStation(Farm) || !IsValid(Farm) || Pet->WorkSite!=Farm || Hero->Inventory->Count(EFrontierItem::Material)!=Before){End(false);return;}
                // 正常取出预留种子后才允许拆除，不直接清空资源绕过保护。
                if(!Farm->Crop->Interact(Hero,2) || !Hero->Building->DismantleStation(Farm)){End(false);return;}
                ExpectedMaterials=Before+UBaseBuildingComponent::GetCost(EWorkSpecialty::Farming)/2;
                Phase=4;At=World->GetTimeSeconds();return;
            }
            if(Mode==3){Hero->CommandFollow();if(Pet->WorkSite || Pet->Activity!=ECreatureActivity::Follow){End(false);return;}}
            Hero->SetActorLocation(FVector(-400,-720,60));
            const auto Rotation=(FVector(-555,-1000,120)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
            Hero->GetController()->SetControlRotation(Rotation);Hero->FirstPersonCamera->SetWorldRotation(Rotation);
            if(!Door->ToggleDoor(Hero) || Door->DoorState!=2){End(false);return;}
            FNavLocation Goal;auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
            const bool Projected=Nav && Nav->ProjectPointToNavigation(Farm->GetActorLocation(),Goal,FVector(250,250,300));
            auto* Path=Projected?UNavigationSystemV1::FindPathToLocationSynchronously(World,Pet->GetActorLocation(),Goal.Location,Pet):nullptr;
            UE_LOG(LogTemp,Display,TEXT("CF_DOOR_FARM_PATH projected=%d goal=%s valid=%d partial=%d activity=%d stamina=%.1f nutrition=%.1f"),Projected,*Goal.Location.ToString(),Path && Path->IsValid(),Path && Path->IsPartial(),static_cast<int32>(Pet->Activity),Pet->Stamina,Pet->Nutrition);
            Phase=Mode==3?3:2;At=World->GetTimeSeconds();return;
        }
        if(Phase==2)
        {
            if(Farm->StoredFood==0 && Elapsed<22)return;
            End(Farm->StoredFood==1 && Farm->Crop->Seeds==0 && !Farm->Crop->bPlanted && Farm->Crop->GrowthWork==0 &&
                Pet->WorkSite==Farm && Pet->Activity==ECreatureActivity::Work && Pet->GetActorLocation().Y>-1000 && Travel>300 && Continuous);
        }
    },.05f,true);
}
