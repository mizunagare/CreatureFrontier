#include "MemoryScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "ResourceNode.h"
#include "ResourceDefinition.h"
#include "CompanionMemoryComponent.h"
#include "CompanionPersonalityComponent.h"
#include "CompanionNamingComponent.h"
#include "FrontierSaveGame.h"
#include "InventoryComponent.h"
#include "InputRoutingQA.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Containers/Ticker.h"
#include "UnrealClient.h"
#include "Framework/Application/SlateApplication.h"

namespace CFMemoryQA
{
FGuid Id(int32 Index){return FGuid(0xCF270000,0,0,Index);}
ACreatureCharacter* Pet(UWorld* World,int32 Index)
{for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->PersistentId==Id(Index))return *It;return nullptr;}
AResourceNode* Crystal(UWorld* World)
{for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,7))return *It;return nullptr;}
void Aim(ACFPlayerCharacter* Hero,ACreatureCharacter* Target)
{
    Hero->SetActorLocation(Target->GetActorLocation()+FVector(0,-200,0));const auto View=(Target->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
    Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
}
void Home(UWorld* World,ACFPlayerCharacter* Hero)
{
    auto* A=Pet(World,1);auto* D=Pet(World,4);A->SetActorLocation(FVector(-450,-500,40));D->SetActorLocation(FVector(-100,-500,40));
    A->SetActorTickEnabled(false);D->SetActorTickEnabled(false);A->GetCharacterMovement()->DisableMovement();D->GetCharacterMovement()->DisableMovement();Aim(Hero,A);
}
bool SlateKey(FKey Key)
{
    const FKeyEvent Event(Key,FModifierKeysState(),0,false,0,0);
    const bool Handled=FSlateApplication::Get().ProcessKeyDownEvent(Event);FSlateApplication::Get().ProcessKeyUpEvent(Event);return Handled;
}
bool Snapshot(UWorld* World,ACFPlayerCharacter* Hero,bool Done)
{
    auto* A=Pet(World,1);auto* D=Pet(World,4);auto* Node=Crystal(World);
    if(!A || !D || !Node || !Node->Definition || Node->Remaining!=Node->Definition->Capacity-5 || Hero->Inventory->Count(EFrontierItem::Crystal)!=4 || Hero->Inventory->Count(EFrontierItem::Material)!=1) return false;
    if(Done && (!D->bResumeWorkAfterRest || D->Activity!=ECreatureActivity::Rest || D->GetFieldResource()!=Node))return false;
    for(int32 Index=1;Index<=4;++Index)
    {
        auto* Live=Pet(World,Index);if(!Live || Live->Tamer!=Hero->GetController() || Live->BondPoints!=0 || Live->Experience!=0 || Live->Stamina!=100 || Live->Nutrition!=100 || Live->PettingCooldown!=0 || Live->Personality->IsDiscovered())return false;
        const auto Expected=Index==1?(Done?ECrystalJourneyMemory::QuietThanks:ECrystalJourneyMemory::Pending):Index==4?(Done?ECrystalJourneyMemory::Celebration:ECrystalJourneyMemory::Pending):ECrystalJourneyMemory::None;
        if(Live->Memory->GetMemory()!=Expected)return false;
    }
    return true;
}
bool Finish(UWorld* World,ACFPlayerCharacter* Hero)
{
    Home(World,Hero);auto* A=Pet(World,1);auto* D=Pet(World,4);
    bool Pass=CFInputQA::UniqueBindings(Hero) && CFInputQA::Press(Hero,EKeys::F7) && A->Memory->IsOpen() && UGameplayStatics::IsGamePaused(World);
    Pass &= !Hero->PetCompanion(A) && !Hero->FeedCompanion(A);Pass &= SlateKey(EKeys::Escape);Pass &= !UGameplayStatics::IsGamePaused(World) && A->Memory->GetMemory()==ECrystalJourneyMemory::Pending;
    Pass &= A->Memory->Open(Hero) && SlateKey(EKeys::One) && !A->Memory->IsOpen() && !UGameplayStatics::IsGamePaused(World) && !A->Memory->Open(Hero);
    A->UpdateCreaturePresentation(.1f);Pass &= A->GetVisualState()==TEXT("Affection");
    D->bResumeWorkAfterRest=true;D->SetActivity(ECreatureActivity::Rest);Aim(Hero,D);
    Pass &= D->Memory->Open(Hero) && SlateKey(EKeys::Two) && !D->Memory->IsOpen() && D->Activity==ECreatureActivity::Rest && D->bResumeWorkAfterRest && D->GetFieldResource()==Crystal(World);
    D->UpdateCreaturePresentation(.1f);Pass &= D->GetVisualState()==TEXT("Playing");
    const bool Stable=Snapshot(World,Hero,true);
    UE_LOG(LogTemp,Display,TEXT("CF_MEMORY_RECALL pass=%d stable=%d states=%d,%d resume=%d field=%d"),Pass,Stable,int32(A->Memory->GetMemory()),int32(D->Memory->GetMemory()),D->bResumeWorkAfterRest,D->GetFieldResource()==Crystal(World));
    return Pass && Stable && Hero->SaveProgress(TEXT("MemoryDoneCross"));
}
}
void RunMemoryScenario(UWorld* World,ACFPlayerCharacter* Hero,int32 Mode)
{
    using namespace CFMemoryQA;bool Sampling=true,Gates=true,Persistence=true;
    if(Mode==4)
    {
        auto* Old=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("PersonalityCross"),0));bool Pass=Old && Old->FormatVersion>=26 && Old->FormatVersion<=FrontierCurrentSaveVersion && Old->Creatures.Num()==2;
        for(int32 Attempt=0;Attempt<2;++Attempt)
        {
            Pass &= Hero->LoadProgress(TEXT("PersonalityCross"));
            if(Old)for(const auto& R:Old->Creatures)
            {ACreatureCharacter* Live=nullptr;for(auto* P:Hero->GetCompanionRoster())if(P->PersistentId==R.Id)Live=P;
             Pass &= Live && Live->Nickname==R.Nickname && Live->Personality->GetPreference()==R.Preference && Live->Personality->IsDiscovered()==R.bPreferenceDiscovered && Live->Memory->GetMemory()==(Old->FormatVersion>=27?static_cast<ECrystalJourneyMemory>(R.CrystalJourney):ECrystalJourneyMemory::None) && Live->BondPoints==R.BondPoints;}
        }
        UE_LOG(LogTemp,Display,TEXT("CF_MEMORY_LEGACY_READ %s"),Pass?TEXT("PASS"):TEXT("FAIL"));FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return;
    }
    if(Mode==3)Persistence=Hero->LoadProgress(TEXT("MemoryDoneCross")) && Snapshot(World,Hero,true) && Hero->LoadProgress(TEXT("MemoryDoneCross")) && Snapshot(World,Hero,true);
    else if(Mode==2)Persistence=Hero->LoadProgress(TEXT("MemoryPendingCross")) && Snapshot(World,Hero,false) && Finish(World,Hero);
    else
    {
        UCreatureDefinition* Leaf=nullptr;for(TActorIterator<ACreatureCharacter> It(World);It;++It){if(It->Definition && It->Definition->Form==ECreatureForm::Leaf)Leaf=It->Definition;It->Destroy();}
        auto* Node=CFMemoryQA::Crystal(World);if(!Leaf || !Node){UE_LOG(LogTemp,Error,TEXT("CF_MEMORY FAIL fixture"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
        Hero->GetCharacterMovement()->DisableMovement();Hero->SetActorLocation(FVector(1200,1600,40));
        auto Make=[World,Hero,Leaf](int32 Index,FVector Position,const TCHAR* Name)
        {const FTransform At(FRotator(0,-90,0),Position);auto* P=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),At,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
         P->PersistentId=Id(Index);P->Nickname=Name;P->Definition=Leaf;P->FinishSpawning(At);P->Tame(Hero->GetController());P->SetActorTickEnabled(false);P->GetCharacterMovement()->DisableMovement();return P;};
        auto* A=Make(1,FVector(1400,1400,40),TEXT("Tansy"));auto* B=Make(2,FVector(2200,1600,40),TEXT("Scout"));auto* C=Make(3,FVector(1600,1600,40),TEXT("Dozer"));auto* D=Make(4,FVector(1400,1800,40),TEXT("Spark"));
        C->SetActivity(ECreatureActivity::Rest);Sampling=D->AssignFieldResource(Node);
        Hero->Inventory->Restore({{EFrontierItem::Crystal,999}});const int32 Budget=Node->Remaining;
        Sampling &= !Node->TryHarvest(Hero,Hero) && Node->Remaining==Budget && A->Memory->GetMemory()==ECrystalJourneyMemory::None;
        Hero->Inventory->Restore({});auto* Original=Node->Definition.Get();Node->Definition=LoadObject<UResourceDefinition>(nullptr,TEXT("/Game/Resources/DA_Brush.DA_Brush"));
        Sampling &= Node->TryHarvest(Hero,Hero) && A->Memory->GetMemory()==ECrystalJourneyMemory::None;Node->Definition=Original;Node->HarvestCooldown=0;
        auto* Wall=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Wall);Wall->SetRootComponent(Box);Box->SetBoxExtent(FVector(60,15,140));Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Box->SetCollisionResponseToAllChannels(ECR_Ignore);Box->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);Box->RegisterComponent();Wall->SetActorLocation(FVector(1400,1500,40));
        Sampling &= Node->TryHarvest(Hero,Hero) && A->Memory->GetMemory()==ECrystalJourneyMemory::None && D->Memory->GetMemory()==ECrystalJourneyMemory::None;Wall->Destroy();Node->HarvestCooldown=0;
        Hero->SetActorLocation(FVector(0,-1200,40));Sampling &= Node->TryHarvest(Hero,D) && D->Memory->GetMemory()==ECrystalJourneyMemory::None;
        Node->HarvestCooldown=0;Hero->SetActorLocation(FVector(1200,1600,40));Sampling &= Node->TryHarvest(Hero,D) && A->Memory->GetMemory()==ECrystalJourneyMemory::Pending && D->Memory->GetMemory()==ECrystalJourneyMemory::Pending && B->Memory->GetMemory()==ECrystalJourneyMemory::None && C->Memory->GetMemory()==ECrystalJourneyMemory::None;
        Sampling &= !Node->TryHarvest(Hero,Hero);Node->HarvestCooldown=0;Sampling &= Node->TryHarvest(Hero,Hero) && Snapshot(World,Hero,false);
        UE_LOG(LogTemp,Display,TEXT("CF_MEMORY_SAMPLE pass=%d remaining=%d/%d crystal=%d material=%d states=%d,%d,%d,%d"),Sampling,Node->Remaining,Node->Definition->Capacity,Hero->Inventory->Count(EFrontierItem::Crystal),Hero->Inventory->Count(EFrontierItem::Material),int32(A->Memory->GetMemory()),int32(B->Memory->GetMemory()),int32(C->Memory->GetMemory()),int32(D->Memory->GetMemory()));
        Gates=!A->Memory->Open(Hero);Home(World,Hero);
        A->Activity=ECreatureActivity::Work;Gates &= !A->Memory->Open(Hero);A->Activity=ECreatureActivity::Follow;
        A->Tamer=nullptr;Gates &= !A->Memory->Open(Hero);A->Tamer=Hero->GetController();A->Health=0;Gates &= !A->Memory->Open(Hero);A->Health=A->GetMaxHealth();
        UGameplayStatics::SetGamePaused(World,true);Gates &= !A->Memory->Open(Hero);UGameplayStatics::SetGamePaused(World,false);
        Gates &= Hero->Naming->Open(A) && !A->Memory->Open(Hero);Hero->Naming->Close();
        Hero->SetActorLocation(FVector(-450,-900,40));Gates &= !A->Memory->Open(Hero);Aim(Hero,A);
        Hero->Health=0;Gates &= !A->Memory->Open(Hero);Hero->Health=100;
        A->FocusTarget=B;Gates &= !A->Memory->Open(Hero);A->FocusTarget=nullptr;A->SpecialWindupRemaining=1;Gates &= !A->Memory->Open(Hero);A->SpecialWindupRemaining=0;
        auto* RecallWall=World->SpawnActor<AActor>();auto* RecallBox=NewObject<UBoxComponent>(RecallWall);RecallWall->SetRootComponent(RecallBox);RecallBox->SetBoxExtent(FVector(80,20,140));RecallBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);RecallBox->SetCollisionResponseToAllChannels(ECR_Ignore);RecallBox->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);RecallBox->RegisterComponent();RecallWall->SetActorLocation(FVector(-450,-600,70));
        Gates &= !A->Memory->Open(Hero);RecallWall->Destroy();
        auto* RecallThreat=World->SpawnActor<ACreatureCharacter>(A->GetActorLocation()+FVector(0,400,0),FRotator::ZeroRotator);RecallThreat->SetActorTickEnabled(false);Gates &= !A->Memory->Open(Hero);RecallThreat->Destroy();
        Gates &= A->Memory->Open(Hero);A->Health=0;Gates &= !A->Memory->Commit(false) && A->Memory->GetMemory()==ECrystalJourneyMemory::Pending;A->Memory->Close();A->Health=A->GetMaxHealth();
        Persistence=Hero->SaveProgress(TEXT("MemoryPendingCross")) && Snapshot(World,Hero,false);
        if(Mode==0)
        {
            Gates &= A->Memory->Open(Hero);
            for(int32 Case=0;Case<3;++Case)
            {
                auto* Bad=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("MemoryPendingCross"),0));
                if(Case==0)Bad->Creatures[0].CrystalJourney=255;
                if(Case==1)Bad->Research.bCrystalSample=false;
                if(Case==2){auto& R=*Bad->Creatures.FindByPredicate([](const FCreatureSaveRecord& R){return R.Id==Id(1);});R.bOwned=false;R.Disposition=ECreatureDisposition::Wild;R.Nickname.Empty();R.Preference=ECompanionPreference::None;R.bPreferenceDiscovered=false;}
                const bool Rejected=UGameplayStatics::SaveGameToSlot(Bad,TEXT("MemoryBad"),0) && !Hero->LoadProgress(TEXT("MemoryBad")) && Pet(World,1)==A && A->Memory->IsOpen() && UGameplayStatics::IsGamePaused(World);
                UE_LOG(LogTemp,Display,TEXT("CF_MEMORY_BAD case=%d rejected=%d feedback=%s"),Case,Rejected,*Hero->Feedback);Persistence &= Rejected;
            }
            Persistence &= Hero->LoadProgress(TEXT("MemoryPendingCross")) && !UGameplayStatics::IsGamePaused(World) && Snapshot(World,Hero,false);A=Pet(World,1);Home(World,Hero);
            Gates &= A->Memory->Open(Hero);A->Destroy();Gates &= !UGameplayStatics::IsGamePaused(World);Persistence &= Hero->LoadProgress(TEXT("MemoryPendingCross"));A=Pet(World,1);Home(World,Hero);
            Gates &= A->Memory->Open(Hero);UGameplayStatics::ApplyDamage(Hero,1000,nullptr,nullptr,nullptr);Gates &= Hero->Health==0 && !UGameplayStatics::IsGamePaused(World);Persistence &= Hero->LoadProgress(TEXT("MemoryPendingCross"));A=Pet(World,1);
            A->Health=0;Persistence &= Hero->SaveProgress(TEXT("MemoryDowned")) && Hero->LoadProgress(TEXT("MemoryDowned"));A=Pet(World,1);Persistence &= A && A->Health==0 && A->Memory->GetMemory()==ECrystalJourneyMemory::Pending;
            auto* Legacy=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("MemoryPendingCross"),0));Legacy->FormatVersion=26;for(auto& R:Legacy->Creatures)R.CrystalJourney=255;
            Persistence &= UGameplayStatics::SaveGameToSlot(Legacy,TEXT("MemoryLegacy26"),0) && Hero->LoadProgress(TEXT("MemoryLegacy26"));for(auto* P:Hero->GetCompanionRoster())Persistence &= P->Memory->GetMemory()==ECrystalJourneyMemory::None;
            Persistence &= Hero->LoadProgress(TEXT("MemoryPendingCross")) && Snapshot(World,Hero,false) && Finish(World,Hero);
        }
    }
    const bool Pass=Sampling && Gates && Persistence;
    const TCHAR* Suffix=Mode==1?TEXT("_WRITE"):Mode==2?TEXT("_READ"):Mode==3?TEXT("_DONE_READ"):TEXT("");
    UE_LOG(LogTemp,Display,TEXT("CF_MEMORY%s %s sampling=%d gates=%d persistence=%d"),Suffix,Pass?TEXT("PASS"):TEXT("FAIL"),Sampling,Gates,Persistence);
    if(Pass && Mode==0 && FParse::Param(FCommandLine::Get(),TEXT("CFMemoryPreview")))
    {
        Hero->LoadProgress(TEXT("MemoryPendingCross"));for(auto* P:Hero->GetCompanionRoster()){P->SetActorTickEnabled(false);P->GetCharacterMovement()->DisableMovement();}Home(World,Hero);Hero->GetCharacterMovement()->DisableMovement();auto* A=Pet(World,1);auto* D=Pet(World,4);
        if(!A->Memory->Open(Hero)){FPlatformMisc::RequestExitWithStatus(false,1);return;}
        const FString Root=FPaths::ProjectSavedDir()/TEXT("MemoryPreview");IFileManager::Get().MakeDirectory(*Root,true);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Root](float){FScreenshotRequest::RequestScreenshot(Root/TEXT("Dialog.png"),true,false);return false;}),1.f);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Hero,A,D](float){A->Memory->Commit(false);D->bResumeWorkAfterRest=true;D->SetActivity(ECreatureActivity::Rest);Aim(Hero,D);D->Memory->Open(Hero);D->Memory->Commit(true);Hero->ToggleRoster();return false;}),2.f);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Root](float){FScreenshotRequest::RequestScreenshot(Root/TEXT("Roster.png"),true,false);return false;}),3.f);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Root](float){const bool Ready=IFileManager::Get().FileSize(*(Root/TEXT("Dialog.png")))>1000 && IFileManager::Get().FileSize(*(Root/TEXT("Roster.png")))>1000;
            UE_LOG(LogTemp,Display,TEXT("CF_MEMORY_PREVIEW %s"),Ready?TEXT("PASS"):TEXT("FAIL"));FPlatformMisc::RequestExitWithStatus(false,Ready?0:1);return false;}),4.f);return;
    }
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}
