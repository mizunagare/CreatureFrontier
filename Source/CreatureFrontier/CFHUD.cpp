#include "CFHUD.h"
#include "CFPlayerCharacter.h"
#include "StorageInteractionComponent.h"
#include "Engine/Canvas.h"
#include "Kismet/GameplayStatics.h"

void ACFHUD::BeginHUDFrame(ACFPlayerCharacter* Player)
{
    Audit=FFrontierHUDAudit();Audit.Frame=++FrameSerial;Audit.Viewport=FVector2D(Canvas->ClipX,Canvas->ClipY);
    Audit.bInterface=Player->IsRosterOpen() || Player->IsJournalOpen() || Player->IsHelpVisible() || Player->IsBackpackOpen() || Player->Storage->IsOpen();
    Audit.bPaused=UGameplayStatics::IsGamePaused(this);Layout=FFrontierHUDLayout::Build(Canvas->ClipX,Canvas->ClipY,Audit.bInterface);
}
void ACFHUD::DrawHUD()
{
    Super::DrawHUD();auto* Player=Cast<ACFPlayerCharacter>(GetOwningPawn());if(!Canvas || !Player)return;
    BeginHUDFrame(Player);
    if(Player->IsPauseMenuOpen()){DrawPauseMenu(Player);return;}
    if(Audit.bPaused)return;
    DrawWorldHUD(Player);
    DrawWorldLabels(Player);
    if(Player->IsHelpVisible())DrawControlHelp(Player);
    if(Player->IsBackpackOpen())DrawBackpack(Player);
    if(Player->Storage->IsOpen())DrawStorage(Player);
    if(Player->IsJournalOpen())DrawJournalPanel(Player);
    if(Player->IsRosterOpen())DrawRosterPanel(Player);
}
