#include "CFPlayerCharacter.h"
#include "WorkStation.h"
#include "CreatureCharacter.h"
#include "InventoryComponent.h"
#include "InputRoutingQA.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

void RunFoodPortionScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    const bool Read=FParse::Param(FCommandLine::Get(),TEXT("CFFoodPortionRead")),Rules=FParse::Param(FCommandLine::Get(),TEXT("CFFoodPortionRules"));
    if(!Hero->LoadProgress(TEXT("FoodPortionInput"))){UE_LOG(LogTemp,Error,TEXT("CF_FOOD_PORTION FAIL source"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    AWorkStation* Site=nullptr;for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt && It->RequiredSpecialty==EWorkSpecialty::Feeding)Site=*It;
    if(!Site || Site->StoredFood!=(Read?2:3) || Hero->Inventory->Count(EFrontierItem::Food)!=(Read?1:0)){UE_LOG(LogTemp,Error,TEXT("CF_FOOD_PORTION FAIL source-budget"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    Hero->FirstPersonCamera->SetWorldRotation((Site->GetActorLocation()+FVector(0,0,25)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());
    if(Hero->TraceFacility()!=Site || !CFInputQA::UniqueBindings(Hero)){UE_LOG(LogTemp,Error,TEXT("CF_FOOD_PORTION FAIL aim-binding"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    if(Rules)
    {
        // 以下为明确库存／设施分类夹具，不作真实资源来源或农业产出证据，不保存注入状态。
        const auto Original=Hero->Inventory->GetStacks();auto SetFood=[&](int32 Amount){auto Items=Original;Items.Add(EFrontierItem::Food,Amount);Hero->Inventory->Restore(Items);};
        SetFood(0);Site->StoredFood=6;const bool Bulk=CFInputQA::Press(Hero,EKeys::O) && Site->StoredFood==1 && Hero->Inventory->Count(EFrontierItem::Food)==5;
        SetFood(6);Site->StoredFood=2;const bool Deposit=CFInputQA::Press(Hero,EKeys::U) && Site->StoredFood==7 && Hero->Inventory->Count(EFrontierItem::Food)==1;
        SetFood(999);Site->StoredFood=3;const bool Full=!Site->TransferFood(Hero,false,true) && Site->StoredFood==3 && Hero->Inventory->Count(EFrontierItem::Food)==999;
        SetFood(0);Site->StoredFood=0;const bool Empty=!Site->TransferFood(Hero,false,true) && Hero->Inventory->Count(EFrontierItem::Food)==0;
        Site->StoredFood=3;Site->RequiredSpecialty=EWorkSpecialty::Crafting;const bool Wrong=!Site->TransferFood(Hero,false,true) && Site->StoredFood==3 && Hero->Inventory->Count(EFrontierItem::Food)==0;
        Site->RequiredSpecialty=EWorkSpecialty::Farming;const bool Farm=CFInputQA::ShiftPress(Hero,EKeys::O) && Site->StoredFood==2 && Hero->Inventory->Count(EFrontierItem::Food)==1;
        Site->RequiredSpecialty=EWorkSpecialty::Feeding;Site->StoredFood=3;SetFood(0);
        auto* Wall=World->SpawnActor<AStaticMeshActor>();auto* Mesh=Wall->GetStaticMeshComponent();Mesh->SetMobility(EComponentMobility::Movable);Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Mesh->SetCollisionProfileName(TEXT("BlockAll"));Wall->SetActorTransform(FTransform(FRotator::ZeroRotator,(Hero->GetPawnViewLocation()+Site->GetActorLocation())*.5,FVector(.4,.4,2)));
        const bool Occluded=!Site->TransferFood(Hero,false,true) && Site->StoredFood==3 && Hero->Inventory->Count(EFrontierItem::Food)==0;
        Wall->Destroy();const bool Pass=Bulk && Deposit && Full && Empty && Wrong && Farm && Occluded;
        UE_LOG(LogTemp,Display,TEXT("CF_FOOD_PORTION_RULES %s bulk=%d deposit=%d full=%d empty=%d wrong=%d farm=%d occluded=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Bulk,Deposit,Full,Empty,Wrong,Farm,Occluded);FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return;
    }
    auto Expected=Hero->Inventory->GetStacks();Expected.Add(EFrontierItem::Food,1);const FGuid Id=Site->PersistentId;const float HP=Hero->Health;
    if(!Read && (!CFInputQA::ShiftPress(Hero,EKeys::O) || Site->StoredFood!=2 || Hero->Inventory->Count(EFrontierItem::Food)!=1)){UE_LOG(LogTemp,Error,TEXT("CF_FOOD_PORTION FAIL single-input"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    for(int32 I=0;I<static_cast<int32>(EFrontierItem::Count);++I)if(Hero->Inventory->Count(static_cast<EFrontierItem>(I))!=Expected.FindRef(static_cast<EFrontierItem>(I))){UE_LOG(LogTemp,Error,TEXT("CF_FOOD_PORTION FAIL inventory"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    const bool Preview=FParse::Param(FCommandLine::Get(),TEXT("CFFoodPortionPreview"));
    const FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("FoodPortionPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));
    if(Preview){IFileManager::Get().MakeDirectory(*Root,true);UE_LOG(LogTemp,Display,TEXT("CF_FOOD_PORTION_PREVIEW_ROOT %s"),*Root);FTimerHandle Shot;World->GetTimerManager().SetTimer(Shot,[Root](){FScreenshotRequest::RequestScreenshot(Root/TEXT("Single.png"),false,false);},.2f,false);}
    FTimerHandle End;
    World->GetTimerManager().SetTimer(End,[World,Hero,Id,HP,Expected,Preview,Root]()
    {
        bool Pass=Hero->Health==HP && Hero->SaveProgress(TEXT("FoodPortionDone")) && Hero->LoadProgress(TEXT("FoodPortionDone"));
        AWorkStation* Restored=nullptr;for(TActorIterator<AWorkStation> It(World);It;++It)if(It->PersistentId==Id)Restored=*It;
        Pass &= Restored && Restored->StoredFood==2;for(int32 I=0;I<static_cast<int32>(EFrontierItem::Count);++I)Pass &= Hero->Inventory->Count(static_cast<EFrontierItem>(I))==Expected.FindRef(static_cast<EFrontierItem>(I));
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Health>0)Pass &= It->IsActorTickEnabled() && It->GetActorEnableCollision() && It->GetCharacterMovement()->MovementMode!=MOVE_None;
        if(Preview)Pass &= IFileManager::Get().FileSize(*(Root/TEXT("Single.png")))>1000;
        UE_LOG(LogTemp,Display,TEXT("CF_FOOD_PORTION %s bagFood=1 stock=2 conserved=3 saved=1"),Pass?TEXT("PASS"):TEXT("FAIL"));FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    },Preview?1.f:.1f,false);
}
