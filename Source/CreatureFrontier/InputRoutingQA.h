#pragma once
#include "CFPlayerCharacter.h"
#include "Components/InputComponent.h"
namespace CFInputQA
{
inline bool UniqueBindings(ACFPlayerCharacter* Hero)
{
    auto* Input=Hero->FindComponentByClass<UInputComponent>();if(!Input) return false;TSet<FKey> Seen;
    for(const auto& B:Input->KeyBindings) if(B.KeyEvent==IE_Pressed && !B.Chord.bShift && !B.Chord.bCtrl && !B.Chord.bAlt && !B.Chord.bCmd)
    {if(Seen.Contains(B.Chord.Key)) return false;Seen.Add(B.Chord.Key);}
    return Seen.Num()>0;
}
inline bool Press(ACFPlayerCharacter* Hero,FKey Key)
{
    auto* Input=Hero->FindComponentByClass<UInputComponent>();if(!Input) return false;const FInputKeyBinding* Found=nullptr;
    for(const auto& B:Input->KeyBindings) if(B.KeyEvent==IE_Pressed && B.Chord.Key==Key && !B.Chord.bShift && !B.Chord.bCtrl && !B.Chord.bAlt && !B.Chord.bCmd)
    {if(Found) return false;Found=&B;}
    if(!Found || !Found->KeyDelegate.IsBound()) return false;Found->KeyDelegate.Execute(Key);return true;
}
inline bool Release(ACFPlayerCharacter* Hero,FKey Key)
{
    auto* Input=Hero->FindComponentByClass<UInputComponent>();if(!Input)return false;const FInputKeyBinding* Found=nullptr;
    for(const auto& B:Input->KeyBindings)if(B.KeyEvent==IE_Released && B.Chord.Key==Key && !B.Chord.bShift && !B.Chord.bCtrl && !B.Chord.bAlt && !B.Chord.bCmd){if(Found)return false;Found=&B;}
    if(!Found || !Found->KeyDelegate.IsBound())return false;Found->KeyDelegate.Execute(Key);return true;
}
}
