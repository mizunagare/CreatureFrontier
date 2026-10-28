#include "PersonalityScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "CompanionPersonalityComponent.h"
#include "CompanionNamingComponent.h"
#include "FrontierSaveGame.h"
#include "InventoryComponent.h"
#include "InputRoutingQA.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Containers/Ticker.h"
#include "UnrealClient.h"

namespace CFPersonalityQA
{
const FGuid QuietId(0xCF260000,0,0,2),PlayId(0xCF260000,0,0,3);
ACreatureCharacter* Find(UWorld* World,const FGuid& Id)
{for(TActorIterator<ACreatureCharacter> It(World);It;++It) if(It->PersistentId==Id) return *It;return nullptr;}
void Aim(ACFPlayerCharacter* Hero,ACreatureCharacter* Pet)
{
    Hero->SetActorLocation(Pet->GetActorLocation()+FVector(-180,0,0));
    const auto View=(Pet->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
    Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
}
bool Snapshot(UWorld* World,ACFPlayerCharacter* Hero)
{
    auto* A=Find(World,QuietId);auto* B=Find(World,PlayId);
    return A && B && A->Personality->GetPreference()==ECompanionPreference::Quiet && B->Personality->GetPreference()==ECompanionPreference::Playful &&
        A->Personality->IsDiscovered() && B->Personality->IsDiscovered() && A->Nickname==TEXT("QuietLeaf") && B->Nickname==TEXT("PlayLeaf") &&
        A->Definition==B->Definition && A->Tamer==Hero->GetController() && B->Tamer==Hero->GetController() && A->BondPoints==4 && B->BondPoints==4 &&
        A->PettingCooldown==30 && B->PettingCooldown==30 && A->Stamina==95 && B->Stamina==95 && A->Nutrition==100 && B->Nutrition==100 &&
        A->Experience==0 && B->Experience==0 && Hero->Inventory->Count(EFrontierItem::Food)==3;
}
}
void RunPersonalityScenario(UWorld* World,ACFPlayerCharacter* Hero,int32 Mode)
{
    using namespace CFPersonalityQA;bool Gates=true,Interaction=true,Persistence=true;
    if(Mode==3)
    {
        auto* Old=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("NameCross"),0));
        bool Pass=Old && Old->FormatVersion<=28 && Old->Creatures.Num()==2;
        for(int32 Attempt=0;Attempt<2;++Attempt)
        {
            Pass &= Hero->LoadProgress(TEXT("NameCross"));
            if(Old) for(const auto& R:Old->Creatures)
            {auto* Pet=Find(World,R.Id);const auto Preference=Old->FormatVersion>=26?R.Preference:UCompanionPersonalityComponent::PreferenceForId(R.Id);
             Pass &= Pet && Pet->Nickname==R.Nickname && Pet->BondPoints==R.BondPoints && Pet->Personality->GetPreference()==Preference && Pet->Personality->IsDiscovered()==(Old->FormatVersion>=26 && R.bPreferenceDiscovered);}
        }
        UE_LOG(LogTemp,Display,TEXT("CF_PERSONALITY_LEGACY_READ %s"),Pass?TEXT("PASS"):TEXT("FAIL"));FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return;
    }
    if(Mode==2)
        Persistence=Hero->LoadProgress(TEXT("PersonalityCross")) && Snapshot(World,Hero) && Hero->LoadProgress(TEXT("PersonalityCross")) && Snapshot(World,Hero);
    else
    {
        UCreatureDefinition* Leaf=nullptr;
        for(TActorIterator<ACreatureCharacter> It(World);It;++It){if(It->Definition && It->Definition->Form==ECreatureForm::Leaf) Leaf=It->Definition;It->Destroy();}
        if(!Leaf){UE_LOG(LogTemp,Error,TEXT("CF_PERSONALITY FAIL fixture"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
        Hero->GetCharacterMovement()->DisableMovement();Hero->Inventory->Restore({{EFrontierItem::Food,3}});
        auto Make=[World,Hero,Leaf](FGuid Id,FVector Position,const TCHAR* Name)
        {
            const FTransform At(FRotator(0,180,0),Position);auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),At,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            Pet->PersistentId=Id;Pet->Definition=Leaf;Pet->FinishSpawning(At);Pet->Tame(Hero->GetController());Pet->Nickname=Name;
            Pet->SetActorTickEnabled(false);Pet->GetCharacterMovement()->DisableMovement();return Pet;
        };
        auto* A=Make(QuietId,FVector(-350,-1050,40),TEXT("QuietLeaf"));auto* B=Make(PlayId,FVector(-350,-700,40),TEXT("PlayLeaf"));Aim(Hero,B);
        Gates=!A->Personality->IsDiscovered() && !B->Personality->IsDiscovered() && A->Personality->GetSummary().Contains(TEXT("尚未发现"));
        auto Reject=[Hero,B]()
        {
            const float Energy=B->Stamina,Food=B->Nutrition;const bool Known=B->Personality->IsDiscovered();const int32 Bond=B->BondPoints;
            return !Hero->PlayWithCompanion(B) && B->Stamina==Energy && B->Nutrition==Food && B->BondPoints==Bond && B->Personality->IsDiscovered()==Known && B->PettingCooldown==0;
        };
        B->Activity=ECreatureActivity::Work;Gates &= Reject();B->Activity=ECreatureActivity::Combat;Gates &= Reject();B->Activity=ECreatureActivity::Rest;Gates &= Reject();B->Activity=ECreatureActivity::Follow;
        B->bResumeWorkAfterRest=true;Gates &= Reject();B->bResumeWorkAfterRest=false;
        B->Stamina=19;Gates &= Reject();B->Stamina=100;B->Nutrition=19;Gates &= Reject();B->Nutrition=100;
        B->FocusTarget=A;Gates &= Reject();B->FocusTarget=nullptr;B->SpecialWindupRemaining=1;Gates &= Reject();B->SpecialWindupRemaining=0;
        B->Health=0;Gates &= Reject();B->Health=B->GetMaxHealth();B->Tamer=nullptr;Gates &= Reject();B->Tamer=Hero->GetController();
        Hero->Health=0;Gates &= Reject();Hero->Health=100;
        UGameplayStatics::SetGamePaused(World,true);Gates &= Reject();UGameplayStatics::SetGamePaused(World,false);
        Gates &= Hero->Naming->Open(B);Gates &= Reject();Hero->Naming->Close();
        Hero->SetActorLocation(B->GetActorLocation()+FVector(-400,0,0));Gates &= Reject();Aim(Hero,B);
        auto* Wall=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Wall);Wall->SetRootComponent(Box);Box->SetBoxExtent(FVector(20,80,140));
        Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Box->SetCollisionResponseToAllChannels(ECR_Ignore);Box->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);Box->RegisterComponent();Wall->SetActorLocation(B->GetActorLocation()+FVector(-90,0,40));
        Gates &= Reject();Wall->Destroy();
        auto* Threat=World->SpawnActor<ACreatureCharacter>(B->GetActorLocation()+FVector(0,400,0),FRotator::ZeroRotator);Threat->SetActorTickEnabled(false);Gates &= Reject();Threat->Destroy();
        auto* Boundary=Make(FGuid(0xCF260000,0,0,4),FVector(-350,-1400,40),TEXT("Boundary"));Aim(Hero,Boundary);Boundary->Stamina=20;Boundary->Nutrition=20;
        Gates &= Hero->PlayWithCompanion(Boundary) && Boundary->Stamina==15 && Boundary->Nutrition==20 && Boundary->BondPoints==2 && !Boundary->Personality->IsDiscovered();
        Boundary->PettingCooldown=0;Gates &= !Hero->PlayWithCompanion(Boundary) && Boundary->Stamina==15 && Boundary->BondPoints==2;
        Boundary->Activity=ECreatureActivity::Rest;Boundary->bResumeWorkAfterRest=true;
        Gates &= Hero->PetCompanion(Boundary) && Boundary->bResumeWorkAfterRest && Boundary->Activity==ECreatureActivity::Rest && Boundary->Stamina==15;
        Boundary->Destroy();
        const float HP=A->Health,Attack=A->GetAttackDamage();
        Aim(Hero,A);Interaction=CFInputQA::UniqueBindings(Hero) && CFInputQA::Press(Hero,EKeys::Y) && A->BondPoints==2 && A->Personality->IsDiscovered();
        A->UpdateCreaturePresentation(.05f);Interaction &= A->GetVisualState()==TEXT("Affection");
        Aim(Hero,B);Interaction &= CFInputQA::Press(Hero,EKeys::Y) && B->BondPoints==2 && !B->Personality->IsDiscovered();
        Interaction &= !Hero->PlayWithCompanion(B) && B->Stamina==100 && B->PettingCooldown==30;
        B->PettingCooldown=0;Interaction &= CFInputQA::Press(Hero,EKeys::F6) && B->BondPoints==4 && B->Personality->IsDiscovered() && B->Stamina==95;
        const auto Position=B->GetActorTransform();float MaxBounce=0;for(int32 Step=0;Step<15;++Step){B->UpdateCreaturePresentation(.05f);MaxBounce=FMath::Max(MaxBounce,B->CreatureVisual->GetAttachParent()->GetRelativeLocation().Z);}
        Interaction &= B->GetVisualState()==TEXT("Playing") && MaxBounce>12 && B->GetActorTransform().Equals(Position) && !Hero->PetCompanion(B) && B->BondPoints==4 && B->Stamina==95;
        Aim(Hero,A);A->PettingCooldown=0;Interaction &= Hero->PlayWithCompanion(A) && A->Stamina==95 && A->BondPoints==4 && A->Health==HP && A->GetAttackDamage()==Attack && A->Experience==0;
        for(int32 Step=0;Step<50;++Step)B->UpdateCreaturePresentation(.1f);Interaction &= B->GetVisualState()==TEXT("Idle");
        Persistence=Hero->SaveProgress(TEXT("PersonalityCross")) && Snapshot(World,Hero);
        if(Mode==0)
        {
            Persistence &= Hero->LoadProgress(TEXT("PersonalityCross")) && Snapshot(World,Hero);A=Find(World,QuietId);
            for(int32 Case=0;Case<4;++Case)
            {
                auto* Bad=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("PersonalityCross"),0));auto& R=Bad->Creatures[0];
                if(Case==0)R.Preference=static_cast<ECompanionPreference>(255);if(Case==1)R.Preference=ECompanionPreference::None;
                if(Case==2){R.bOwned=false;R.Disposition=ECreatureDisposition::Wild;R.Nickname.Empty();R.BondPoints=0;R.PettingCooldown=0;}
                if(Case==3){R.bOwned=false;R.Disposition=ECreatureDisposition::Wild;R.Nickname.Empty();R.BondPoints=0;R.PettingCooldown=0;R.Preference=ECompanionPreference::None;R.bPreferenceDiscovered=true;}
                Persistence &= UGameplayStatics::SaveGameToSlot(Bad,TEXT("PersonalityBad"),0) && !Hero->LoadProgress(TEXT("PersonalityBad")) && Find(World,QuietId)==A && Snapshot(World,Hero);
            }
            A->Health=0;Persistence &= Hero->SaveProgress(TEXT("PersonalityDowned")) && Hero->LoadProgress(TEXT("PersonalityDowned"));A=Find(World,QuietId);
            Persistence &= A && A->Health==0 && A->Personality->IsDiscovered() && A->Personality->GetPreference()==ECompanionPreference::Quiet;
            auto* Legacy=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("PersonalityCross"),0));Legacy->FormatVersion=25;
            for(auto& R:Legacy->Creatures){R.Preference=static_cast<ECompanionPreference>(255);R.bPreferenceDiscovered=true;}
            Persistence &= UGameplayStatics::SaveGameToSlot(Legacy,TEXT("PersonalityLegacy25"),0) && Hero->LoadProgress(TEXT("PersonalityLegacy25"));
            for(auto* Pet:Hero->GetCompanionRoster())Persistence &= !Pet->Personality->IsDiscovered() && Pet->Personality->GetPreference()==UCompanionPersonalityComponent::PreferenceForId(Pet->PersistentId);
            Persistence &= Hero->LoadProgress(TEXT("PersonalityCross")) && Snapshot(World,Hero);
        }
    }
    const bool Pass=Gates && Interaction && Persistence;
    UE_LOG(LogTemp,Display,TEXT("CF_PERSONALITY%s %s gates=%d interaction=%d persistence=%d"),Mode==1?TEXT("_WRITE"):Mode==2?TEXT("_READ"):TEXT(""),Pass?TEXT("PASS"):TEXT("FAIL"),Gates,Interaction,Persistence);
    if(Pass && Mode==0 && FParse::Param(FCommandLine::Get(),TEXT("CFPersonalityPreview")))
    {
        auto* A=Find(World,QuietId);auto* B=Find(World,PlayId);A->SetActorTickEnabled(false);B->SetActorTickEnabled(false);Hero->GetCharacterMovement()->DisableMovement();Aim(Hero,A);
        A->PettingCooldown=0;Hero->PetCompanion(A);A->UpdateCreaturePresentation(.1f);
        const FString Root=FPaths::ProjectSavedDir()/TEXT("PersonalityPreview");IFileManager::Get().MakeDirectory(*Root,true);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Root](float){FScreenshotRequest::RequestScreenshot(Root/TEXT("Quiet.png"),true,false);return false;}),1.f);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Hero,B](float){Aim(Hero,B);B->PettingCooldown=0;Hero->PlayWithCompanion(B);B->UpdateCreaturePresentation(.1f);return false;}),2.f);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Root](float){FScreenshotRequest::RequestScreenshot(Root/TEXT("Play.png"),true,false);return false;}),3.f);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Hero](float){Hero->ToggleRoster();return false;}),4.f);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Root](float){FScreenshotRequest::RequestScreenshot(Root/TEXT("Roster.png"),true,false);return false;}),5.f);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Root](float){const bool Ready=IFileManager::Get().FileSize(*(Root/TEXT("Play.png")))>1000 && IFileManager::Get().FileSize(*(Root/TEXT("Roster.png")))>1000;
            const bool All=Ready && IFileManager::Get().FileSize(*(Root/TEXT("Quiet.png")))>1000;
            UE_LOG(LogTemp,Display,TEXT("CF_PERSONALITY_PREVIEW %s"),All?TEXT("PASS"):TEXT("FAIL"));FPlatformMisc::RequestExitWithStatus(false,All?0:1);return false;}),6.f);return;
    }
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}
