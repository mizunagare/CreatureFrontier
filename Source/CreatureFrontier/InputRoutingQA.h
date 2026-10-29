#pragma once
#include "CFPlayerCharacter.h"
#include "Components/InputComponent.h"
#include "GameFramework/InputSettings.h"
namespace CFInputQA
{
// 核对实际动作映射和唯一委托后执行；这是引擎绑定验证，不是操作系统鼠标输入。
inline bool Action(ACFPlayerCharacter* Hero,FName Name,FKey Key)
{
    TArray<FInputActionKeyMapping> Mappings;GetDefault<UInputSettings>()->GetActionMappingByName(Name,Mappings);
    bool Mapped=false;for(const auto& Mapping:Mappings)
        Mapped |= Mapping.Key==Key && !Mapping.bShift && !Mapping.bCtrl && !Mapping.bAlt && !Mapping.bCmd;
    auto* Input=Hero->FindComponentByClass<UInputComponent>();if(!Mapped || !Input)return false;
    const FInputActionBinding* Found=nullptr;
    for(int32 Index=0;Index<Input->GetNumActionBindings();++Index)
    {const auto& Binding=Input->GetActionBinding(Index);if(Binding.GetActionName()==Name && Binding.KeyEvent==IE_Pressed){if(Found)return false;Found=&Binding;}}
    if(!Found || !Found->ActionDelegate.IsBound())return false;Found->ActionDelegate.Execute(Key);return true;
}
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
