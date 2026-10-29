#include "CFPlayerCharacter.h"
#include "BaseBuildingComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "StorageInteractionComponent.h"
#include "PlayerGatheringComponent.h"

FString ACFPlayerCharacter::GetQuickSaveSlot() const
{
    // 菜单自动化测试绝不能写入玩家的常规存档槽。
    return FParse::Param(FCommandLine::Get(),TEXT("CFSessionTest"))?TEXT("FrontierSessionAutomation"):TEXT("FrontierSlot");
}

bool ACFPlayerCharacter::ClosePauseMenu()
{
    if(!UGameplayStatics::SetGamePaused(this,false)) return false;
    bPauseMenu=false; bPauseConfirmation=false; Feedback=TEXT("已继续 | F1 操作帮助 | Esc 暂停"); return true;
}

void ACFPlayerCharacter::TogglePauseMenu()
{
    if(!HasAuthority() || !GetController()) return;
    if(bPauseMenu)
    {
        if(bPauseConfirmation) { bPauseConfirmation=false; Feedback=TEXT("已取消确认，世界保持暂停"); return; }
        ClosePauseMenu(); return;
    }
    if(!UGameplayStatics::SetGamePaused(this,true)) { Feedback=TEXT("当前游戏模式无法暂停"); return; }
    Storage->Close();
    Gathering->Stop();
    CancelMeleeAttack(); Building->Cancel(); StopJumping(); GetCharacterMovement()->StopMovementImmediately(); ConsumeMovementInputVector();
    bRosterOpen=false; bJournalOpen=false; bHelpVisible=false; bBackpackOpen=false;
    bPauseMenu=true; bPauseConfirmation=false; PauseSelection=0;
    Feedback=TEXT("已暂停，世界模拟停止。退出前请保存");
}

void ACFPlayerCharacter::PauseMenuNext() { if(bPauseMenu) { PauseSelection=(PauseSelection+1)%5; bPauseConfirmation=false; } else if(Storage->IsOpen()) Storage->Step(1); else StepBackpackRecipe(1); }
void ACFPlayerCharacter::PauseMenuPrevious() { if(bPauseMenu) { PauseSelection=(PauseSelection+4)%5; bPauseConfirmation=false; } else if(Storage->IsOpen()) Storage->Step(-1); else StepBackpackRecipe(-1); }
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
    if(!bPauseMenu) { if(Storage->IsOpen()) Storage->DepositOne(); else if(bBackpackOpen) CraftBackpackRecipe(); else SelectRosterCompanion(); return; }
    switch(PauseSelection)
    {
        case 0: ClosePauseMenu(); break;
        case 1: QuickSave(); break;
        case 2:
            if(!bPauseConfirmation) { bPauseConfirmation=true; Feedback=TEXT("读档会替换当前世界。Enter 或 F9 确认，Esc 取消"); }
            else { bPauseConfirmation=false; LoadProgress(GetQuickSaveSlot()); }
            break;
        case 3: if(RespawnAtBase()) ClosePauseMenu(); break;
        case 4:
            if(!bPauseConfirmation) { bPauseConfirmation=true; Feedback=TEXT("不保存就退出？Enter 确认，Esc 取消，F5 保存"); }
            else QuitGame();
            break;
    }
}
