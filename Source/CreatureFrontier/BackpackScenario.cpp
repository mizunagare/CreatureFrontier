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
    Hero->Inventory->Restore({{EFrontierItem::Material,100}}); Hero->SetActorLocation(FVector(0,-3000,40));
    auto* Trough=World->SpawnActor<AWorkStation>(Hero->GetActorLocation()+FVector(200,0,0),FRotator::ZeroRotator); Trough->RequiredSpecialty=EWorkSpecialty::Feeding;
    Hero->ConfirmInterfaceAction();
    Reject &= !Hero->IsNearWorkStation() && Hero->Inventory->Count(EFrontierItem::Material)==100 && Hero->GetBackpackRecipeStatus(0).Contains(TEXT("8米"));
    Trough->Destroy(); Hero->SetActorLocation(Base);
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
    const bool Passed=Craft && Reject && Panels && Persistence && Guidance;
    UE_LOG(LogTemp,Display,TEXT("CF_BACKPACK_GUIDANCE %s cases=7"),Guidance?TEXT("PASS"):TEXT("FAIL"));
    UE_LOG(LogTemp,Display,TEXT("CF_BACKPACK %s craft=%d reject=%d panels=%d persistence=%d"),Passed?TEXT("PASS"):TEXT("FAIL"),Craft,Reject,Panels,Persistence);
    if(Passed && FParse::Param(FCommandLine::Get(),TEXT("CFBackpackPreview")))
    {
        Hero->ToggleBackpack(); const FString Root=FPaths::ProjectSavedDir()/TEXT("BackpackPreview"); IFileManager::Get().MakeDirectory(*Root,true);
        FTimerHandle Shot,Done;
        World->GetTimerManager().SetTimer(Shot,[Root]() { FScreenshotRequest::RequestScreenshot(Root/TEXT("backpack.png"),false,false); },3.f,false);
        World->GetTimerManager().SetTimer(Done,[]() { FPlatformMisc::RequestExitWithStatus(false,0); },5.f,false); return;
    }
    FPlatformMisc::RequestExitWithStatus(false,Passed?0:1);
}
