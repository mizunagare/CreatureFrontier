#include "CFHUD.h"
#include "CFPlayerCharacter.h"
#include "PlayerEquipmentComponent.h"
#include "Engine/Canvas.h"

void ACFHUD::DrawWrappedText(const FString& Text,FLinearColor Color,float X,float Y,float Width,int32 MaxLines)
{
    TArray<FString> Words; Text.ParseIntoArrayWS(Words); FString Line; int32 Row=0;
    for(const auto& Word:Words)
    {
        const FString Next=Line.IsEmpty()?Word:Line+TEXT(" ")+Word;
        float W,H; GetTextSize(Next,W,H);
        if(W>Width && !Line.IsEmpty())
        {
            DrawText(Line,Color,X,Y+Row*20); ++Row; if(Row>=MaxLines) return; Line=Word;
        }
        else Line=Next;
    }
    if(!Line.IsEmpty() && Row<MaxLines) DrawText(Line,Color,X,Y+Row*20);
}

void ACFHUD::DrawPauseMenu(ACFPlayerCharacter* Player)
{
    const float Width=FMath::Min(640.f,Canvas->ClipX-48),Left=(Canvas->ClipX-Width)/2,Top=(Canvas->ClipY-460)/2;
    DrawRect(FLinearColor(.005f,.012f,.02f,.88f),0,0,Canvas->ClipX,Canvas->ClipY);
    DrawRect(FLinearColor(.025f,.055f,.08f,.98f),Left,Top,Width,460);
    DrawRect(FLinearColor(.08f,.26f,.32f,1),Left+14,Top+112+Player->GetPauseSelection()*40,Width-28,34);
    DrawText(TEXT("PAUSED - CREATURE FRONTIER"),FLinearColor::White,Left+24,Top+24,nullptr,1.2f);
    DrawText(TEXT("World simulation stopped"),FLinearColor::Green,Left+24,Top+54);
    DrawText(TEXT("F5 save | F9 load confirmation"),FLinearColor::White,Left+24,Top+80);
    const TCHAR* Options[]={TEXT("Resume"),TEXT("Save current progress"),TEXT("Load saved progress"),TEXT("Respawn at base (downed only)"),TEXT("Quit to desktop (no auto-save)")};
    for(int32 Index=0;Index<5;++Index)
        DrawText(FString(Index==Player->GetPauseSelection()?TEXT(">> "):TEXT("   "))+Options[Index],Index==Player->GetPauseSelection()?FLinearColor::Yellow:FLinearColor::White,Left+24,Top+120+Index*40);
    DrawWrappedText(Player->Feedback,Player->IsPauseConfirmationPending()?FLinearColor::Yellow:FLinearColor::Green,Left+24,Top+332,Width-48,3);
    DrawText(TEXT("Up / Down choose | Enter confirm | Esc resume / cancel"),FLinearColor::White,Left+24,Top+414);
    DrawText(TEXT("Quitting does not save. F5 saves before you leave."),FLinearColor::White,Left+24,Top+436);
}

void ACFHUD::DrawControlHelp(ACFPlayerCharacter* Player)
{
    const TArray<FString> Lines={
        TEXT("WASD move | mouse look | Space jump"),
        TEXT("M gather resource (3m) | shared finite stock"),
        TEXT("LMB melee | RMB capture / cancel spear windup"),
        TEXT("Q focus enemy | G companion special"),
        TEXT("L select one companion | K command all"),
        TEXT("F follow | E nearest work facility (8m)"),
        TEXT("T rest | P treat aimed companion"),
        TEXT("J feed aimed companion | H heal yourself"),
        TEXT("Y pet companion (2.5m, safe, follow / rest)"),
        TEXT("B build | V cycle facility | X dismantle"),
        TEXT("Build: 8 facility / home | 9 rotate home wall"),
        TEXT("C backpack / recipes | 1 craft device (3)"),
        TEXT("2 craft medicine (5) | 3 craft food (2)"),
        FString::Printf(TEXT("4 spear (%d) | 5 vest (%d)"),Player->Equipment->SpearCost,Player->Equipment->VestCost),
        TEXT("6 / 7 equip or remove spear / vest"),
        TEXT("U store food | O take from garden / trough"),
        TEXT("Z recover aimed companion cargo"),
        TEXT("Tab team | [ / ] browse | Enter select"),
        TEXT("I journal | PageUp / PageDown browse"),
        TEXT("N claim completed journey reward"),
        TEXT("R downed base respawn - world retained"),
        TEXT("F5 save | F9 confirm load | Esc pause"),
        TEXT("F1 closes help - this panel does NOT pause")};
    const float Left=Canvas->ClipX-488,Step=FMath::Clamp((Canvas->ClipY-130)/Lines.Num(),15.f,24.f);
    DrawRect(FLinearColor(.015f,.025f,.04f,.96f),Left,18,470,86+Step*Lines.Num());
    DrawText(TEXT("CONTROLS | F1 close"),FLinearColor::White,Left+14,30);
    DrawText(TEXT("LIVE WORLD - open Esc menu to pause"),FLinearColor::Yellow,Left+14,56);
    for(int32 Index=0;Index<Lines.Num();++Index) DrawText(Lines[Index],FLinearColor::White,Left+14,90+Index*Step);
}
