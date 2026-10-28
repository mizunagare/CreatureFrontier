#include "HomeDoorScenario.h"
#include "HomeScenario.h"
#include "HomeModule.h"
#include "CFPlayerCharacter.h"
#include "PlayerPaceComponent.h"
#include "CFHUD.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Engine/GameViewportClient.h"

namespace CFDoorPlayerQA
{
bool Toggle(ACFPlayerCharacter* Hero,AHomeModule* Door,float Orientation)
{
    const auto Aim=(CFDoorNavigationQA::Point(FVector(-555,-1000,120),Orientation)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
    Hero->GetController()->SetControlRotation(Aim);Hero->FirstPersonCamera->SetWorldRotation(Aim);
    const uint8 Before=Door->DoorState;Hero->CommandWork();return Door->DoorState!=Before;
}
void AimFront(ACFPlayerCharacter* Hero,float Orientation)
{
    const auto Aim=(CFDoorNavigationQA::Point(FVector(-400,-1000,120),Orientation)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
    Hero->GetController()->SetControlRotation(Aim);Hero->FirstPersonCamera->SetWorldRotation(Aim);
}
bool Capture(ACFPlayerCharacter* Hero,AHomeModule* Door,const FString& Root,const TCHAR* Scene,const TSharedRef<bool>& StateValid,const TSharedRef<int32>& Processed)
{
    if(Root.IsEmpty())return true;
    auto* PC=Cast<APlayerController>(Hero->GetController());auto* HUD=PC?Cast<ACFHUD>(PC->GetHUD()):nullptr;
    if(!HUD || HUD->GetLayoutAudit().Context!=EFrontierHUDContext::Door)return false;
    const auto& Audit=HUD->GetLayoutAudit();
    const auto* Viewport=Hero->GetWorld()->GetGameViewport();
    if(!Viewport)return false;
    const int32 View=Viewport->ViewModeIndex;
    if(FParse::Param(FCommandLine::Get(),TEXT("CFDoorUnlitPreview")) && View!=VMI_Unlit)return false;
    UE_LOG(LogTemp,Display,TEXT("CF_DOOR_PLAYER_VIEW %s mode=%d lighting=%d materials=%d"),Scene,View,Viewport->EngineShowFlags.Lighting,Viewport->EngineShowFlags.Materials);
    UE_LOG(LogTemp,Display,TEXT("CF_DOOR_PLAYER_SHOT %s viewport=%.0fx%.0f frame=%llu"),Scene,Audit.Viewport.X,Audit.Viewport.Y,Audit.Frame);
    const uint8 Expected=Door->DoorState;const TWeakObjectPtr<AHomeModule> WeakDoor(Door);const FString Label(Scene);
    const auto Handle=MakeShared<FDelegateHandle>();
    *Handle=FScreenshotRequest::OnScreenshotRequestProcessed().AddLambda([WeakDoor,Expected,View,Label,StateValid,Processed,Handle]()
    {
        const auto* Target=WeakDoor.Get();const auto* CurrentViewport=Target?Target->GetWorld()->GetGameViewport():nullptr;
        const bool Valid=Target && Target->DoorState==Expected && CurrentViewport && CurrentViewport->ViewModeIndex==View;
        *StateValid &= Valid;++*Processed;
        UE_LOG(LogTemp,Display,TEXT("CF_DOOR_PLAYER_SHOT_STATE %s %s state=%d expected=%d"),*Label,Valid?TEXT("PASS"):TEXT("FAIL"),Target?Target->DoorState:-1,Expected);
        FScreenshotRequest::OnScreenshotRequestProcessed().Remove(*Handle);
    });
    FScreenshotRequest::RequestScreenshot(Root/(FString(Scene)+TEXT(".png")),false,false);return true;
}
}
void RunHomeDoorPlayerScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Rotated)
{
    const float Orientation=Rotated?90.f:0.f;
    auto* Door=CFDoorNavigationQA::Room(World,Hero,Orientation);
    if(!Door){UE_LOG(LogTemp,Error,TEXT("CF_DOOR_PLAYER FAIL fixture %s"),*Hero->Feedback);FPlatformMisc::RequestExitWithStatus(false,1);return;}
    auto* Movement=NewObject<UHomeMovementProbe>(Hero);Movement->RegisterComponent();
    CFDoorPlayerQA::AimFront(Hero,Orientation);
    FString Root;
    if(FParse::Param(FCommandLine::Get(),TEXT("CFDoorPlayerPreview")))
    {Root=FPaths::ProjectSavedDir()/TEXT("DoorPlayerPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits);IFileManager::Get().MakeDirectory(*Root,true);UE_LOG(LogTemp,Display,TEXT("CF_DOOR_PLAYER_ROOT %s"),*Root);}
    const auto Poll=MakeShared<FTimerHandle>();
    const auto ShotStateValid=MakeShared<bool>(true);const auto Processed=MakeShared<int32>(0);
    World->GetTimerManager().SetTimer(*Poll,[World,Hero,Door,Movement,Root,Poll,Orientation,ShotStateValid,Processed,Phase=0,At=World->GetTimeSeconds(),
        Previous=Hero->GetActorLocation(),Travel=0.,Closed=false,Opened=false,Entered=false,Occupied=false,InsideClosed=false,Returned=false,Continuous=true,Shots=true]() mutable
    {
        const double Elapsed=World->GetTimeSeconds()-At;
        const FVector Position=Hero->GetActorLocation();const double Step=FVector::Dist2D(Position,Previous);Previous=Position;
        Travel+=Step;Continuous &= Step<100;
        const double Y=CFDoorNavigationQA::Original(Position,Orientation).Y;
        const FVector Forward=FRotator(0,Orientation,0).RotateVector(FVector(0,1,0));
        auto Stop=[&](){Movement->Direction=FVector::ZeroVector;Hero->GetCharacterMovement()->StopMovementImmediately();};
        auto Advance=[&](int32 Next){Phase=Next;At=World->GetTimeSeconds();};
        auto Finish=[&](bool Passed)
        {
            Stop();Hero->Pace->StopSlowWalk();
            UE_LOG(LogTemp,Display,TEXT("CF_DOOR_PLAYER %s yaw=%.0f phase=%d closed=%d open=%d entered=%d occupied=%d insideClosed=%d returned=%d continuous=%d shots=%d travel=%.1f hero=%s feedback=%s"),
                Passed?TEXT("PASS"):TEXT("FAIL"),Orientation,Phase,Closed,Opened,Entered,Occupied,InsideClosed,Returned,Continuous,Shots,Travel,*Position.ToString(),*Hero->Feedback);
            FTimerHandle Handle=*Poll;World->GetTimerManager().ClearTimer(Handle);FPlatformMisc::RequestExitWithStatus(false,Passed?0:1);
        };
        if(!Continuous || Hero->Health<=0 || Elapsed>12){Finish(false);return;}
        if(Phase==0 && Elapsed>=2)
        {
            Shots &= CFDoorPlayerQA::Capture(Hero,Door,Root,TEXT("Closed"),ShotStateValid,Processed);
            Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);Hero->Pace->StartSlowWalk();Movement->Direction=Forward;Advance(1);return;
        }
        if(Phase==1 && Elapsed>=3)
        {
            Stop();Closed=Door->DoorState==1 && Y<-1030 && Travel>120;
            Opened=Closed && CFDoorPlayerQA::Toggle(Hero,Door,Orientation) && Door->DoorState==2;
            if(!Opened){Finish(false);return;}
            Movement->Direction=-Forward;Advance(2);return;
        }
        if(Phase==2 && Y<-1200)
        {Stop();CFDoorPlayerQA::Toggle(Hero,Door,Orientation);if(Door->DoorState!=1){Finish(false);return;}CFDoorPlayerQA::Toggle(Hero,Door,Orientation);if(Door->DoorState!=2){Finish(false);return;}Advance(3);return;}
        if(Phase==3 && Elapsed>.35)
        {Shots &= CFDoorPlayerQA::Capture(Hero,Door,Root,TEXT("Open"),ShotStateValid,Processed);Movement->Direction=Forward;Advance(4);return;}
        if(Phase==4 && Y>-930)
        {
            Stop();Entered=Position.Z>40;
            Occupied=!CFDoorPlayerQA::Toggle(Hero,Door,Orientation) && Door->DoorState==2 && Hero->Feedback.Contains(TEXT("活动范围"));
            if(!Entered || !Occupied){Finish(false);return;}Advance(5);return;
        }
        if(Phase==5 && Elapsed>.35)
        {Shots &= CFDoorPlayerQA::Capture(Hero,Door,Root,TEXT("Occupied"),ShotStateValid,Processed);Movement->Direction=Forward;Advance(6);return;}
        if(Phase==6 && Y>-727)
        {
            Stop();InsideClosed=CFDoorPlayerQA::Toggle(Hero,Door,Orientation) && Door->DoorState==1;
            if(!InsideClosed){Finish(false);return;}Advance(7);return;
        }
        if(Phase==7 && Elapsed>.35)
        {
            Shots &= CFDoorPlayerQA::Capture(Hero,Door,Root,TEXT("InsideClosed"),ShotStateValid,Processed);
            // 专用负对照重现同帧重开，确认处理回调能够拒绝错误状态的图片。
            if(FParse::Param(FCommandLine::Get(),TEXT("CFDoorEarlyReopenProbe")))CFDoorPlayerQA::Toggle(Hero,Door,Orientation);
            Advance(11);return;
        }
        // 截图请求在后续渲染帧执行，必须保持关闭状态，不能同帧重开。
        if(Phase==11 && Elapsed>.35)
        {
            if(!CFDoorPlayerQA::Toggle(Hero,Door,Orientation) || Door->DoorState!=2){Finish(false);return;}
            Movement->Direction=-Forward;Advance(8);return;
        }
        if(Phase==8 && Y<-1200)
        {
            Stop();Returned=CFDoorPlayerQA::Toggle(Hero,Door,Orientation) && Door->DoorState==1;
            if(!Returned){Finish(false);return;}Advance(9);return;
        }
        if(Phase==9 && Elapsed>.35)
        {Shots &= CFDoorPlayerQA::Capture(Hero,Door,Root,TEXT("Returned"),ShotStateValid,Processed);Advance(10);return;}
        if(Phase==10 && Elapsed>.75)
        {
            Shots &= *ShotStateValid && (Root.IsEmpty() || *Processed==5);
            if(!Root.IsEmpty())for(const auto* Scene:{TEXT("Closed"),TEXT("Open"),TEXT("Occupied"),TEXT("InsideClosed"),TEXT("Returned")})Shots &= IFileManager::Get().FileSize(*(Root/(FString(Scene)+TEXT(".png"))))>1000;
            Finish(Closed && Opened && Entered && Occupied && InsideClosed && Returned && Shots && Travel>900);return;
        }
    },.05f,true);
}
