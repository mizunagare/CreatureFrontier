#include "HollowScenario.h"
#include "CFPlayerCharacter.h"
#include "CFHUD.h"
#include "CreatureCharacter.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Containers/Ticker.h"
#include "EngineUtils.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
struct FHollowPreview
{
    ACFPlayerCharacter* Hero;ACFHUD* HUD;FString Root;int32 Scene=0;bool Awaiting=false;double Next=0,Deadline=0,Started=0;uint64 FirstFrame=0,LastFrame=0,PeakMemory=0;
    const TCHAR* Name() const {const TCHAR* Names[]={TEXT("Entrance"),TEXT("Switchback"),TEXT("Chamber"),TEXT("Cache")};return Names[Scene];}
    void Setup()
    {
        const FVector Locations[]={FVector(0,-1900,40),FVector(280,-3200,40),FVector(-650,-3900,40),FVector(-700,-4100,40)};
        const FVector Targets[]={FVector(0,-2800,150),FVector(-400,-3700,80),FVector(-400,-3850,80),FVector(-550,-4100,0)};
        Hero->SetActorLocation(Locations[Scene]);Hero->GetCharacterMovement()->DisableMovement();
        const auto View=(Targets[Scene]-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
        Hero->Feedback=TEXT("南侧余烬洞窟：穿过弯道寻找旧机件，带回基地加工；危险仍需应对");
        LastFrame=HUD->GetLayoutAudit().Frame;Next=FPlatformTime::Seconds()+.7;Awaiting=false;
    }
    bool Fail()
    {UE_LOG(LogTemp,Error,TEXT("CF_HOLLOW_PREVIEW FAIL scene=%d"),Scene);FPlatformMisc::RequestExitWithStatus(false,1);return false;}
    bool Tick()
    {
        PeakMemory=FMath::Max(PeakMemory,FPlatformMemory::GetStats().UsedPhysical);
        const double Now=FPlatformTime::Seconds();if(Now>Deadline)return Fail();if(Now<Next)return true;
        if(Awaiting)
        {
            if(IFileManager::Get().FileSize(*(Root/(FString(Name())+TEXT(".png"))))<1000)return true;
            if(++Scene==4)
            {
                const double Seconds=Now-Started;
                UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_METRICS seconds=%.2f frames=%llu fps=%.2f peakSampledMB=%.1f"),Seconds,HUD->GetLayoutAudit().Frame-FirstFrame,(HUD->GetLayoutAudit().Frame-FirstFrame)/Seconds,PeakMemory/1048576.0);
                UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_PREVIEW PASS scenes=4"));FPlatformMisc::RequestExitWithStatus(false,0);return false;
            }
            Setup();return true;
        }
        const auto& Audit=HUD->GetLayoutAudit();if(Audit.Frame<=LastFrame)return true;
        if(!Audit.bFits || !Audit.bNoOverlap || !Audit.bCenterClear || Hero->Health<=0)return Fail();
        UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_FRAME PASS scene=%s viewport=%.0fx%.0f"),Name(),Audit.Viewport.X,Audit.Viewport.Y);
        FScreenshotRequest::RequestScreenshot(Root/(FString(Name())+TEXT(".png")),true,false);Awaiting=true;Next=Now+.3;return true;
    }
};
}
void RunHollowPreview(UWorld* World,ACFPlayerCharacter* Hero)
{
    auto* PC=Cast<APlayerController>(Hero->GetController());auto* HUD=PC?Cast<ACFHUD>(PC->GetHUD()):nullptr;
    if(!HUD){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    // 图形取景冻结居民，不伪装为战斗；行走/采集由独立实际移动专项验证。
    for(TActorIterator<ACreatureCharacter> It(World);It;++It){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();}
    auto State=MakeShared<FHollowPreview>();State->Hero=Hero;State->HUD=HUD;State->FirstFrame=HUD->GetLayoutAudit().Frame;
    State->Root=FPaths::ProjectSavedDir()/TEXT("HollowPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits);IFileManager::Get().MakeDirectory(*State->Root,true);
    UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_PREVIEW_ROOT %s"),*State->Root);State->Started=FPlatformTime::Seconds();State->Deadline=State->Started+30;State->Setup();
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float){return State->Tick();}),.1f);
}
