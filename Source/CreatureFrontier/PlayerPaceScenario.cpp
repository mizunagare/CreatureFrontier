#include "HollowScenario.h"
#include "CFPlayerCharacter.h"
#include "PlayerPaceComponent.h"
#include "CreatureCharacter.h"
#include "InputRoutingQA.h"
#include "InputKeyEventArgs.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformMisc.h"

namespace
{
struct FPaceTest
{
    UWorld* World;ACFPlayerCharacter* Hero;APlayerController* PC;int32 Phase=0;
    double Next=0,Deadline=0;FVector Start;float Fast=0,Slow=0,Diagonal=0,StartGame=0,FastSeconds=0,SlowSeconds=0,DiagonalSeconds=0;
    bool Gates=true,Physical=true,Chord=false,Paused=false,Loaded=false,Focus=false,Death=false;
    void Key(FKey Key,EInputEvent Event)
    {PC->InputKey(FInputKeyEventArgs(nullptr,IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(),Key,Event,FPlatformTime::Cycles64()));}
    void Place()
    {Hero->GetCharacterMovement()->StopMovementImmediately();Hero->ConsumeMovementInputVector();Hero->SetActorLocation(FVector(-250,-400,40));PC->SetControlRotation(FRotator::ZeroRotator);Start=Hero->GetActorLocation();StartGame=World->GetTimeSeconds();}
    void Advance(int32 To,double Delay=.2){Phase=To;Next=FPlatformTime::Seconds()+Delay;}
    bool Tick()
    {
        const double Now=FPlatformTime::Seconds();if(Now>Deadline){UE_LOG(LogTemp,Error,TEXT("CF_PACE FAIL deadline phase=%d"),Phase);FPlatformMisc::RequestExitWithStatus(false,1);return false;}if(Now<Next)return true;
        auto* Pace=Hero->Pace.Get();auto* Move=Hero->GetCharacterMovement();
        if(Phase==0){Fast=FVector::Dist2D(Start,Hero->GetActorLocation());FastSeconds=World->GetTimeSeconds()-StartGame;Key(EKeys::W,IE_Released);Place();Key(EKeys::LeftShift,IE_Pressed);Key(EKeys::W,IE_Pressed);Hero->Feedback=TEXT("PACE_Q_PENDING");Key(EKeys::Q,IE_Pressed);Advance(1,.8);return true;}
        if(Phase==1)
        {Slow=FVector::Dist2D(Start,Hero->GetActorLocation());SlowSeconds=World->GetTimeSeconds()-StartGame;Physical &= Pace->IsSlowWalking() && FMath::IsNearlyEqual(Move->MaxWalkSpeed,192.f) && Hero->GetVelocity().Size2D()<=193;Chord=Hero->Feedback==TEXT("瞄准存活的野生生物以指挥集火");Key(EKeys::Q,IE_Released);Key(EKeys::W,IE_Released);Place();Key(EKeys::W,IE_Pressed);Key(EKeys::D,IE_Pressed);Advance(2,.8);return true;}
        if(Phase==2)
        {Diagonal=FVector::Dist2D(Start,Hero->GetActorLocation());DiagonalSeconds=World->GetTimeSeconds()-StartGame;const bool Timed=FastSeconds>0 && SlowSeconds>0 && DiagonalSeconds>0;Physical &= Timed && Hero->GetVelocity().Size2D()<=193 && Slow/SlowSeconds>100 && Slow/SlowSeconds<=193 && Diagonal/DiagonalSeconds<=193 && Fast/FastSeconds>(Slow/SlowSeconds)*1.5f;Key(EKeys::W,IE_Released);Key(EKeys::D,IE_Released);Key(EKeys::LeftShift,IE_Released);Move->StopMovementImmediately();Advance(3);return true;}
        if(Phase==3)
        {Physical &= !Pace->IsSlowWalking() && Move->MaxWalkSpeed==600;Key(EKeys::LeftShift,IE_Pressed);Advance(4);return true;}
        if(Phase==4)
        {Paused=Pace->IsSlowWalking() && UGameplayStatics::SetGamePaused(World,true);Advance(5,.3);return true;}
        if(Phase==5)
        {Paused &= !Pace->IsSlowWalking() && Move->MaxWalkSpeed==600;Key(EKeys::LeftShift,IE_Released);Advance(6);return true;}
        if(Phase==6){Paused &= !PC->IsInputKeyDown(EKeys::LeftShift);UGameplayStatics::SetGamePaused(World,false);Key(EKeys::LeftShift,IE_Pressed);Advance(7);return true;}
        if(Phase==7)
        {Loaded=Pace->IsSlowWalking() && Hero->SaveProgress(TEXT("PaceAutomation")) && Hero->LoadProgress(TEXT("PaceAutomation")) && !Pace->IsSlowWalking() && Move->MaxWalkSpeed==600;Key(EKeys::LeftShift,IE_Released);Advance(8);return true;}
        if(Phase==8){Key(EKeys::LeftShift,IE_Pressed);Advance(9);return true;}
        if(Phase==9)
        {Focus=Pace->IsSlowWalking();Pace->HandleActivationChanged(false);Focus &= !Pace->IsSlowWalking() && Move->MaxWalkSpeed==600;Key(EKeys::LeftShift,IE_Released);Advance(10);return true;}
        if(Phase==10){Key(EKeys::LeftShift,IE_Pressed);Advance(11);return true;}
        if(Phase==11)
        {Death=Pace->IsSlowWalking();UGameplayStatics::ApplyDamage(Hero,1000,nullptr,nullptr,nullptr);Death &= Hero->Health==0 && !Pace->IsSlowWalking() && Move->MaxWalkSpeed==600 && Hero->RespawnAtBase() && !Pace->IsSlowWalking();Key(EKeys::LeftShift,IE_Released);Advance(12);return true;}
        const bool Pass=Gates && Physical && Chord && Paused && Loaded && Focus && Death;
        UE_LOG(LogTemp,Display,TEXT("CF_PACE %s gates=%d movement=%d chord=%d pause=%d load=%d focusCallback=%d death=%d fast=%.1f slow=%.1f diagonal=%.1f gameSeconds=%.3f/%.3f/%.3f"),Pass?TEXT("PASS"):TEXT("FAIL"),Gates,Physical,Chord,Paused,Loaded,Focus,Death,Fast,Slow,Diagonal,FastSeconds,SlowSeconds,DiagonalSeconds);FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
};
}
void RunPlayerPaceScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)It->Destroy();
    auto* PC=Cast<APlayerController>(Hero->GetController());if(!PC || !Hero->Pace){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    auto* Pace=Hero->Pace.Get();auto* Move=Hero->GetCharacterMovement();bool Gates=CFInputQA::UniqueBindings(Hero);
    for(float Original:{600.f,300.f,120.f})
    {Move->MaxWalkSpeed=Original;Gates &= CFInputQA::Press(Hero,EKeys::LeftShift) && Pace->IsSlowWalking() && Move->MaxWalkSpeed==FMath::Min(192.f,Original);CFInputQA::Press(Hero,EKeys::LeftShift);Gates &= CFInputQA::Release(Hero,EKeys::LeftShift) && !Pace->IsSlowWalking() && Move->MaxWalkSpeed==Original;}
    Move->MaxWalkSpeed=600;CFInputQA::Press(Hero,EKeys::LeftShift);
    Gates &= !Hero->LoadProgress(TEXT("PaceMissing_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)) && Pace->IsSlowWalking();CFInputQA::Release(Hero,EKeys::LeftShift);
    auto State=MakeShared<FPaceTest>();State->World=World;State->Hero=Hero;State->PC=PC;State->Gates=Gates;State->Place();State->Key(EKeys::W,IE_Pressed);State->Next=FPlatformTime::Seconds()+.8;State->Deadline=FPlatformTime::Seconds()+15;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float){return State->Tick();}),.05f);
}
