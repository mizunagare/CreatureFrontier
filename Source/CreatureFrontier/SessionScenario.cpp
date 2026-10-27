#include "SessionScenario.h"
#include "CFPlayerCharacter.h"
#include "InventoryComponent.h"
#include "Containers/Ticker.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMisc.h"
#include "TimerManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Engine/GameViewportClient.h"

void RunSessionScenario(UWorld* World, ACFPlayerCharacter* Hero)
{
    auto Passed=MakeShared<bool>(true);
    auto Fired=MakeShared<bool>(false);
    Hero->ToggleHelp(); *Passed &= Hero->IsHelpVisible();
    Hero->TogglePauseMenu();
    *Passed &= Hero->IsPauseMenuOpen() && !Hero->IsHelpVisible() && UGameplayStatics::IsGamePaused(World);
    UE_LOG(LogTemp,Display,TEXT("CF_SESSION stage open=%d"),*Passed);
    Hero->PauseMenuPrevious(); Hero->ConfirmInterfaceAction();
    *Passed &= Hero->GetPauseSelection()==4 && Hero->IsPauseConfirmationPending();
    Hero->TogglePauseMenu();
    *Passed &= Hero->IsPauseMenuOpen() && !Hero->IsPauseConfirmationPending();
    UE_LOG(LogTemp,Display,TEXT("CF_SESSION stage cancel=%d"),*Passed);
    Hero->QuickSave();
    *Passed &= UGameplayStatics::DoesSaveGameExist(TEXT("FrontierSessionAutomation"),0);
    const int32 SavedMaterials=Hero->Inventory->Count(EFrontierItem::Material);
    Hero->Inventory->Restore({{EFrontierItem::Material,77}});
    Hero->QuickLoad();
    *Passed &= Hero->IsPauseConfirmationPending() && Hero->Inventory->Count(EFrontierItem::Material)==77;
    Hero->TogglePauseMenu();
    *Passed &= Hero->IsPauseMenuOpen() && Hero->Inventory->Count(EFrontierItem::Material)==77;
    Hero->QuickLoad(); Hero->ConfirmInterfaceAction();
    *Passed &= Hero->Inventory->Count(EFrontierItem::Material)==SavedMaterials && Hero->IsPauseMenuOpen() && UGameplayStatics::IsGamePaused(World);
    UE_LOG(LogTemp,Display,TEXT("CF_SESSION stage load=%d materials=%d expected=%d feedback=%s"),*Passed,Hero->Inventory->Count(EFrontierItem::Material),SavedMaterials,*Hero->Feedback);
    FTimerHandle Timer; World->GetTimerManager().SetTimer(Timer,[Fired](){*Fired=true;},.15f,false);
    const double Start=FPlatformTime::Seconds(); const double FrozenTime=World->GetTimeSeconds();
    auto Phase=MakeShared<int32>(0); auto ResumedTime=MakeShared<double>(0);
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([World,Hero,Passed,Fired,Start,FrozenTime,Phase,ResumedTime](float)
    {
        const double Elapsed=FPlatformTime::Seconds()-Start;
        if(*Phase==0 && Elapsed>.5)
        {
            *Passed &= !*Fired && FMath::Abs(World->GetTimeSeconds()-FrozenTime)<.0001;
            UE_LOG(LogTemp,Display,TEXT("CF_SESSION freeze=%d fired=%d time=%.4f expected=%.4f"),*Passed,*Fired,World->GetTimeSeconds(),FrozenTime);
            Hero->TogglePauseMenu();
            *Passed &= !Hero->IsPauseMenuOpen() && !UGameplayStatics::IsGamePaused(World);
            *ResumedTime=World->GetTimeSeconds(); *Phase=1;
        }
        else if(*Phase==1 && Elapsed>1.2)
        {
            *Passed &= *Fired && World->GetTimeSeconds()>*ResumedTime+.1f;
            UE_LOG(LogTemp,Display,TEXT("CF_SESSION resume=%d fired=%d time=%.4f expected=%.4f"),*Passed,*Fired,World->GetTimeSeconds(),*ResumedTime);
            Hero->QuickLoad(); *Passed &= Hero->IsPauseMenuOpen() && Hero->IsPauseConfirmationPending();
            Hero->TogglePauseMenu(); Hero->TogglePauseMenu();
            *Passed &= !Hero->IsPauseMenuOpen();
            UE_LOG(LogTemp,Display,TEXT("CF_SESSION %s pause_timer resume save load_confirmation quit_cancel help"),*Passed?TEXT("PASS"):TEXT("FAIL"));
            if(*Passed && FParse::Param(FCommandLine::Get(),TEXT("CFSessionPreview")))
            {
                IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("SessionPreview")),true);
                Hero->TogglePauseMenu(); *Phase=2; return true;
            }
            FPlatformMisc::RequestExitWithStatus(false,*Passed?0:1); return false;
        }
        else if(*Phase==2 && Elapsed>3.5) { FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("SessionPreview/paused.png"),false,false); *Phase=3; }
        else if(*Phase==3 && Elapsed>5) { Hero->TogglePauseMenu(); Hero->ToggleHelp(); *Phase=4; }
        else if(*Phase==4 && Elapsed>7) { FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("SessionPreview/controls.png"),false,false); *Phase=5; }
        else if(*Phase==5 && Elapsed>8.5) { Hero->ToggleHelp(); *Phase=6; }
        else if(*Phase==6 && Elapsed>10.5) { FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("SessionPreview/compact.png"),false,false); *Phase=7; }
        else if(*Phase==7 && Elapsed>12) { FPlatformMisc::RequestExitWithStatus(false,0); return false; }
        return true;
    }));
}
