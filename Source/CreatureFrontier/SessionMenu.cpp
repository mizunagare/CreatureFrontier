#include "CFPlayerCharacter.h"
#include "BaseBuildingComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

FString ACFPlayerCharacter::GetQuickSaveSlot() const
{
    // The menu automation must never write the player's normal save slot.
    return FParse::Param(FCommandLine::Get(),TEXT("CFSessionTest"))?TEXT("FrontierSessionAutomation"):TEXT("FrontierSlot");
}

bool ACFPlayerCharacter::ClosePauseMenu()
{
    if(!UGameplayStatics::SetGamePaused(this,false)) return false;
    bPauseMenu=false; bPauseConfirmation=false; Feedback=TEXT("Resumed | F1 controls | Esc pause"); return true;
}

void ACFPlayerCharacter::TogglePauseMenu()
{
    if(!HasAuthority() || !GetController()) return;
    if(bPauseMenu)
    {
        if(bPauseConfirmation) { bPauseConfirmation=false; Feedback=TEXT("Confirmation cancelled - world remains paused"); return; }
        ClosePauseMenu(); return;
    }
    if(!UGameplayStatics::SetGamePaused(this,true)) { Feedback=TEXT("Pause is unavailable in this game mode"); return; }
    CancelMeleeAttack(); Building->Cancel(); StopJumping(); GetCharacterMovement()->StopMovementImmediately(); ConsumeMovementInputVector();
    bRosterOpen=false; bJournalOpen=false; bHelpVisible=false; bBackpackOpen=false;
    bPauseMenu=true; bPauseConfirmation=false; PauseSelection=0;
    Feedback=TEXT("Paused - world simulation stopped. Save before quitting");
}

void ACFPlayerCharacter::PauseMenuNext() { if(bPauseMenu) { PauseSelection=(PauseSelection+1)%5; bPauseConfirmation=false; } else StepBackpackRecipe(1); }
void ACFPlayerCharacter::PauseMenuPrevious() { if(bPauseMenu) { PauseSelection=(PauseSelection+4)%5; bPauseConfirmation=false; } else StepBackpackRecipe(-1); }
void ACFPlayerCharacter::ToggleHelp()
{
    if(bPauseMenu) return;
    bHelpVisible=!bHelpVisible;
    if(bHelpVisible) { bRosterOpen=false; bJournalOpen=false; bBackpackOpen=false; }
}

void ACFPlayerCharacter::QuickSave()
{
    if(bPauseMenu) { PauseSelection=1; bPauseConfirmation=false; }
    SaveProgress(GetQuickSaveSlot());
}
void ACFPlayerCharacter::QuickLoad()
{
    if(!bPauseMenu) TogglePauseMenu();
    if(!bPauseMenu) return;
    if(PauseSelection!=2) { PauseSelection=2; bPauseConfirmation=false; }
    ConfirmInterfaceAction();
}

void ACFPlayerCharacter::ConfirmInterfaceAction()
{
    if(!bPauseMenu) { if(bBackpackOpen) CraftBackpackRecipe(); else SelectRosterCompanion(); return; }
    switch(PauseSelection)
    {
        case 0: ClosePauseMenu(); break;
        case 1: QuickSave(); break;
        case 2:
            if(!bPauseConfirmation) { bPauseConfirmation=true; Feedback=TEXT("Load replaces the current world. Enter or F9 confirms; Esc cancels"); }
            else { bPauseConfirmation=false; LoadProgress(GetQuickSaveSlot()); }
            break;
        case 3: if(RespawnAtBase()) ClosePauseMenu(); break;
        case 4:
            if(!bPauseConfirmation) { bPauseConfirmation=true; Feedback=TEXT("Quit without saving? Enter confirms; Esc cancels. F5 saves instead"); }
            else QuitGame();
            break;
    }
}
