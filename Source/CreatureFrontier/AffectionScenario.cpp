#include "AffectionScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "CompanionNamingComponent.h"
#include "InventoryComponent.h"
#include "FrontierSaveGame.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameViewportClient.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "TimerManager.h"
#include <limits>

void RunAffectionScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    UCreatureDefinition* Definition=nullptr;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It) { if(It->Definition && It->Definition->Form==ECreatureForm::Leaf) Definition=It->Definition; It->Destroy(); }
    if(!Definition) { UE_LOG(LogTemp,Error,TEXT("CF_AFFECTION FAIL fixture")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Hero->SetActorLocation(FVector(-530,-1050,40)); Hero->GetCharacterMovement()->DisableMovement(); Hero->Inventory->Restore({{EFrontierItem::Food,3}});
    const FTransform Transform(FRotator(0,180,0),FVector(-350,-1050,40));
    auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),Transform); Pet->Definition=Definition; Pet->FinishSpawning(Transform); Pet->Tame(Hero->GetController()); Pet->SetActorTickEnabled(false); Pet->GetCharacterMovement()->DisableMovement();
    const float HP=Pet->Health,Stamina=Pet->Stamina; const int32 XP=Pet->Experience;
    bool Care=Hero->PetCompanion(Pet) && Pet->BondPoints==2 && Pet->PettingCooldown==30 && !Hero->PetCompanion(Pet);
    Pet->UpdateCreaturePresentation(.05f); Care &= Pet->GetVisualState()==TEXT("Affection") && Pet->Health==HP && Pet->Stamina==Stamina && Pet->Experience==XP && Hero->Inventory->Count(EFrontierItem::Food)==3;
    for(int32 Step=0;Step<30;++Step) Pet->UpdateCreaturePresentation(.1f); Care &= Pet->GetVisualState()==TEXT("Idle");
    Pet->Tick(10); Care &= FMath::IsNearlyEqual(Pet->PettingCooldown,20.f,.001f);
    Pet->PettingCooldown=0;
    Pet->SetActivity(ECreatureActivity::Work); bool Gates=!Hero->PetCompanion(Pet); Pet->SetActivity(ECreatureActivity::Combat); Gates &= !Hero->PetCompanion(Pet); Pet->SetActivity(ECreatureActivity::Follow);
    Pet->SetActorLocation(FVector(-100,-1050,40)); Gates &= !Hero->PetCompanion(Pet); Pet->SetActorLocation(Transform.GetLocation());
    auto* Wall=World->SpawnActor<AStaticMeshActor>(FVector(-440,-1050,70),FRotator::ZeroRotator); Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable); Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"))); Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll")); Wall->SetActorScale3D(FVector(.2,2,2));
    Gates &= !Hero->PetCompanion(Pet); Wall->Destroy();
    auto* Threat=World->SpawnActor<ACreatureCharacter>(Pet->GetActorLocation()+FVector(0,400,0),FRotator::ZeroRotator); Threat->SetActorTickEnabled(false); Gates &= !Hero->PetCompanion(Pet); Threat->Destroy();
    Pet->Tamer=nullptr; Gates &= !Hero->PetCompanion(Pet); Pet->Tamer=Hero->GetController(); Pet->Health=0; Gates &= !Hero->PetCompanion(Pet); Pet->Health=HP; Hero->Health=0; Gates &= !Hero->PetCompanion(Pet); Hero->Health=100;
    Hero->TogglePauseMenu(); Gates &= !Hero->PetCompanion(Pet); Hero->TogglePauseMenu(); Gates &= Pet->BondPoints==2 && Pet->PettingCooldown==0;
    Pet->Nutrition=50;
    auto RejectPausedCare=[Hero,Pet]()
    {
        const int32 Food=Hero->Inventory->Count(EFrontierItem::Food),Bond=Pet->BondPoints;
        const float Nutrition=Pet->Nutrition,Energy=Pet->Stamina,Cooldown=Pet->PettingCooldown;
        const bool PetRejected=!Hero->PetCompanion(Pet);
        const bool FeedRejected=!Hero->FeedCompanion(Pet);
        return PetRejected && FeedRejected && Hero->Inventory->Count(EFrontierItem::Food)==Food && Pet->BondPoints==Bond &&
            Pet->Nutrition==Nutrition && Pet->Stamina==Energy && Pet->PettingCooldown==Cooldown;
    };
    bool PausedCare=UGameplayStatics::SetGamePaused(World,true);
    PausedCare &= RejectPausedCare(); UGameplayStatics::SetGamePaused(World,false);
    const bool Opened=Hero->Naming->Open(Pet); PausedCare &= Opened && UGameplayStatics::IsGamePaused(World);
    if(Opened) { PausedCare &= RejectPausedCare(); Hero->Naming->Close(); }
    PausedCare &= !UGameplayStatics::IsGamePaused(World) && !Hero->Naming->IsOpen();
    bool Feed=Hero->FeedCompanion(Pet) && Pet->BondPoints==5 && Hero->Inventory->Count(EFrontierItem::Food)==2;
    Pet->BondPoints=99; Feed &= Hero->PetCompanion(Pet) && Pet->BondPoints==100; Pet->AddBond(3); Feed &= Pet->BondPoints==100 && Pet->GetBondLabel()==TEXT("亲密无间");
    auto FindPet=[World,Hero]() { for(TActorIterator<ACreatureCharacter> It(World);It;++It) if(It->Tamer==Hero->GetController()) return *It; return static_cast<ACreatureCharacter*>(nullptr); };
    bool Saved=Hero->SaveProgress(TEXT("FrontierAffectionAutomation")) && Hero->LoadProgress(TEXT("FrontierAffectionAutomation")); Pet=FindPet();
    Saved &= Pet && Pet->BondPoints==100 && Pet->PettingCooldown==30 && !Hero->PetCompanion(Pet);
    auto* Snapshot=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("FrontierAffectionAutomation"),0));
    bool Invalid=Snapshot && Snapshot->Creatures.Num()==1;
    if(Invalid) for(int32 Case=0;Case<5;++Case)
    {
        auto* Bad=DuplicateObject<UFrontierSaveGame>(Snapshot,GetTransientPackage()); auto& Record=Bad->Creatures[0];
        if(Case==0) Record.BondPoints=101;
        if(Case==1) Record.BondPoints=-1;
        if(Case==2) Record.PettingCooldown=31;
        if(Case==3) Record.PettingCooldown=std::numeric_limits<float>::quiet_NaN();
        if(Case==4) { Record.bOwned=false; Record.Disposition=ECreatureDisposition::Wild; }
        Invalid &= UGameplayStatics::SaveGameToSlot(Bad,TEXT("FrontierAffectionInvalid"),0) && !Hero->LoadProgress(TEXT("FrontierAffectionInvalid")) && IsValid(Pet) && Pet->BondPoints==100 && Pet->PettingCooldown==30;
    }
    bool Legacy=false;
    if(Snapshot) { Snapshot->FormatVersion=15; Legacy=UGameplayStatics::SaveGameToSlot(Snapshot,TEXT("FrontierAffectionLegacy"),0) && Hero->LoadProgress(TEXT("FrontierAffectionLegacy")); Pet=FindPet(); Legacy &= Pet && Pet->BondPoints==0 && Pet->PettingCooldown==0; }
    bool Response=Pet && Hero->PetCompanion(Pet);
    if(Pet) { Pet->UpdateCreaturePresentation(.1f); Response &= Pet->GetVisualState()==TEXT("Affection") && Pet->BondPoints==2; }
    const bool Passed=Care && Gates && PausedCare && Feed && Saved && Invalid && Legacy && Response;
    UE_LOG(LogTemp,Display,TEXT("CF_AFFECTION %s care=%d gates=%d paused=%d feed=%d save=%d invalid=%d legacy=%d response=%d"),Passed?TEXT("PASS"):TEXT("FAIL"),Care,Gates,PausedCare,Feed,Saved,Invalid,Legacy,Response);
    if(Passed && FParse::Param(FCommandLine::Get(),TEXT("CFAffectionPreview")))
    {
        Pet->SetActorTickEnabled(false); Hero->GetCharacterMovement()->DisableMovement();
        const FRotator View=(Pet->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation(); Hero->GetController()->SetControlRotation(View); Hero->FirstPersonCamera->SetWorldRotation(View);
        const FString Root=FPaths::ProjectSavedDir()/TEXT("AffectionPreview"); IFileManager::Get().MakeDirectory(*Root,true);
        FTimerHandle Shot,Done; World->GetTimerManager().SetTimer(Shot,[Root](){FScreenshotRequest::RequestScreenshot(Root/TEXT("petting.png"),false,false);},3.f,false);
        World->GetTimerManager().SetTimer(Done,[](){FPlatformMisc::RequestExitWithStatus(false,0);},5.f,false); return;
    }
    FPlatformMisc::RequestExitWithStatus(false,Passed?0:1);
}
