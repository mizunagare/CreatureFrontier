#include "FarmingScenario.h"
#include "CFPlayerCharacter.h"
#include "BaseBuildingComponent.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "WorkStation.h"
#include "InventoryComponent.h"
#include "FrontierSaveGame.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/HUD.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

static UCreatureDefinition* FindFarmer(UWorld* World)
{
    for (TActorIterator<ACreatureCharacter> It(World); It; ++It)
        if (It->Definition && It->Definition->Form == ECreatureForm::Leaf) return It->Definition;
    return nullptr;
}

void RunFarmingScenario(UWorld* World, ACFPlayerCharacter* Hero)
{
    auto* Definition = FindFarmer(World);
    if (!Definition) { UE_LOG(LogTemp,Error,TEXT("CF_FARM FAIL definition")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    for (TActorIterator<ACreatureCharacter> It(World); It; ++It) It->Destroy();
    Hero->SetActorLocation(FVector(-350,-1050,40)); Hero->GetCharacterMovement()->DisableMovement();
    Hero->Inventory->Restore({{EFrontierItem::Material,80},{EFrontierItem::Food,2}});
    Hero->Building->Selected = EWorkSpecialty::Farming;
    auto* Garden = Hero->Building->BuildAt(FTransform(FRotator::ZeroRotator,FVector(0,-700,-15),FVector(2,1,.7)));
    if (!Garden) { UE_LOG(LogTemp,Error,TEXT("CF_FARM FAIL build %s"),*Hero->Feedback); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    bool Build = Hero->Inventory->Count(EFrontierItem::Material) == 62 && Definition->WorkSpecialties.Contains(EWorkSpecialty::Farming);
    TArray<UStaticMeshComponent*> Pieces; Garden->GetComponents(Pieces);
    int32 Plants = 0;
    for (auto* Piece : Pieces) if (Piece->GetName().StartsWith(TEXT("FarmPlant")) && Piece->IsVisible()) ++Plants;
    Build &= Plants == 6;
    const FTransform Transform(FRotator::ZeroRotator,FVector(0,-900,40));
    auto* Pet = World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    Pet->Definition = Definition; Pet->FinishSpawning(Transform); Pet->Tame(Hero->GetController());
    Pet->GetCharacterMovement()->DisableMovement(); Pet->SetActorTickEnabled(false);
    bool Reject = !Hero->FeedCompanion(Pet); // Full satiety.
    Pet->Nutrition = 30; Pet->SetActorLocation(FVector(-1500,-1000,40));
    Reject &= !Hero->FeedCompanion(Pet); Pet->SetActorLocation(Transform.GetLocation());
    auto* Wall = World->SpawnActor<AStaticMeshActor>(FVector(-170,-975,75),FRotator::ZeroRotator);
    Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
    Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Wall->SetActorScale3D(FVector(.3,1,2));
    Reject &= !Hero->FeedCompanion(Pet); Wall->Destroy();
    Pet->Health = 0; Reject &= !Hero->FeedCompanion(Pet); Pet->Health = Pet->GetMaxHealth();
    Reject &= Hero->Inventory->Count(EFrontierItem::Food) == 2;
    Pet->Nutrition = 100; Pet->WorkSite = Garden; Pet->SetActivity(ECreatureActivity::Work);
    Pet->Tick(1); const bool Growing = Hero->Inventory->Count(EFrontierItem::Food) == 2;
    Pet->Tick(2.4f);
    const bool Harvest = Growing && Hero->Inventory->Count(EFrontierItem::Food) == 3 && Pet->Experience == 1 && Pet->Nutrition < 100;
    Pet->Nutrition = 10; Pet->Stamina = 100;
    Pet->Tick(3.4f); const bool Slower = Hero->Inventory->Count(EFrontierItem::Food) == 3;
    Pet->Tick(3.4f);
    const bool Hungry = Slower && Hero->Inventory->Count(EFrontierItem::Food) == 4;
    // Switching jobs cannot turn accumulated garden progress into gathered materials.
    auto* Other = World->SpawnActor<AWorkStation>(FVector(0,-1050,-15),FRotator::ZeroRotator);
    Pet->Nutrition = 100; Pet->Stamina = 100;
    Pet->Tick(1);
    Pet->WorkSite = Other; Pet->Tick(.8f);
    const bool Switch = Hero->Inventory->Count(EFrontierItem::Material) == 62;
    Pet->WorkSite = Garden; Pet->SetActivity(ECreatureActivity::Rest); Pet->SetActivity(ECreatureActivity::Work);
    Other->Destroy();
    Pet->Nutrition = 100; Pet->Stamina = 100;
    Hero->Inventory->Restore({{EFrontierItem::Material,62},{EFrontierItem::Food,999}});
    const int32 XP = Pet->Experience;
    Pet->Tick(3.4f);
    bool Full = Hero->Inventory->Count(EFrontierItem::Food) == 999 && Pet->Experience == XP;
    Hero->Inventory->Consume(EFrontierItem::Food,1); Pet->Tick(.1f);
    Full &= Hero->Inventory->Count(EFrontierItem::Food) == 999 && Pet->Experience == XP+1;
    Pet->Nutrition = .01f; Pet->Tick(.2f);
    const bool Starve = Pet->Nutrition == 0 && Pet->Activity == ECreatureActivity::Rest && !Pet->IsSpecialReady() && Pet->Health == Pet->GetMaxHealth();
    Hero->Inventory->Restore({{EFrontierItem::Material,62}});
    Reject &= !Hero->FeedCompanion(Pet);
    Hero->CraftFood();
    const bool Craft = Hero->Inventory->Count(EFrontierItem::Material) == 60 && Hero->Inventory->Count(EFrontierItem::Food) == 1;
    Pet->Stamina = 20;
    bool Feed = Hero->FeedCompanion(Pet) && Pet->Nutrition == 35 && Pet->Stamina == 35 && Hero->Inventory->Count(EFrontierItem::Food) == 0;
    Feed &= !Hero->FeedCompanion(Pet);
    Hero->CommandWork(); Pet->Tick(3.4f);
    Feed &= Pet->Activity == ECreatureActivity::Work && Hero->Inventory->Count(EFrontierItem::Food) == 1;
    Pet->Nutrition = 55;
    const FString Slot = TEXT("FrontierFarmingAutomation");
    bool Saved = Hero->SaveProgress(Slot) && Hero->LoadProgress(Slot);
    for (TActorIterator<ACreatureCharacter> It(World); It; ++It)
        if (It->Tamer == Hero->GetController()) Saved &= It->Nutrition == 55 && It->WorkSite && It->WorkSite->RequiredSpecialty == EWorkSpecialty::Farming;
    Saved &= Hero->Inventory->Count(EFrontierItem::Food) == 1;
    auto* Legacy = Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0));
    bool Migration = Legacy != nullptr;
    if (Legacy)
    {
        Legacy->FormatVersion = 8; Legacy->InventoryStacks.Remove(EFrontierItem::Food); Legacy->Facilities.Empty();
        for (auto& Record : Legacy->Creatures) { Record.Nutrition = 100; Record.WorkSiteId.Invalidate(); Record.WorkSiteName = NAME_None; Record.Activity = ECreatureActivity::Follow; }
        Migration &= UGameplayStatics::SaveGameToSlot(Legacy,TEXT("FrontierLegacyFoodAutomation"),0) && Hero->LoadProgress(TEXT("FrontierLegacyFoodAutomation"));
        Migration &= Hero->Inventory->Count(EFrontierItem::Food) == 3;
    }
    ACreatureCharacter* Restored = nullptr;
    for (TActorIterator<ACreatureCharacter> It(World); It; ++It) if (It->Tamer == Hero->GetController()) Restored = *It;
    if (!Restored) { UE_LOG(LogTemp,Error,TEXT("CF_FARM FAIL restore")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Migration &= Restored->Nutrition == 100;
    Restored->SetActorTickEnabled(false); Restored->GetCharacterMovement()->DisableMovement();
    Restored->Nutrition = 80; Restored->Stamina = 99;
    FTimerHandle Cap;
    World->GetTimerManager().SetTimer(Cap,[Hero,Restored,Build,Reject,Harvest,Hungry,Switch,Full,Starve,Craft,Feed,Saved,Migration]()
    {
        const bool Clamped = Hero->FeedCompanion(Restored) && Restored->Nutrition == 100 && Restored->Stamina == 100 && Hero->Inventory->Count(EFrontierItem::Food) == 2;
        const bool Pass = Build && Reject && Harvest && Hungry && Switch && Full && Starve && Craft && Feed && Saved && Migration && Clamped;
        UE_LOG(LogTemp,Display,TEXT("CF_FARM %s build=%d reject=%d harvest=%d hungry=%d switch=%d full=%d starve=%d craft=%d feed=%d save=%d migration=%d clamp=%d"),
            Pass ? TEXT("PASS") : TEXT("FAIL"),Build,Reject,Harvest,Hungry,Switch,Full,Starve,Craft,Feed,Saved,Migration,Clamped);
        FPlatformMisc::RequestExitWithStatus(false,Pass ? 0 : 1);
    },3.2f,false);
}

void RunFarmPreview(UWorld* World, ACFPlayerCharacter* Hero)
{
    auto* Definition = FindFarmer(World);
    Hero->SetActorLocation(FVector(-350,-1050,40)); Hero->GetCharacterMovement()->DisableMovement();
    Hero->Inventory->Restore({{EFrontierItem::Material,100}}); Hero->Building->Selected = EWorkSpecialty::Farming;
    auto* Garden = Hero->Building->BuildAt(FTransform(FRotator::ZeroRotator,FVector(0,-700,-15),FVector(2,1,.7)));
    if (!Garden || !Definition) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    auto* Pet = World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),FTransform(FRotator(0,-37,0),FVector(-160,-700,40)),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    Pet->Definition = Definition; Pet->FinishSpawning(FTransform(FRotator(0,-37,0),FVector(-160,-700,40))); Pet->Tame(Hero->GetController());
    for (TActorIterator<ACreatureCharacter> It(World); It; ++It) { It->SetActorTickEnabled(false); It->GetCharacterMovement()->DisableMovement(); }
    auto* PC = Cast<APlayerController>(Hero->GetController());
    auto* Camera = World->SpawnActor<ACameraActor>();
    const FVector Eye(300,-1050,250);
    Camera->SetActorLocation(Eye); Camera->SetActorRotation((FVector(0,-700,20)-Eye).Rotation()); Camera->GetCameraComponent()->FieldOfView = 65;
    if (PC) { if (PC->GetHUD()) PC->GetHUD()->bShowHUD = false; PC->SetViewTarget(Camera); }
    if (auto* Label = Garden->FindComponentByClass<UTextRenderComponent>()) Label->SetWorldRotation(FRotator(0,(Eye-Label->GetComponentLocation()).Rotation().Yaw,0));
    const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("FarmPreview/farm_garden.png"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
    FTimerHandle Shot,End;
    World->GetTimerManager().SetTimer(Shot,[Path]() { FScreenshotRequest::RequestScreenshot(Path,false,false); },4.0f,false);
    World->GetTimerManager().SetTimer(End,[Path]()
    { const bool Pass = IFileManager::Get().FileSize(*Path) > 1000; UE_LOG(LogTemp,Display,TEXT("CF_FARM_PREVIEW %s %s"),Pass ? TEXT("PASS") : TEXT("FAIL"),*Path); FPlatformMisc::RequestExitWithStatus(false,Pass ? 0 : 1); },6.0f,false);
}
