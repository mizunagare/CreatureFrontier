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
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Engine/OverlapResult.h"
#include "InputRoutingQA.h"
#include "HomeBuildingComponent.h"
#include "StorageInteractionComponent.h"
#include "FrontierSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

namespace CFDoorFarmVolumeQA
{
bool InsideRoom(FVector Position)
{return Position.X>-560 && Position.X<-240 && Position.Y>-1000 && Position.Y<-600 && Position.Z>40 && Position.Z<120;}
bool FurnishedRoom(UWorld* World)
{
    TSet<FGuid> Floors;int32 Modules=0,Windows=0,Doors=0,Roofs=0,Crates=0;
    for(TActorIterator<AHomeModule> It(World);It;++It)
    {
        if(It->bPreview || It->bStaged)continue;++Modules;
        if(It->GetKind()==EHomeModuleKind::Floor)Floors.Add(It->PersistentId);
        Windows+=It->GetKind()==EHomeModuleKind::Window;Doors+=It->GetKind()==EHomeModuleKind::Door;
        Roofs+=It->GetKind()==EHomeModuleKind::Roof;
        if(It->GetKind()==EHomeModuleKind::Storage)
        {++Crates;if(It->StorageInventory->Count(EFrontierItem::Material)!=3)return false;}
    }
    if(Floors.Num()!=2 || Modules!=11 || Windows!=1 || Doors!=1 || Roofs!=2 || Crates!=1)return false;
    for(TActorIterator<AHomeModule> It(World);It;++It)
        if(It->GetKind()!=EHomeModuleKind::Floor && !It->bPreview && ( !Floors.Contains(It->FloorId) || It->PaidMaterials<=0))return false;
    return true;
}
bool FurnishedSnapshot(UWorld* World,ACFPlayerCharacter* Hero,const FString& Slot)
{
    const auto* Save=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0));
    if(!Save || !FurnishedRoom(World) || !Hero->Inventory->GetStacks().OrderIndependentCompareEqual(Save->InventoryStacks))return false;
    const auto Live=HomeLayout::Collect(World);if(Live.Num()!=Save->HomeModules.Num())return false;
    for(const auto& Record:Save->HomeModules)
    {
        const auto* Found=Live.FindByPredicate([&Record](const FHomeModuleRecord& Other){return Other.Id==Record.Id;});
        if(!Found || Found->FloorId!=Record.FloorId || Found->Definition!=Record.Definition || !Found->Transform.Equals(Record.Transform,.01) ||
            Found->DoorState!=Record.DoorState || Found->PaidMaterials!=Record.PaidMaterials ||
            !Found->PaidExtraItems.OrderIndependentCompareEqual(Record.PaidExtraItems) || !Found->StoredItems.OrderIndependentCompareEqual(Record.StoredItems))return false;
    }
    return true;
}
}

