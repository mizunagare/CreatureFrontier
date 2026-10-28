#include "NameScenario.h"
#include "CFPlayerCharacter.h"
#include "CompanionNamingComponent.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "FrontierSaveGame.h"
#include "InventoryComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Containers/Ticker.h"
#include "UnrealClient.h"
#include "Components/BoxComponent.h"
#include "InputRoutingQA.h"

namespace CFNameQA
{
ACreatureCharacter* Named(UWorld* World,const FString& Name)
{for(TActorIterator<ACreatureCharacter> It(World);It;++It) if(It->Nickname==Name) return *It;return nullptr;}
bool Snapshot(UWorld* World,ACFPlayerCharacter* Hero)
{
    auto* A=Named(World,TEXT("小苔"));auto* B=Named(World,TEXT("Fern"));
    auto* Saved=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("NameCross"),0));
    if(!A || !B || A==B || !Saved || A->GetSpeciesName()!=B->GetSpeciesName() || Hero->GetCompanionRoster().Num()!=2 || A->BondPoints!=0 || A->Experience!=0) return false;
    for(const auto& R:Saved->Creatures)
    {auto* Live=Named(World,R.Nickname);if(!Live || Live->PersistentId!=R.Id || Live->GetCreatureName()!=R.Nickname || Live->Tamer!=Hero->GetController()) return false;}
    return A->PersistentId!=B->PersistentId;
}
}
void RunNameScenario(UWorld* World,ACFPlayerCharacter* Hero,int32 Mode)
{
    using namespace CFNameQA;bool Pass=true;auto* Naming=Hero->Naming.Get();
    if(Mode==2) Pass=Hero->LoadProgress(TEXT("NameCross")) && Snapshot(World,Hero) && Hero->LoadProgress(TEXT("NameCross")) && Snapshot(World,Hero);
    else
    {
        UCreatureDefinition* Leaf=nullptr;for(TActorIterator<ACreatureCharacter> It(World);It;++It){if(It->Definition && It->Definition->Form==ECreatureForm::Leaf) Leaf=It->Definition;It->Destroy();}
        if(!Leaf){UE_LOG(LogTemp,Error,TEXT("CF_NAME FAIL definition"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
        Hero->GetCharacterMovement()->DisableMovement();Hero->SetActorLocation(FVector(-450,-1000,40));
        auto Make=[&](FVector Position){const FTransform At(Position);auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),At,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            Pet->Definition=Leaf;Pet->FinishSpawning(At);Pet->Tame(Hero->GetController());Pet->SetActorTickEnabled(false);Pet->GetCharacterMovement()->DisableMovement();return Pet;};
        auto* A=Make(FVector(-450,-800,40));auto* B=Make(FVector(-100,-800,40));const auto Order=Hero->GetCompanionRoster();const auto Id=A->PersistentId;
        Pass=Naming->RenameCompanion(A,TEXT("  小苔  ")) && A->Nickname==TEXT("小苔") && A->GetCreatureName()==TEXT("小苔") &&
            A->PersistentId==Id && A->BondPoints==0 && A->Experience==0 && Hero->GetCompanionRoster()==Order;
        Pass &= CFInputQA::UniqueBindings(Hero);
        A->UpdateCreaturePresentation(.05f);Pass &= A->GetVisualState()==TEXT("Affection") && A->BondPoints==0 && A->PettingCooldown==0;
        for(const auto& Bad:TArray<FString>{TEXT("bad\nname"),FString::ChrN(25,TEXT('x')),TEXT("bad\tname"),TEXT("bad\u202Ename"),TEXT("bad\u200Bname")})
            Pass &= !Naming->RenameCompanion(A,Bad) && A->Nickname==TEXT("小苔");
        A->Activity=ECreatureActivity::Work;Pass &= !Naming->RenameCompanion(A,TEXT("busy"));A->Activity=ECreatureActivity::Follow;
        A->Health=0;Pass &= !Naming->RenameCompanion(A,TEXT("downed"));A->Health=A->GetMaxHealth();
        A->Tamer=nullptr;Pass &= !Naming->RenameCompanion(A,TEXT("other"));A->Tamer=Hero->GetController();
        UGameplayStatics::SetGamePaused(World,true);Pass &= !Naming->RenameCompanion(A,TEXT("paused"));UGameplayStatics::SetGamePaused(World,false);
        Hero->SetActorLocation(FVector(-450,-1600,40));Pass &= !Naming->RenameCompanion(A,TEXT("far"));Hero->SetActorLocation(FVector(-450,-1000,40));
        B->Disposition=ECreatureDisposition::Wild;B->Tamer=nullptr;Pass &= !Naming->RenameCompanion(A,TEXT("danger"));B->Disposition=ECreatureDisposition::Tamed;B->Tamer=Hero->GetController();
        auto* Wall=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Wall);Wall->SetRootComponent(Box);Box->SetBoxExtent(FVector(80,20,140));
        Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Box->SetCollisionResponseToAllChannels(ECR_Ignore);Box->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);Box->SetCanEverAffectNavigation(false);Box->RegisterComponent();Wall->SetActorLocation(FVector(-450,-900,60));
        Pass &= !Naming->RenameCompanion(A,TEXT("blocked")) && !Naming->Open(A);Wall->Destroy();
        Hero->FirstPersonCamera->SetWorldRotation((A->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());
        Pass &= CFInputQA::Press(Hero,EKeys::F4) && Naming->IsOpen() && UGameplayStatics::IsGamePaused(World);
        Pass &= !Naming->Commit(TEXT("bad\nname")) && Naming->IsOpen() && A->Nickname==TEXT("小苔");
        Naming->Close();Pass &= !UGameplayStatics::IsGamePaused(World) && !CastChecked<APlayerController>(Hero->GetController())->bShowMouseCursor;
        Pass &= Naming->Open(A) && Naming->Commit(TEXT(" ")) && A->Nickname.IsEmpty() && A->GetCreatureName()==A->GetSpeciesName() && !Naming->IsOpen();
        Pass &= Naming->Open(A) && Naming->Commit(TEXT("小苔")) && A->Nickname==TEXT("小苔");
        Hero->SetActorLocation(FVector(-100,-1000,40));Pass &= Naming->RenameCompanion(B,TEXT("小苔")) && A->Nickname==B->Nickname && A->PersistentId!=B->PersistentId;
        Pass &= Naming->RenameCompanion(B,TEXT("Fern"));
        Hero->SetActorLocation(FVector(-450,-1000,40));
        Pass &= Hero->SaveProgress(TEXT("NameCross")) && Snapshot(World,Hero);
        if(Mode==0) Pass &= Hero->LoadProgress(TEXT("NameCross")) && Snapshot(World,Hero);
    }
    if(Mode==0)
    {
        auto* A=Named(World,TEXT("小苔"));
        for(int32 Case=0;Case<6;++Case)
        {
            auto* Bad=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("NameCross"),0));auto& R=Bad->Creatures[0];
            if(Case==0) R.Nickname=TEXT("bad\nname");if(Case==1) R.Nickname=FString::ChrN(25,TEXT('x'));
            if(Case==2) R.Nickname=TEXT(" untrimmed");if(Case==3) R.bOwned=false;
            if(Case==4) R.Disposition=ECreatureDisposition::Wild;if(Case==5) R.Nickname=TEXT("bad\u202Ename");
            Pass &= UGameplayStatics::SaveGameToSlot(Bad,TEXT("NameBad"),0) && !Hero->LoadProgress(TEXT("NameBad")) && Named(World,TEXT("小苔"))==A && Snapshot(World,Hero);
        }
        A=Named(World,TEXT("小苔"));Hero->FirstPersonCamera->SetWorldRotation((A->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());
        Pass &= Naming->Open(A) && Hero->LoadProgress(TEXT("NameCross")) && !Naming->IsOpen() && !UGameplayStatics::IsGamePaused(World) && Snapshot(World,Hero);
        A=Named(World,TEXT("小苔"));Pass &= Naming->Open(A);A->Destroy();Pass &= !Naming->Commit(TEXT("ghost"));Naming->Close();Pass &= Hero->LoadProgress(TEXT("NameCross")) && Snapshot(World,Hero);
        A=Named(World,TEXT("小苔"));Pass &= Naming->Open(A);UGameplayStatics::ApplyDamage(Hero,1000,nullptr,nullptr,nullptr);
        Pass &= Hero->Health==0 && !Naming->IsOpen() && !UGameplayStatics::IsGamePaused(World) && Hero->LoadProgress(TEXT("NameCross")) && Snapshot(World,Hero);
        A=Named(World,TEXT("小苔"));A->Health=0;A->Activity=ECreatureActivity::Rest;
        Pass &= Hero->SaveProgress(TEXT("NameDowned")) && Hero->LoadProgress(TEXT("NameDowned")) && Named(World,TEXT("小苔")) && Named(World,TEXT("小苔"))->Health==0;
        auto* Legacy=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("NameCross"),0));Legacy->FormatVersion=24;
        Pass &= UGameplayStatics::SaveGameToSlot(Legacy,TEXT("NameLegacy24"),0) && Hero->LoadProgress(TEXT("NameLegacy24"));
        for(auto* Pet:Hero->GetCompanionRoster()) Pass &= Pet->Nickname.IsEmpty() && Pet->GetCreatureName()==Pet->GetSpeciesName();
        Pass &= Hero->LoadProgress(TEXT("NameCross")) && Snapshot(World,Hero);
    }
    UE_LOG(LogTemp,Display,TEXT("CF_NAME%s %s"),Mode==1?TEXT("_WRITE"):Mode==2?TEXT("_READ"):TEXT(""),Pass?TEXT("PASS"):TEXT("FAIL"));
    if(Pass && Mode==0 && FParse::Param(FCommandLine::Get(),TEXT("CFNamePreview")))
    {
        auto* Pet=Named(World,TEXT("小苔"));Hero->FirstPersonCamera->SetWorldRotation((Pet->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());
        if(!Naming->Open(Pet)){FPlatformMisc::RequestExitWithStatus(false,1);return;}
        const FString Root=FPaths::ProjectSavedDir()/TEXT("NamePreview");IFileManager::Get().MakeDirectory(*Root,true);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Root](float){FScreenshotRequest::RequestScreenshot(Root/TEXT("Dialog.png"),true,false);return false;}),1.f);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Naming,Hero](float){Naming->Close();Hero->ToggleRoster();return false;}),2.f);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Root](float){FScreenshotRequest::RequestScreenshot(Root/TEXT("Roster.png"),true,false);return false;}),3.f);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Root](float){const bool Ready=IFileManager::Get().FileSize(*(Root/TEXT("Dialog.png")))>1000 && IFileManager::Get().FileSize(*(Root/TEXT("Roster.png")))>1000;
            UE_LOG(LogTemp,Display,TEXT("CF_NAME_PREVIEW %s"),Ready?TEXT("PASS"):TEXT("FAIL"));FPlatformMisc::RequestExitWithStatus(false,Ready?0:1);return false;}),4.f);return;
    }
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}
