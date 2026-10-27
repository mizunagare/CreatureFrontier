#include "JournalScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "CaptureComponent.h"
#include "FrontierSaveGame.h"
#include "InventoryComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "TimerManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

void RunJournalScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    TMap<ECreatureForm,UCreatureDefinition*> Definitions;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It) { if(It->Definition) Definitions.Add(It->Definition->Form,It->Definition); It->Destroy(); }
    if(Definitions.Num()!=3) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Hero->SetActorLocation(FVector(-350,-1050,40)); Hero->GetCharacterMovement()->DisableMovement();
    const FString EmptySlot=TEXT("FrontierJournalEmptyAutomation"); Hero->SaveProgress(EmptySlot);
    auto* Empty=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(EmptySlot,0)); Empty->Journal.Empty(); Empty->Creatures.Empty();
    UGameplayStatics::SaveGameToSlot(Empty,EmptySlot,0); Hero->LoadProgress(EmptySlot); Hero->GetCharacterMovement()->DisableMovement();
    Hero->ToggleJournal(); Hero->NextJournalPage(); Hero->PreviousJournalPage();
    bool UI=Hero->IsJournalOpen() && Hero->GetJournalPage()==0; Hero->ToggleJournal();
    auto Make=[&](ECreatureForm Form,FVector Position)
    {
        const FTransform Transform(FRotator::ZeroRotator,Position);
        auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Pet->Definition=Definitions[Form]; Pet->FinishSpawning(Transform); Pet->SetActorTickEnabled(false); Pet->GetCharacterMovement()->DisableMovement(); return Pet;
    };
    auto* Leaf=Make(ECreatureForm::Leaf,FVector(0,-900,40));
    auto* Ember=Make(ECreatureForm::Ember,FVector(-900,-900,40));
    auto* Stone=Make(ECreatureForm::Stone,FVector(1500,-900,40));
    bool Sight=Hero->GetJournal().IsEmpty();
    Hero->FirstPersonCamera->SetWorldRotation((Leaf->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());
    Hero->ObserveAimedCreature();
    Sight &= Hero->HasJournalEntry(Definitions[ECreatureForm::Leaf]) && !Hero->HasJournalEntry(Definitions[ECreatureForm::Leaf],true);
    Hero->ObserveAimedCreature(); Sight &= Hero->GetJournal().Num()==1;
    bool Reject=!Hero->ObserveCreature(Stone) && !Hero->HasJournalEntry(Definitions[ECreatureForm::Stone]);
    Hero->Health=0; Reject &= !Hero->ObserveCreature(Ember); Hero->Health=100;
    Ember->Health=0; Reject &= !Hero->ObserveCreature(Ember); Ember->Health=80;
    const FVector Old=Ember->GetActorLocation(); Ember->SetActorLocation(FVector(-900,-900,40));
    auto* Wall=World->SpawnActor<AActor>(); auto* Mesh=NewObject<UStaticMeshComponent>(Wall);
    Wall->SetRootComponent(Mesh); Mesh->RegisterComponent();
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"))); Mesh->SetCollisionProfileName(TEXT("BlockAll"));
    Wall->SetActorLocation(FVector(-650,-970,80)); Wall->SetActorScale3D(FVector(1,4,4));
    Reject &= !Hero->ObserveCreature(Ember); Wall->Destroy(); Ember->SetActorLocation(Old);
    Hero->CaptureComponent->OnCaptureResolved.Broadcast(Leaf,false);
    bool Capture=!Hero->HasJournalEntry(Definitions[ECreatureForm::Leaf],true);
    Hero->Inventory->Restore({{EFrontierItem::CaptureDevice,5}}); Hero->CaptureComponent->CapturePower=100;
    int32 Seed=1; for(;Seed<100;++Seed) { FMath::RandInit(Seed); if(FMath::FRand()<.95f) break; } FMath::RandInit(Seed);
    Hero->CaptureComponent->TryCapture(Leaf);
    Capture &= Leaf->Tamer==Hero->GetController() && Hero->HasJournalEntry(Definitions[ECreatureForm::Leaf],true) && Hero->GetJournal().Num()==1;
    Hero->ObserveCreature(Ember);
    const FString Slot=TEXT("FrontierJournalAutomation");
    bool Saved=Hero->SaveProgress(Slot) && Hero->LoadProgress(Slot);
    Saved &= Hero->GetJournal().Num()==2 && Hero->HasJournalEntry(Definitions[ECreatureForm::Leaf],true) && Hero->HasJournalEntry(Definitions[ECreatureForm::Ember]) && !Hero->HasJournalEntry(Definitions[ECreatureForm::Ember],true);
    for(TActorIterator<ACreatureCharacter> It(World);It;++It) if(It->Tamer==Hero->GetController()) It->Destroy();
    Saved &= Hero->SaveProgress(TEXT("FrontierJournalHistory")) && Hero->LoadProgress(TEXT("FrontierJournalHistory")) && Hero->HasJournalEntry(Definitions[ECreatureForm::Leaf],true);
    Saved &= Hero->LoadProgress(Slot);
    UI &= !Hero->IsJournalOpen(); Hero->ToggleJournal(); Hero->NextJournalPage(); UI &= Hero->IsJournalOpen() && Hero->GetJournalPage()==1;
    Hero->NextJournalPage(); UI &= Hero->GetJournalPage()==0; Hero->PreviousJournalPage(); UI &= Hero->GetJournalPage()==1;
    Hero->ToggleRoster(); UI &= Hero->IsRosterOpen() && !Hero->IsJournalOpen(); Hero->ToggleJournal(); UI &= Hero->IsJournalOpen() && !Hero->IsRosterOpen();
    if(FParse::Param(FCommandLine::Get(),TEXT("CFJournalPreview")))
    {
        Hero->ToggleJournal(); Hero->ToggleJournal();
        const FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("JournalPreview")); IFileManager::Get().MakeDirectory(*Root,true);
        FTimerHandle Shot,Page,Shot2,Done;
        World->GetTimerManager().SetTimer(Shot,[Root](){FScreenshotRequest::RequestScreenshot(Root/TEXT("entry1.png"),false,false);},3.f,false);
        World->GetTimerManager().SetTimer(Page,[Hero](){Hero->NextJournalPage();},4.f,false);
        World->GetTimerManager().SetTimer(Shot2,[Root](){FScreenshotRequest::RequestScreenshot(Root/TEXT("entry2.png"),false,false);},6.f,false);
        World->GetTimerManager().SetTimer(Done,[](){FPlatformMisc::RequestExitWithStatus(false,0);},8.f,false); return;
    }
    auto* Invalid=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0)); const auto Duplicate=Invalid->Journal[0]; Invalid->Journal.Add(Duplicate);
    UGameplayStatics::SaveGameToSlot(Invalid,TEXT("FrontierJournalInvalid"),0);
    const float OldHealth=Hero->Health; const int32 OldCount=Hero->GetJournal().Num();
    bool InvalidRejected=!Hero->LoadProgress(TEXT("FrontierJournalInvalid")) && Hero->Health==OldHealth && Hero->GetJournal().Num()==OldCount;
    Invalid->Journal[0].Definition.Reset(); UGameplayStatics::SaveGameToSlot(Invalid,TEXT("FrontierJournalInvalid"),0);
    InvalidRejected &= !Hero->LoadProgress(TEXT("FrontierJournalInvalid")) && Hero->GetJournal().Num()==OldCount;
    Invalid->Journal.SetNum(129); UGameplayStatics::SaveGameToSlot(Invalid,TEXT("FrontierJournalInvalid"),0);
    InvalidRejected &= !Hero->LoadProgress(TEXT("FrontierJournalInvalid")) && Hero->GetJournal().Num()==OldCount;
    auto* Legacy=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0)); Legacy->FormatVersion=11; Legacy->Journal.Empty();
    UGameplayStatics::SaveGameToSlot(Legacy,TEXT("FrontierJournalLegacy"),0);
    bool Migration=Hero->LoadProgress(TEXT("FrontierJournalLegacy")) && Hero->GetJournal().Num()==1 && Hero->HasJournalEntry(Definitions[ECreatureForm::Leaf],true) && !Hero->HasJournalEntry(Definitions[ECreatureForm::Ember]);
    const bool Pass=Sight && Reject && Capture && Saved && UI && InvalidRejected && Migration;
    UE_LOG(LogTemp,Display,TEXT("CF_JOURNAL %s sight=%d reject=%d capture=%d save=%d ui=%d invalid=%d migration=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Sight,Reject,Capture,Saved,UI,InvalidRejected,Migration);
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}