void RunHomeDoorFarmScenario(UWorld* World,ACFPlayerCharacter* Hero,int32 Mode,bool UseExistingRoom)
{
    const bool AvoidTest=FParse::Param(FCommandLine::Get(),TEXT("CFStanceFarmTest"));
    const bool Furnished=FParse::Param(FCommandLine::Get(),TEXT("CFFurnishedFarm"));
    if(UseExistingRoom && (!Furnished || Mode!=0)){UE_LOG(LogTemp,Error,TEXT("CF_HOME_ROOM_WORK FAIL reuse-mode"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    const FString Slot=Furnished?TEXT("FurnishedFarmBlocked"):TEXT("DoorFarmBlocked");
    if(Furnished && Mode>2){UE_LOG(LogTemp,Error,TEXT("CF_FURNISHED_FARM FAIL unsupported-mode"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    if(AvoidTest && Mode!=0){UE_LOG(LogTemp,Error,TEXT("CF_STANCE_FARM FAIL mode"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    // 固定房间只验证单伙伴工作，不替代自由放置、长期运输或自然冒险。
    auto Finish=[Mode](bool Pass)
    {UE_LOG(LogTemp,Display,TEXT("CF_DOOR_FARM_MODE %s mode=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Mode);UE_LOG(LogTemp,Display,TEXT("CF_DOOR_FARM %s"),Pass?TEXT("PASS"):TEXT("FAIL"));FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);};
    AHomeModule* Door=nullptr;AWorkStation* Farm=nullptr;ACreatureCharacter* RestoredPet=nullptr;
    if(Mode==2)
    {
        if(!Hero->LoadProgress(Slot)){Finish(false);return;}
        if(Furnished)
        {
            const bool Snapshot=CFDoorFarmVolumeQA::FurnishedSnapshot(World,Hero,Slot);
            UE_LOG(LogTemp,Display,TEXT("CF_FURNISHED_RESTORE %s identity=1 inventory=1"),Snapshot?TEXT("PASS"):TEXT("FAIL"));
            if(!Snapshot){Finish(false);return;}
        }
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
    else if(UseExistingRoom)
    {
        AHomeModule* Crate=nullptr;
        for(TActorIterator<AHomeModule> It(World);It;++It)
        {if(It->GetKind()==EHomeModuleKind::Door)Door=*It;if(It->GetKind()==EHomeModuleKind::Storage)Crate=*It;}
        Hero->SetActorLocation(FVector(-800,-850,60));
        const bool Stored=Crate && Hero->Storage->Open(Crate) && Hero->Storage->Transfer(EFrontierItem::Material,3,true);
        Hero->Storage->Close();Hero->SetActorLocation(FVector(-400,-1250,60));
        if(!Stored || !CFDoorFarmVolumeQA::FurnishedRoom(World)){Finish(false);return;}
    }
    else Door=CFDoorNavigationQA::Room(World,Hero,0);
    if(Furnished && Door && Mode!=2 && !UseExistingRoom)
    {
        AHomeModule* West=nullptr;
        for(TActorIterator<AHomeModule> It(World);It;++It)
            if(It->GetKind()==EHomeModuleKind::Wall && FMath::IsNearlyEqual(It->GetActorLocation().X,-600.))West=*It;
        Hero->SetActorLocation(FVector(-430,-800,60));
        // 标准农圃和家具保持原尺寸，通过正常拆建扩展第二格室内空间。
        auto Build=[Hero](EHomeModuleKind Kind,FVector Position,FGuid Floor=FGuid(),double Yaw=0)
        {Hero->HomeBuilding->Selected=Kind;auto* Module=Hero->HomeBuilding->BuildAt(FTransform(FRotator(0,Yaw,0),Position),Floor);
            if(!Module)for(const auto& Part:AHomeModule::PlacementParts(Kind))
            {TArray<FOverlapResult> Hits;Hero->GetWorld()->OverlapMultiByObjectType(Hits,Position+FRotator(0,Yaw,0).RotateVector(Part.Center),FRotator(0,Yaw,0).Quaternion(),FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects),FCollisionShape::MakeBox(Part.Extent-FVector(1)));
                for(const auto& Hit:Hits)UE_LOG(LogTemp,Display,TEXT("CF_FURNISHED_OVERLAP actor=%s component=%s"),*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()));}
            UE_LOG(LogTemp,Display,TEXT("CF_FURNISHED_BUILD kind=%d position=%s built=%d feedback=%s"),static_cast<int32>(Kind),*Position.ToString(),Module!=nullptr,*Hero->Feedback);return Module;};
        const bool Opened=West && Hero->HomeBuilding->Dismantle(West);
        auto* Extension=Opened?Build(EHomeModuleKind::Floor,FVector(-800,-800,-40)):nullptr;
        Hero->SetActorLocation(FVector(-800,-900,60));
        const bool Shell=Extension && Build(EHomeModuleKind::Wall,FVector(-800,-1000,-30),Extension->PersistentId) &&
            Build(EHomeModuleKind::Wall,FVector(-800,-600,-30),Extension->PersistentId) &&
            Build(EHomeModuleKind::Window,FVector(-1000,-800,-30),Extension->PersistentId,90) &&
            Build(EHomeModuleKind::Roof,FVector(-800,-800,280),Extension->PersistentId);
        auto* Crate=Shell?Build(EHomeModuleKind::Storage,FVector(-900,-700,-30),Extension->PersistentId):nullptr;
        const bool Ready=Crate && Hero->Storage->Open(Crate) && Hero->Storage->Transfer(EFrontierItem::Material,3,true);
        Hero->Storage->Close();Hero->SetActorLocation(FVector(-400,-1250,60));
        if(!Ready || !CFDoorFarmVolumeQA::FurnishedRoom(World))
        {UE_LOG(LogTemp,Error,TEXT("CF_FURNISHED_FARM FAIL fixture %s"),*Hero->Feedback);Finish(false);return;}
    }
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
    const int32 HouseMaterials=Hero->Inventory->Count(EFrontierItem::Material);
    World->GetTimerManager().SetTimer(*Timer,[World,Hero,Door,Farm,FarmWeak=TWeakObjectPtr<AWorkStation>(Farm),Definition,Timer,Finish,Mode,AvoidTest,Furnished,Slot,HouseMaterials,UseExistingRoom,Phase=Mode==2?1:0,At=World->GetTimeSeconds(),Pet=RestoredPet,Previous=RestoredPet?RestoredPet->GetActorLocation():FVector::ZeroVector,Travel=0.,Continuous=true,ExpectedMaterials=0,Diagnosed=false]() mutable
    {
        auto End=[&](bool Pass)
        {
            if(Furnished)
            {
                Pass &= CFDoorFarmVolumeQA::FurnishedRoom(World) && Hero->Inventory->Count(EFrontierItem::Material)==HouseMaterials;
                UE_LOG(LogTemp,Display,TEXT("CF_FURNISHED_FARM %s stored=3 phase=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Phase);
            }
            if(UseExistingRoom)UE_LOG(LogTemp,Display,TEXT("CF_HOME_ROOM_WORK %s noRebuild=1"),Pass?TEXT("PASS"):TEXT("FAIL"));
            if(AvoidTest)
            {
                Pass &= Phase==2 && Pet && Pet->bAvoidCombat && Pet->WorkSite==Farm;
                UE_LOG(LogTemp,Display,TEXT("CF_STANCE_FARM %s"),Pass?TEXT("PASS"):TEXT("FAIL"));
            }
            const auto* LiveFarm=FarmWeak.Get();
            UE_LOG(LogTemp,Display,TEXT("CF_DOOR_FARM_STATE phase=%d food=%d seeds=%d growth=%.2f travel=%.1f continuous=%d pet=%s"),Phase,LiveFarm?LiveFarm->StoredFood:-1,LiveFarm?LiveFarm->Crop->Seeds:-1,LiveFarm?LiveFarm->Crop->GrowthWork:-1,Travel,Continuous,Pet?*Pet->GetActorLocation().ToString():TEXT("无"));
            // 清除定时器会释放捕获对象；完成回调必须先复制到当前调用栈。
            const auto Complete=Finish;
            const bool Preview=Pass && Furnished && Mode==0 && FParse::Param(FCommandLine::Get(),TEXT("CFFurnishedFarmPreview"));
            auto* Observer=Hero;auto* PreviewWorld=World;
            FTimerHandle Handle=*Timer;World->GetTimerManager().ClearTimer(Handle);
            if(Preview)
            {
                // 仅移动观察者到室内合法站位，不移动伙伴，也不改变房屋碰撞。
                Observer->SetActorLocation(FVector(-950,-950,60));
                const auto View=(FVector(-700,-650,90)-(Observer->GetActorLocation()+FVector(0,0,64))).Rotation();
                Observer->FirstPersonCamera->SetRelativeRotation(FRotator::ZeroRotator);Observer->GetController()->SetControlRotation(View);
                Observer->Feedback=TEXT("双格家园：门窗、储物与农圃协同，伙伴已完成本次工作");
                const FString Root=FPaths::ProjectSavedDir()/TEXT("FurnishedFarmPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits);
                IFileManager::Get().MakeDirectory(*Root,true);UE_LOG(LogTemp,Display,TEXT("CF_FURNISHED_PREVIEW_ROOT %s"),*Root);
                FTimerHandle Shot,Done;
                PreviewWorld->GetTimerManager().SetTimer(Shot,[Root,Observer](){UE_LOG(LogTemp,Display,TEXT("CF_FURNISHED_CAMERA hero=%s camera=%s rotation=%s"),*Observer->GetActorLocation().ToString(),*Observer->FirstPersonCamera->GetComponentLocation().ToString(),*Observer->FirstPersonCamera->GetComponentRotation().ToString());FScreenshotRequest::RequestScreenshot(Root/TEXT("Interior.png"),false,false);},1.f,false);
                PreviewWorld->GetTimerManager().SetTimer(Done,[Root,Complete](){const bool Ready=IFileManager::Get().FileSize(*(Root/TEXT("Interior.png")))>1000;
                    UE_LOG(LogTemp,Display,TEXT("CF_FURNISHED_PREVIEW %s"),Ready?TEXT("PASS"):TEXT("FAIL"));Complete(Ready);},3.f,false);
                return;
            }
            Complete(Pass);
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
            if(!Diagnosed && Elapsed>=2)
            {
                Diagnosed=true;auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
                TArray<FOverlapResult> Overlaps;FCollisionQueryParams Query(SCENE_QUERY_STAT(FarmHeroFixture),false,Hero);
                const auto* Capsule=Hero->GetCapsuleComponent();
                World->OverlapMultiByObjectType(Overlaps,Hero->GetActorLocation(),Hero->GetActorQuat(),FCollisionObjectQueryParams::AllObjects,
                    FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight()),Query);
                bool FarmOverlap=false;for(const auto& Overlap:Overlaps)FarmOverlap |= Overlap.GetActor()==Farm;
                UE_LOG(LogTemp,Display,TEXT("CF_DOOR_FARM_HERO_FIXTURE overlapFarm=%d position=%s"),FarmOverlap,*Hero->GetActorLocation().ToString());
                if(FarmOverlap && !FParse::Param(FCommandLine::Get(),TEXT("CFDoorFarmCancelOriginalAim"))){End(false);return;}
                for(int32 Index=0;Index<2;++Index)
                {
                    FNavLocation Goal;const bool Projected=Nav && Nav->ProjectPointToNavigation(Pet->GetFollowDestination(),Goal,Index==0?FVector(250,250,300):FVector(80,80,80));
                    auto* Path=Projected?UNavigationSystemV1::FindPathToLocationSynchronously(World,Pet->GetActorLocation(),Goal.Location,Pet):nullptr;
                    UE_LOG(LogTemp,Display,TEXT("CF_DOOR_FARM_FOLLOW_PATH sample=%d desired=%s projected=%d point=%s valid=%d partial=%d length=%.1f"),Index,*Pet->GetFollowDestination().ToString(),Projected,*Goal.Location.ToString(),Path && Path->IsValid(),Path && Path->IsPartial(),Path?Path->GetPathLength():-1);
                }
                const auto* AI=Cast<AAIController>(Pet->GetController());
                UE_LOG(LogTemp,Display,TEXT("CF_DOOR_FARM_FOLLOW_AI state=%d immediate=%s"),AI?static_cast<int32>(AI->GetMoveStatus()):-1,AI?*AI->GetImmediateMoveDestination().ToString():TEXT("无"));
            }
            if(Elapsed<8)return;
            const bool FacingProbe=FParse::Param(FCommandLine::Get(),TEXT("CFDoorFarmFacingProbe"));
            const FVector Position=Pet->GetActorLocation();
            const bool VolumeChecked=CFDoorFarmVolumeQA::InsideRoom(FVector(-400,-850,60)) &&
                !CFDoorFarmVolumeQA::InsideRoom(FVector(-400,-850,360)) && !CFDoorFarmVolumeQA::InsideRoom(FVector(-666,-850,60));
            const bool Inside=CFDoorFarmVolumeQA::InsideRoom(Position);
            const bool SameFloor=FMath::Abs(Position.Z-Hero->GetActorLocation().Z)<100;
            UE_LOG(LogTemp,Display,TEXT("CF_DOOR_FARM_VOLUME %s sameFloor=%d height=%.1f"),VolumeChecked?TEXT("PASS"):TEXT("FAIL"),SameFloor,Position.Z);
            UE_LOG(LogTemp,Display,TEXT("CF_DOOR_FARM_DOCK probe=%d inside=%d distance=%.1f"),FacingProbe,Inside,FVector::Dist2D(Position,Pet->GetFollowDestination()));
            End(Farm->StoredFood==0 && Farm->Crop->Seeds==1 && !Farm->Crop->bPlanted && Farm->Crop->GrowthWork==0 &&
                !Pet->WorkSite && Pet->Activity==ECreatureActivity::Follow && VolumeChecked && SameFloor && (FacingProbe?Travel>100:(Travel>300 && Inside)) &&
                FVector::Dist2D(Pet->GetActorLocation(),Pet->GetFollowDestination())<65);return;
        }
        if(Phase==0 && Elapsed>=2)
        {
            Hero->SetActorLocation(FVector(-510,-730,60));
            const FTransform Transform(FVector(-400,-1350,60));
            Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if(!Pet){End(false);return;}
            Pet->Definition=Definition;Pet->FinishSpawning(Transform);Pet->Tame(Hero->GetController());Previous=Pet->GetActorLocation();
            // 使用玩家现有工作命令；看向上方避免优先命中门或野外资源。
            Hero->GetController()->SetControlRotation(FRotator(70,0,0));Hero->FirstPersonCamera->SetWorldRotation(FRotator(70,0,0));
            if(UseExistingRoom){if(!CFInputQA::Press(Hero,EKeys::E)){End(false);return;}}else Hero->CommandWork();
            if(Pet->WorkSite!=Farm || Pet->Activity!=ECreatureActivity::Work){End(false);return;}
            if(AvoidTest)
            {
                if(!CFInputQA::Press(Hero,EKeys::F8) || !Pet->bAvoidCombat || Pet->WorkSite!=Farm || Pet->Activity!=ECreatureActivity::Work){End(false);return;}
                UE_LOG(LogTemp,Display,TEXT("CF_STANCE_FARM_SWITCH PASS"));
            }
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
                bool Saved=Hero->SaveProgress(Slot);
                if(Furnished)Saved &= CFDoorFarmVolumeQA::FurnishedSnapshot(World,Hero,Slot);
                End(Saved);return;
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
            const bool Opened=UseExistingRoom?CFInputQA::Press(Hero,EKeys::E):Door->ToggleDoor(Hero);
            if(!Opened || Door->DoorState!=2){End(false);return;}
            if(Mode==3)
            {
                UE_LOG(LogTemp,Display,TEXT("CF_DOOR_FARM_CANCEL_AIM goal=%s"),*Pet->GetFollowDestination().ToString());
                // 跟随队位位于玩家身后；开门取景方向不应被误当成室内归队夹具。
                if(!FParse::Param(FCommandLine::Get(),TEXT("CFDoorFarmCancelOriginalAim")))
                {
                    Hero->SetActorLocation(FVector(-440,-940,60));
                    float Facing=-160;FParse::Value(FCommandLine::Get(),TEXT("CFDoorFarmFollowYaw="),Facing);
                    if(!FMath::IsFinite(Facing) || Facing < -180 || Facing > 180){End(false);return;}
                    Hero->GetController()->SetControlRotation(FRotator(0,Facing,0));Hero->FirstPersonCamera->SetWorldRotation(FRotator(0,Facing,0));
                    UE_LOG(LogTemp,Display,TEXT("CF_DOOR_FARM_FACING yaw=%.1f"),Facing);
                }
                UE_LOG(LogTemp,Display,TEXT("CF_DOOR_FARM_CANCEL_FOLLOW goal=%s"),*Pet->GetFollowDestination().ToString());
            }
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
