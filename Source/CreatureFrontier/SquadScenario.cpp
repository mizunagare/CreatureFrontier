#include "SquadScenario.h"
#include "CFPlayerCharacter.h"
#include "InputRoutingQA.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "WorkStation.h"
#include "BaseBuildingComponent.h"
#include "InventoryComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "TimerManager.h"

void RunSquadScenario(UWorld* World, ACFPlayerCharacter* Hero)
{
    TMap<ECreatureForm,UCreatureDefinition*> Definitions;
    for (TActorIterator<ACreatureCharacter> It(World); It; ++It) { if (It->Definition) Definitions.Add(It->Definition->Form,It->Definition); It->Destroy(); }
    if (Definitions.Num() != 3) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Hero->SetActorLocation(FVector(-350,-1050,40)); Hero->GetCharacterMovement()->DisableMovement(); Hero->Inventory->Restore({{EFrontierItem::Material,100}});
    Hero->Building->Selected = EWorkSpecialty::Farming;
    auto* Garden = Hero->Building->BuildAt(FTransform(FRotator::ZeroRotator,FVector(0,-700,-15),FVector(2,1,.7)));
    AWorkStation* Mine = nullptr;
    for (TActorIterator<AWorkStation> It(World); It; ++It) if (It->RequiredSpecialty == EWorkSpecialty::Mining) Mine = *It;
    if (!Garden || !Mine) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    auto Make = [&](ECreatureForm Form,FVector Position)
    {
        const FTransform Transform(FRotator::ZeroRotator,Position);
        auto* Pet = World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Pet->Definition = Definitions[Form]; Pet->FinishSpawning(Transform); Pet->Tame(Hero->GetController());
        Pet->SetActorTickEnabled(false); Pet->GetCharacterMovement()->DisableMovement(); return Pet;
    };
    auto* Leaf = Make(ECreatureForm::Leaf,FVector(0,-900,40));
    auto* Stone = Make(ECreatureForm::Stone,FVector(-500,-900,40));
    auto* Ember = Make(ECreatureForm::Ember,FVector(-900,-900,40));
    if (FParse::Param(FCommandLine::Get(),TEXT("CFRosterPreview")))
    {
        for (int32 I=0; I<8; ++I) Make(ECreatureForm::Leaf,FVector(400+I*100,-1500,40));
        Leaf->WorkSite=Garden; Leaf->SetActivity(ECreatureActivity::Work);
        Stone->Health=0; Ember->Nutrition=24; Ember->Stamina=33;
        Hero->ToggleRoster();
        const FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("RosterPreview"));
        IFileManager::Get().MakeDirectory(*Root,true);
        FTimerHandle Shot,Page,SecondShot,Done;
        World->GetTimerManager().SetTimer(Shot,[Root]() { FScreenshotRequest::RequestScreenshot(Root/TEXT("team.png"),false,false); },3.f,false);
        World->GetTimerManager().SetTimer(Page,[Hero]() { for(int32 I=0;I<8;++I) Hero->NextRosterCompanion(); },4.f,false);
        World->GetTimerManager().SetTimer(SecondShot,[Root]() { FScreenshotRequest::RequestScreenshot(Root/TEXT("team_page2.png"),false,false); },6.f,false);
        World->GetTimerManager().SetTimer(Done,[Root]() { FPlatformMisc::RequestExitWithStatus(false,IFileManager::Get().FileSize(*(Root/TEXT("team.png")))>1000 && IFileManager::Get().FileSize(*(Root/TEXT("team_page2.png")))>1000?0:1); },8.f,false);
        return;
    }
    const auto Roster = Hero->GetCompanionRoster();
    Hero->NextRosterCompanion();
    bool RosterPass = !Hero->GetRosterHighlight() && Roster.Num()==3;
    Hero->ToggleRoster();
    RosterPass &= Hero->IsRosterOpen() && Hero->GetRosterHighlight()==Roster[0];
    Hero->PreviousRosterCompanion(); RosterPass &= Hero->GetRosterHighlight()==Roster.Last();
    Hero->NextRosterCompanion(); RosterPass &= Hero->GetRosterHighlight()==Roster[0];
    Hero->SelectRosterCompanion(); RosterPass &= Hero->IsCommandTarget(Roster[0]);
    Hero->ToggleRoster(); Hero->NextRosterCompanion();
    RosterPass &= !Hero->IsRosterOpen() && Hero->GetRosterHighlight()==Roster[0];
    auto Aim = [&](AActor* Target) { Hero->FirstPersonCamera->SetWorldRotation((Target->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation()); };
    Aim(Leaf); Hero->SelectAimedCompanion();
    bool Select = Hero->IsCommandTarget(Leaf) && !Hero->IsCommandTarget(Stone) && !Hero->IsCommandTarget(Ember);
    Hero->CommandWork();
    bool Split = Leaf->WorkSite == Garden && Leaf->Activity == ECreatureActivity::Work && !Stone->WorkSite && !Ember->WorkSite;
    Select &= Hero->SelectCompanion(Stone); Hero->SetActorLocation(FVector(-650,-500,40)); Hero->CommandWork();
    Split &= Stone->WorkSite == Mine && Stone->Activity == ECreatureActivity::Work && Leaf->WorkSite == Garden && Leaf->Activity == ECreatureActivity::Work;
    Hero->CommandRest();
    bool ScopedRest = Stone->Activity == ECreatureActivity::Rest && Leaf->Activity == ECreatureActivity::Work;
    Hero->CommandFollow();
    ScopedRest &= Stone->Activity == ECreatureActivity::Follow && !Stone->WorkSite && Leaf->WorkSite == Garden;
    Select &= Hero->SelectCompanion(Ember);
    auto* Enemy = World->SpawnActor<ACreatureCharacter>(FVector(-650,-1100,40),FRotator::ZeroRotator);
    Enemy->Health = 1000; Enemy->SetActorTickEnabled(false); Enemy->GetCharacterMovement()->DisableMovement();
    Aim(Enemy); Hero->CommandFocus();
    bool Combat = Ember->FocusTarget == Enemy && !Leaf->FocusTarget && !Stone->FocusTarget;
    const bool RoutedEmber=CFInputQA::Press(Hero,EKeys::G); Ember->Tick(0);
    Combat &= RoutedEmber && Ember->SpecialWindupRemaining > 0 && Ember->Stamina == 80 && Leaf->Stamina == 100 && Stone->Stamina == 100;
    const bool RoutedStone=CFInputQA::Press(Hero,EKeys::G); Stone->Tick(0);
    Combat &= RoutedStone && Stone->SpecialWindupRemaining == 0 && Stone->Stamina == 100;
    bool Reject = !Hero->SelectCompanion(Enemy) && Hero->IsCommandTarget(Ember);
    const FVector Old = Leaf->GetActorLocation(); Leaf->SetActorLocation(FVector(1500,-900,40));
    Reject &= !Hero->SelectCompanion(Leaf) && Hero->IsCommandTarget(Ember); Leaf->SetActorLocation(Old);
    Ember->Health = 0; Hero->CommandFollow();
    bool Lost = !Hero->IsCommandTarget(Leaf) && !Hero->IsCommandTarget(Stone) && !Hero->IsCommandTarget(Ember) && Leaf->Activity == ECreatureActivity::Work;
    Ember->Destroy(); Hero->CommandFollow();
    Lost &= !Hero->IsCommandTarget(Leaf) && Leaf->WorkSite == Garden;
    Hero->ClearCompanionSelection(); const bool All = Hero->IsCommandTarget(Leaf) && Hero->IsCommandTarget(Stone);
    Hero->SelectCompanion(Leaf);
    const FString Slot = TEXT("FrontierSquadAutomation");
    bool Saved = Hero->SaveProgress(Slot) && Hero->LoadProgress(Slot);
    Leaf = nullptr; Stone = nullptr;
    for (TActorIterator<ACreatureCharacter> It(World); It; ++It) if (It->Tamer == Hero->GetController())
    { if (It->Definition->Form == ECreatureForm::Leaf) Leaf = *It; if (It->Definition->Form == ECreatureForm::Stone) Stone = *It; }
    Saved &= Leaf && Stone && Leaf->WorkSite && Leaf->WorkSite->RequiredSpecialty == EWorkSpecialty::Farming && !Hero->IsCommandTarget(Leaf) && !Hero->IsCommandTarget(Stone);
    RosterPass &= !Hero->GetRosterHighlight() && Hero->GetCompanionRoster().Num()==2;
    Hero->ToggleRoster(); RosterPass &= Hero->GetRosterHighlight()!=nullptr;
    Hero->ToggleRoster();
    Hero->CommandFollow(); Saved &= Leaf && Leaf->Activity == ECreatureActivity::Work;
    Hero->ClearCompanionSelection(); Hero->CommandFollow();
    const bool Reset = Leaf && Stone && Leaf->Activity == ECreatureActivity::Follow && Stone->Activity == ECreatureActivity::Follow && !Leaf->WorkSite;
    const bool Pass = RosterPass && Select && Split && ScopedRest && Combat && Reject && Lost && All && Saved && Reset;
    UE_LOG(LogTemp,Display,TEXT("CF_SQUAD %s roster=%d select=%d split=%d rest=%d combat=%d reject=%d lost=%d all=%d save=%d reset=%d"),
        Pass ? TEXT("PASS") : TEXT("FAIL"),RosterPass,Select,Split,ScopedRest,Combat,Reject,Lost,All,Saved,Reset);
    FPlatformMisc::RequestExitWithStatus(false,Pass ? 0 : 1);
}
