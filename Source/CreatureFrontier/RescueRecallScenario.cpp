#include "RescueRecallScenario.h"
#include "CompanionMemoryComponent.h"
#include "CreatureCharacter.h"
#include "CFPlayerCharacter.h"
#include "ResearchComponent.h"
#include "InventoryComponent.h"
#include "InputRoutingQA.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Containers/Ticker.h"
#include "UnrealClient.h"
#include "FrontierSaveGame.h"

void RunRescueRecallLifecycle(UWorld* World,ACFPlayerCharacter* Hero)
{
    ACreatureCharacter* Pet=nullptr;
    auto* PC=Cast<APlayerController>(Hero->GetController());
    auto Find=[World]()->ACreatureCharacter*{for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->PersistentId==FGuid(0xCF290000,0,0,1))return *It;return nullptr;};
    auto Reset=[&]()
    {
        if(!PC || !Hero->LoadProgress(TEXT("RescueMemoryHome")))return false;
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)
        {It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();if(It->Disposition!=ECreatureDisposition::Tamed)It->SetActorLocation(FVector(3000,-3000,40));}
        Pet=Find();if(!Pet)return false;
        Pet->Memory->RestoreSnapshot(ECrystalJourneyMemory::None);
        Hero->SetActorLocation(Pet->GetActorLocation()+FVector(0,200,0));
        return Pet->Memory->GetRescueMemory()==ERescueJourneyMemory::Pending;
    };
    if(!Reset()){UE_LOG(LogTemp,Error,TEXT("CF_RESCUE_LIFECYCLE FAIL fixture"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    const bool Opened=Pet->Memory->Open(Hero);
    PC->UnPossess();
    const bool Released=Opened && !Pet->Memory->IsOpen() && !UGameplayStatics::IsGamePaused(World) && !PC->bShowMouseCursor && Pet->Memory->GetRescueMemory()==ERescueJourneyMemory::Pending;
    UE_LOG(LogTemp,Display,TEXT("CF_RESCUE_LIFECYCLE_UNPOSSESS %s open=%d paused=%d cursor=%d"),Released?TEXT("PASS"):TEXT("FAIL"),Pet->Memory->IsOpen(),UGameplayStatics::IsGamePaused(World),PC->bShowMouseCursor);
    // 恢复专项夹具和输入，但不清除刚才的失败结果。
    PC->Possess(Hero);Pet->Memory->Close();UGameplayStatics::SetGamePaused(World,false);PC->bShowMouseCursor=false;PC->SetInputMode(FInputModeGameOnly());
    bool Swap=Reset() && Pet->Memory->Open(Hero);
    auto* Other=World->SpawnActor<ACFPlayerCharacter>(FVector(-1000,-1000,40),FRotator::ZeroRotator);
    if(Other){PC->Possess(Other);Swap &= !Pet->Memory->IsOpen() && !UGameplayStatics::IsGamePaused(World) && !PC->bShowMouseCursor && !Pet->Memory->Open(Hero);PC->Possess(Hero);Other->Destroy();}else Swap=false;
    bool Same=Reset() && Pet->Memory->Open(Hero);
    // 无变化通知不应关窗；真正再次Possess在本引擎中会先UnPossess，必须释放窗口。
    PC->OnPossessedPawnChanged.Broadcast(Hero,Hero);Same &= Pet->Memory->IsOpen() && UGameplayStatics::IsGamePaused(World);
    PC->Possess(Hero);const bool Restart=!Pet->Memory->IsOpen() && !UGameplayStatics::IsGamePaused(World) && !PC->bShowMouseCursor && Pet->Memory->GetRescueMemory()==ERescueJourneyMemory::Pending;
    Pet->Memory->Close();
    bool Bad=Reset() && Pet->Memory->Open(Hero);
    auto* Snapshot=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("RescueMemoryHome"),0));
    auto* Record=Snapshot?Snapshot->Creatures.FindByPredicate([](const auto& R){return R.Id==FGuid(0xCF290000,0,0,1);}):nullptr;
    if(Record){Record->RescueJourney=255;Bad &= UGameplayStatics::SaveGameToSlot(Snapshot,TEXT("RescueLifecycleBad"),0) && !Hero->LoadProgress(TEXT("RescueLifecycleBad")) && Find()==Pet && Pet->Memory->IsOpen() && UGameplayStatics::IsGamePaused(World) && PC->bShowMouseCursor && Hero->Feedback.Contains(TEXT("脱险经历"));}else Bad=false;
    Pet->Memory->Close();
    bool Loaded=Reset() && Pet->Memory->Open(Hero);auto* Previous=Pet;
    Loaded &= Hero->LoadProgress(TEXT("RescueMemoryHome")) && Find()!=Previous && !UGameplayStatics::IsGamePaused(World) && !PC->bShowMouseCursor;
    bool Target=Reset() && Pet->Memory->Open(Hero);auto* Memory=Pet->Memory.Get();Pet->Destroy();Target &= !Memory->IsOpen() && !UGameplayStatics::IsGamePaused(World) && !PC->bShowMouseCursor;
    bool Owner=Reset() && Pet->Memory->Open(Hero);Memory=Pet->Memory.Get();Hero->Destroy();
    Owner &= !Memory->IsOpen() && !UGameplayStatics::IsGamePaused(World) && IsValid(PC) && !PC->bShowMouseCursor && Pet->Memory->GetRescueMemory()==ERescueJourneyMemory::Pending;
    UE_LOG(LogTemp,Display,TEXT("CF_RESCUE_LIFECYCLE_OWNER %s open=%d paused=%d"),Owner?TEXT("PASS"):TEXT("FAIL"),Memory->IsOpen(),UGameplayStatics::IsGamePaused(World));
    const bool Pass=Released && Swap && Same && Restart && Bad && Loaded && Target && Owner;
    UE_LOG(LogTemp,Display,TEXT("CF_RESCUE_LIFECYCLE %s unpossess=%d swap=%d sameNotice=%d restart=%d bad=%d load=%d target=%d owner=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Released,Swap,Same,Restart,Bad,Loaded,Target,Owner);FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}

namespace CFRescueRecallQA
{
ACreatureCharacter* Find(UWorld* World,int32 Index)
{for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->PersistentId==FGuid(0xCF290000,0,0,Index))return *It;return nullptr;}
void Aim(ACFPlayerCharacter* Hero,ACreatureCharacter* Pet)
{
    Hero->SetActorLocation(Pet->GetActorLocation()+FVector(0,200,0));
    const auto View=(Pet->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
    Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
}
bool Key(FKey Code)
{const FKeyEvent Event(Code,FModifierKeysState(),0,false,0,0);const bool Result=FSlateApplication::Get().ProcessKeyDownEvent(Event);FSlateApplication::Get().ProcessKeyUpEvent(Event);return Result;}
}
void RunRescueRecallScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    using namespace CFRescueRecallQA;
    bool Pass=Hero->LoadProgress(Read?TEXT("RescueRecallDone"):TEXT("RescueMemoryHome"));
    auto* A=Find(World,1);auto* B=Find(World,2);
    if(!Pass || !A){UE_LOG(LogTemp,Error,TEXT("CF_RESCUE_RECALL FAIL input"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    if(Read)
    {
        Pass &= B && A->Memory->GetMemory()==ECrystalJourneyMemory::QuietThanks && A->Memory->GetRescueMemory()==ERescueJourneyMemory::QuietThanks &&
            B->Memory->GetMemory()==ECrystalJourneyMemory::Pending && B->Memory->GetRescueMemory()==ERescueJourneyMemory::Celebration &&
            A->Memory->GetRescueSource()==TEXT("EmberHollow") && B->Memory->GetRescueSource()==TEXT("EmberHollow") && A->Health==40 && B->Health==40 &&
            A->BondPoints==0 && B->BondPoints==0 && A->Experience==0 && B->Experience==0 && Hero->Inventory->Count(EFrontierItem::Medicine)==0;
        UE_LOG(LogTemp,Display,TEXT("CF_RESCUE_RECALL_READ %s"),Pass?TEXT("PASS"):TEXT("FAIL"));FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return;
    }
    for(TActorIterator<ACreatureCharacter> It(World);It;++It){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();if(It->Disposition!=ECreatureDisposition::Tamed)It->SetActorLocation(FVector(3000,-3000,40));}
    // 两经历并存和事件锁定为隔离夹具；自然经历来源另由救助链验收。
    FResearchProgress Progress;Progress.bCrystalSample=true;Hero->Research->Restore(Progress);
    A->Memory->RestoreSnapshot(ECrystalJourneyMemory::Pending);A->SetActorLocation(FVector(-450,-500,40));A->SetActivity(ECreatureActivity::Rest);
    B=World->SpawnActor<ACreatureCharacter>(FVector(-100,-500,40),FRotator::ZeroRotator);B->PersistentId=FGuid(0xCF290000,0,0,2);B->Tamer=Hero->GetController();B->Disposition=ECreatureDisposition::Tamed;B->Health=40;B->SetActivity(ECreatureActivity::Rest);B->SetActorTickEnabled(false);B->GetCharacterMovement()->DisableMovement();
    B->Memory->RestoreRescueSnapshot(ERescueJourneyMemory::Pending,TEXT("EmberHollow"));
    TArray<int32> Counts;for(int32 I=0;I<static_cast<int32>(EFrontierItem::Count);++I)Counts.Add(Hero->Inventory->Count(static_cast<EFrontierItem>(I)));
    const float Nutrition=A->Nutrition,Stamina=A->Stamina,Cooldown=A->PettingCooldown;
    Aim(Hero,A);Pass &= CFInputQA::UniqueBindings(Hero) && CFInputQA::Press(Hero,EKeys::F7) && A->Memory->IsOpen() && UGameplayStatics::IsGamePaused(World);
    Pass &= !Hero->PetCompanion(A) && Key(EKeys::Escape) && !UGameplayStatics::IsGamePaused(World) && A->Memory->GetMemory()==ECrystalJourneyMemory::Pending && A->Memory->GetRescueMemory()==ERescueJourneyMemory::Pending;
    Pass &= CFInputQA::Press(Hero,EKeys::F7) && Key(EKeys::One) && A->Memory->GetMemory()==ECrystalJourneyMemory::QuietThanks && A->Memory->GetRescueMemory()==ERescueJourneyMemory::Pending;
    Pass &= CFInputQA::Press(Hero,EKeys::F7) && A->Memory->IsOpen();A->Health=0;
    Pass &= !A->Memory->Commit(false) && A->Memory->GetRescueMemory()==ERescueJourneyMemory::Pending;A->Memory->Close();A->Health=40;
    Pass &= CFInputQA::Press(Hero,EKeys::F7) && Key(EKeys::Escape) && A->Memory->GetRescueMemory()==ERescueJourneyMemory::Pending;
    Pass &= CFInputQA::Press(Hero,EKeys::F7) && Key(EKeys::One) && A->Memory->GetRescueMemory()==ERescueJourneyMemory::QuietThanks && A->Memory->GetMemory()==ECrystalJourneyMemory::QuietThanks && !A->Memory->Open(Hero);
    A->UpdateCreaturePresentation(.1f);Pass &= A->GetVisualState()==TEXT("Affection");
    Aim(Hero,B);Pass &= CFInputQA::Press(Hero,EKeys::F7) && B->Memory->IsOpen();
    B->Memory->RestoreSnapshot(ECrystalJourneyMemory::Pending);
    Pass &= Key(EKeys::Two) && B->Memory->GetRescueMemory()==ERescueJourneyMemory::Celebration && B->Memory->GetMemory()==ECrystalJourneyMemory::Pending;
    B->UpdateCreaturePresentation(.1f);Pass &= B->GetVisualState()==TEXT("Playing") && !UGameplayStatics::IsGamePaused(World);
    Pass &= A->Health==40 && B->Health==40 && A->Nutrition==Nutrition && A->Stamina==Stamina && A->PettingCooldown==Cooldown && A->BondPoints==0 && A->Experience==0 && B->BondPoints==0 && B->Experience==0;
    for(int32 I=0;I<Counts.Num();++I)Pass &= Counts[I]==Hero->Inventory->Count(static_cast<EFrontierItem>(I));
    Pass &= Hero->SaveProgress(TEXT("RescueRecallDone"));
    UE_LOG(LogTemp,Display,TEXT("CF_RESCUE_RECALL %s"),Pass?TEXT("PASS"):TEXT("FAIL"));
    if(Pass && FParse::Param(FCommandLine::Get(),TEXT("CFRescueRecallPreview")))
    {
        // 仅为窗口/布局取景夹具；不冒充自然行走或连续动作证据。
        B->Memory->RestoreSnapshot(ECrystalJourneyMemory::None);B->Memory->RestoreRescueSnapshot(ERescueJourneyMemory::Pending,TEXT("EmberHollow"));Aim(Hero,B);
        if(!B->Memory->Open(Hero)){FPlatformMisc::RequestExitWithStatus(false,1);return;}
        const FString Root=FPaths::ProjectSavedDir()/TEXT("RescueRecallPreview");IFileManager::Get().MakeDirectory(*Root,true);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Root](float){FScreenshotRequest::RequestScreenshot(Root/TEXT("Dialog.png"),true,false);return false;}),1.f);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Hero,B](float){B->Memory->Commit(true);Hero->ToggleRoster();return false;}),2.f);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Root](float){FScreenshotRequest::RequestScreenshot(Root/TEXT("Roster.png"),true,false);return false;}),3.f);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Hero,A](float){Hero->ToggleRoster();Aim(Hero,A);return false;}),4.f);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Root](float){FScreenshotRequest::RequestScreenshot(Root/TEXT("Context.png"),true,false);return false;}),5.f);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float){FPlatformMisc::RequestExitWithStatus(false,0);return false;}),6.f);return;
    }
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}
