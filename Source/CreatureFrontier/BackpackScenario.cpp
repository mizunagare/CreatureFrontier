#include "BackpackScenario.h"
#include "CFPlayerCharacter.h"
#include "InventoryComponent.h"
#include "PlayerEquipmentComponent.h"
#include "CreatureCharacter.h"
#include "WorkStation.h"
#include "EngineUtils.h"
#include "Engine/DamageEvents.h"
#include "Engine/GameViewportClient.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "ResourceNode.h"
#include "ResourceDefinition.h"
#include "Kismet/GameplayStatics.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Controller.h"

namespace
{
AActor* CraftAccessWall(UWorld* World,const FVector& Position=FVector(1300,-1000,100),const FVector& Extent=FVector(10,250,200))
{
    auto* Wall=World->SpawnActor<AActor>();auto* Mesh=NewObject<UStaticMeshComponent>(Wall);Wall->SetRootComponent(Mesh);
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Mesh->SetCollisionProfileName(TEXT("BlockAll"));Mesh->RegisterComponent();Wall->SetActorLocation(Position);Wall->SetActorScale3D(Extent/50);return Wall;
}
}

void RunBackpackScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    for(TActorIterator<ACreatureCharacter> It(World);It;++It) It->Destroy();
    const FVector Base(-450,0,40);
    Hero->SetActorLocation(Base); Hero->Health=100;
    Hero->Equipment->Restore({}); Hero->Inventory->Restore({{EFrontierItem::Material,100}});
    Hero->ToggleBackpack();
    bool Craft=Hero->IsBackpackOpen() && !UGameplayStatics::IsGamePaused(World);
    const int32 Costs[]={3,5,2,8,12};
    for(int32 Index=0;Index<5;++Index)
    {
        Craft &= Hero->GetBackpackRecipe()==Index && Hero->GetBackpackRecipeCost(Index)==Costs[Index] && Hero->GetBackpackRecipeStatus(Index).StartsWith(TEXT("可以制作"));
        Hero->ConfirmInterfaceAction(); Hero->PauseMenuNext();
    }
    // 原有五项保留顺序与价格；两项新增研究配方尚未解锁。
    for(int32 Index=5;Index<7;++Index)
    {
        Craft &= Hero->GetBackpackRecipe()==Index && Hero->GetBackpackRecipeStatus(Index).StartsWith(TEXT("尚未解锁"));
        Hero->ConfirmInterfaceAction(); Hero->PauseMenuNext();
    }
    for(int32 Index=7;Index<11;++Index)
    {
        Craft &= Hero->GetBackpackRecipe()==Index && !Hero->GetBackpackRecipeStatus(Index).StartsWith(TEXT("可以制作"));
        Hero->ConfirmInterfaceAction(); Hero->PauseMenuNext();
    }
    Craft &= Hero->GetBackpackRecipe()==0 && Hero->Inventory->Count(EFrontierItem::Material)==70;
    for(int32 Index=1;Index<4;++Index) Craft &= Hero->Inventory->Count(static_cast<EFrontierItem>(Index))==1;
    Craft &= Hero->Equipment->GetState().bSpearEquipped && Hero->Equipment->GetState().bVestEquipped;
    for(int32 Index=0;Index<4;++Index) Hero->StepBackpackRecipe(1);
    Hero->ConfirmInterfaceAction(); Hero->StepBackpackRecipe(-1); Hero->ConfirmInterfaceAction();
    bool Reject=Hero->Inventory->Count(EFrontierItem::Material)==70 && Hero->GetBackpackRecipeStatus(3).StartsWith(TEXT("已拥有"));
    Hero->ToggleBackpack(); Hero->ToggleBackpack();
    Hero->Inventory->Restore({{EFrontierItem::Material,1}}); Hero->ConfirmInterfaceAction();
    Reject &= Hero->Inventory->Count(EFrontierItem::Material)==1 && Hero->GetBackpackRecipeStatus(0).Contains(TEXT("还需要2份"));
    const auto Hint=Hero->GetBackpackGatheringHint(0);auto* HintSource=Hero->GetBackpackGatheringSource(0);
    bool Guidance=HintSource && Hint.Contains(TEXT("材料2")) && Hint.Contains(HintSource->Definition->DisplayName.ToString()) && HintSource->Remaining>0 && HintSource->Definition->OutputItem==EFrontierItem::Material;
    const auto BeforePosition=Hero->GetActorLocation();const auto BeforeMaterials=Hero->Inventory->Count(EFrontierItem::Material);
    Hero->GetBackpackGatheringHint(0);Guidance &= Hero->Inventory->Count(EFrontierItem::Material)==BeforeMaterials && Hero->GetActorLocation()==BeforePosition;
    Guidance &= Hero->GetBackpackGatheringHint(7).Contains(TEXT("纤维2")) && Hero->GetBackpackGatheringHint(10).Contains(TEXT("食物")) && Hero->GetBackpackGatheringHint(10).Contains(TEXT("按3制作"));
    Guidance &= !Hero->GetBackpackGatheringSource(-1) && !Hero->GetBackpackGatheringSource(999) && Hero->GetBackpackGatheringHint(3).Contains(TEXT("已拥有")) && !Hero->GetBackpackGatheringSource(3);
    TMap<AResourceNode*,int32> Stocks;AResourceNode* Renewable=nullptr;
    for(TActorIterator<AResourceNode> It(World);It;++It){Stocks.Add(*It,It->Remaining);It->Remaining=0;if(It->Definition && It->Definition->OutputItem==EFrontierItem::Material && It->Definition->RegrowSeconds>0)Renewable=*It;}
    Guidance &= !Hero->GetBackpackGatheringSource(0) && Hero->GetBackpackGatheringHint(0).Contains(TEXT("无可采来源"));
    if(Renewable)
    {const float Regrow=Renewable->RegrowRemaining;Renewable->RegrowRemaining=10;Guidance &= Hero->GetBackpackGatheringSource(0)==Renewable && Hero->GetBackpackGatheringHint(0).Contains(TEXT("再生"));Renewable->RegrowRemaining=Regrow;}
    else Guidance=false;
    for(const auto& Stock:Stocks)Stock.Key->Remaining=Stock.Value;
    Hero->Inventory->Restore({{EFrontierItem::Material,100},{EFrontierItem::CaptureDevice,999}}); Hero->ConfirmInterfaceAction();
    Reject &= Hero->Inventory->Count(EFrontierItem::Material)==100 && Hero->Inventory->Count(EFrontierItem::CaptureDevice)==999 && Hero->GetBackpackRecipeStatus(0).StartsWith(TEXT("产物已满"));
    Hero->Inventory->Restore({{EFrontierItem::Material,100}}); Hero->SetActorLocation(FVector(1200,-1000,40));
    auto* Trough=World->SpawnActor<AWorkStation>(Hero->GetActorLocation()+FVector(200,0,0),FRotator::ZeroRotator); Trough->RequiredSpecialty=EWorkSpecialty::Feeding;
    Hero->ConfirmInterfaceAction();
    Reject &= !Hero->IsNearWorkStation() && Hero->Inventory->Count(EFrontierItem::Material)==100 && Hero->GetBackpackRecipeStatus(0).Contains(TEXT("8米"));
    auto* AccessSite=Trough;AccessSite->RequiredSpecialty=EWorkSpecialty::Crafting;
    auto* AccessWall=CraftAccessWall(World);const auto AccessBag=Hero->Inventory->GetStacks();
    const bool Blocked=!Hero->IsNearWorkStation();const bool NotSpent=!Hero->CraftRecipe(TEXT("Medicine")) && Hero->Inventory->GetStacks().OrderIndependentCompareEqual(AccessBag) && Hero->Feedback.Contains(TEXT("无遮挡"));
    AccessWall->Destroy();FHitResult AccessHit;FCollisionQueryParams AccessQuery(SCENE_QUERY_STAT(CraftAccessDebug),false,Hero);World->LineTraceSingleByChannel(AccessHit,Hero->FirstPersonCamera->GetComponentLocation(),AccessSite->GetActorLocation()+FVector(0,0,30),ECC_Visibility,AccessQuery);UE_LOG(LogTemp,Display,TEXT("CF_WORKSHOP_ACCESS_TRACE camera=%s site=%s hit=%s location=%s destroying=%d"),*Hero->FirstPersonCamera->GetComponentLocation().ToString(),*AccessSite->GetActorLocation().ToString(),*GetNameSafe(AccessHit.GetActor()),*AccessHit.Location.ToString(),AccessHit.GetActor()?AccessHit.GetActor()->IsActorBeingDestroyed():false);const bool Visible=Hero->IsNearWorkStation() && Hero->CraftRecipe(TEXT("Medicine")) && Hero->Inventory->Count(EFrontierItem::Material)==95 && Hero->Inventory->Count(EFrontierItem::Medicine)==1;
    UE_LOG(LogTemp,Display,TEXT("CF_WORKSHOP_ACCESS_VISIBLE near=%d material=%d medicine=%d feedback=%s"),Hero->IsNearWorkStation(),Hero->Inventory->Count(EFrontierItem::Material),Hero->Inventory->Count(EFrontierItem::Medicine),*Hero->Feedback);
    Hero->SetActorLocation(FVector(2400,-1000,40));const bool Far=!Hero->IsNearWorkStation();AccessSite->Destroy();const bool Access=Blocked && NotSpent && Visible && Far;
    Hero->Inventory->Restore({{EFrontierItem::Material,100}});Hero->SetActorLocation(Base);
    Hero->TakeDamage(1000,FDamageEvent(),nullptr,nullptr); Hero->ConfirmInterfaceAction();
    Reject &= Hero->Health<=0 && !Hero->IsBackpackOpen() && Hero->Inventory->Count(EFrontierItem::Material)==100;
    Hero->Health=100;
    Hero->ToggleBackpack(); Hero->ToggleRoster(); bool Panels=!Hero->IsBackpackOpen() && Hero->IsRosterOpen();
    Hero->ToggleBackpack(); Panels &= !Hero->IsRosterOpen(); Hero->ToggleJournal(); Panels &= !Hero->IsBackpackOpen();
    Hero->ToggleBackpack(); Panels &= !Hero->IsJournalOpen(); Hero->ToggleHelp(); Panels &= !Hero->IsBackpackOpen();
    Hero->ToggleBackpack(); Hero->TogglePauseMenu(); Panels &= !Hero->IsBackpackOpen() && Hero->IsPauseMenuOpen(); Hero->TogglePauseMenu();
    Hero->Inventory->Restore({{EFrontierItem::Material,70},{EFrontierItem::Food,1}});
    const bool Saved=Hero->SaveProgress(TEXT("FrontierBackpackAutomation"));
    Hero->ToggleBackpack(); Hero->Inventory->Restore({}); Hero->Equipment->Restore({});
    const bool Loaded=Hero->LoadProgress(TEXT("FrontierBackpackAutomation"));
    const bool Persistence=Saved && Loaded && !Hero->IsBackpackOpen() && Hero->GetBackpackRecipe()==0 && Hero->Inventory->Count(EFrontierItem::Material)==70 && Hero->Equipment->GetState().bOwnSpear && Hero->Equipment->GetState().bOwnVest;
    const bool Passed=Craft && Reject && Panels && Persistence && Guidance && Access;
    UE_LOG(LogTemp,Display,TEXT("CF_WORKSHOP_ACCESS %s blocked=%d noSpend=%d visibleCraft=%d originalCost=5 farRejected=%d"),Access?TEXT("PASS"):TEXT("FAIL"),Blocked,NotSpent,Visible,Far);
    UE_LOG(LogTemp,Display,TEXT("CF_BACKPACK_GUIDANCE %s cases=7"),Guidance?TEXT("PASS"):TEXT("FAIL"));
    UE_LOG(LogTemp,Display,TEXT("CF_BACKPACK %s craft=%d reject=%d panels=%d persistence=%d"),Passed?TEXT("PASS"):TEXT("FAIL"),Craft,Reject,Panels,Persistence);
    if(Passed && FParse::Param(FCommandLine::Get(),TEXT("CFWorkshopAccessPreview")))
    {
        // 隔离的权限夹具，不作为真实开局或材料来源证据；使用实体墙和地板呈现阻挡。
        Hero->SetActorLocation(FVector(1200,-1000,40));Hero->Inventory->Restore({{EFrontierItem::Material,30}});Hero->ToggleBackpack();
        auto* Site=World->SpawnActor<AWorkStation>(FVector(1400,-1000,40),FRotator::ZeroRotator);Site->RequiredSpecialty=EWorkSpecialty::Crafting;
        auto* Wall=CraftAccessWall(World);CraftAccessWall(World,FVector(1200,-1000,-20),FVector(1000,1000,20));
        const auto Rotation=(Site->GetActorLocation()+FVector(0,0,30)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(Rotation);Hero->FirstPersonCamera->SetWorldRotation(Rotation);
        const FString Root=FPaths::ProjectSavedDir()/TEXT("WorkshopAccessPreview");IFileManager::Get().MakeDirectory(*Root,true);
        FTimerHandle BlockedShot,Open,ReadyShot,Done;
        World->GetTimerManager().SetTimer(BlockedShot,[Hero,Root](){Hero->CraftRecipe(TEXT("Medicine"));FScreenshotRequest::RequestScreenshot(Root/TEXT("Blocked.png"),false,false);},.6f,false);
        World->GetTimerManager().SetTimer(Open,[Wall](){Wall->Destroy();},1.2f,false);
        World->GetTimerManager().SetTimer(ReadyShot,[Hero,Root](){const bool OK=Hero->CraftRecipe(TEXT("Medicine")) && Hero->Inventory->Count(EFrontierItem::Material)==25;UE_LOG(LogTemp,Display,TEXT("CF_WORKSHOP_ACCESS_PREVIEW %s"),OK?TEXT("PASS"):TEXT("FAIL"));FScreenshotRequest::RequestScreenshot(Root/TEXT("Ready.png"),false,false);},1.8f,false);
        World->GetTimerManager().SetTimer(Done,[](){FPlatformMisc::RequestExitWithStatus(false,0);},2.5f,false);return;
    }
    if(Passed && FParse::Param(FCommandLine::Get(),TEXT("CFBackpackPreview")))
    {
        Hero->ToggleBackpack(); const FString Root=FPaths::ProjectSavedDir()/TEXT("BackpackPreview"); IFileManager::Get().MakeDirectory(*Root,true);
        FTimerHandle Shot,Done;
        World->GetTimerManager().SetTimer(Shot,[Root]() { FScreenshotRequest::RequestScreenshot(Root/TEXT("backpack.png"),false,false); },3.f,false);
        World->GetTimerManager().SetTimer(Done,[]() { FPlatformMisc::RequestExitWithStatus(false,0); },5.f,false); return;
    }
    FPlatformMisc::RequestExitWithStatus(false,Passed?0:1);
}
