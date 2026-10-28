#include "HomePaletteScenario.h"
#include "HomeDoorScenario.h"
#include "HomeModule.h"
#include "CFPlayerCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"

void RunHomePaletteScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    // 使用游戏世界里的正常付费建造，不把Editor临时Actor的结果当作运行时证据。
    auto* Door=CFDoorNavigationQA::Room(World,Hero,0);
    if(!Door || !Door->Definition){UE_LOG(LogTemp,Error,TEXT("CF_HOME_PALETTE FAIL fixture"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    const FLinearColor Expected=Door->Definition->Color;
    auto Inspect=[Door,Expected](const TCHAR* Stage)
    {
        TArray<UStaticMeshComponent*> Meshes;Door->GetComponents(Meshes);
        bool Pass=Meshes.Num()==4;
        for(const auto* Mesh:Meshes)
        {
            auto* Dynamic=Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
            const FLinearColor Actual=Dynamic?Dynamic->K2_GetVectorParameterValue(TEXT("Tint")):FLinearColor::Transparent;
            const bool Matches=Dynamic && Actual.Equals(Expected,.0001f);
            UE_LOG(LogTemp,Display,TEXT("CF_HOME_PALETTE_PART stage=%s part=%s dynamic=%d matches=%d actual=%s expected=%s material=%s"),
                Stage,*Mesh->GetName(),Dynamic!=nullptr,Matches,*Actual.ToString(),*Expected.ToString(),*GetPathNameSafe(Dynamic));
            Pass &= Matches;
        }
        return Pass;
    };
    const bool Built=Inspect(TEXT("built"));
    const auto Facing=(FVector(-555,-1000,120)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
    Hero->GetController()->SetControlRotation(Facing);Hero->FirstPersonCamera->SetWorldRotation(Facing);
    const bool Opened=Door->ToggleDoor(Hero) && Door->DoorState==2 && Inspect(TEXT("opened"));
    const bool Closed=Door->ToggleDoor(Hero) && Door->DoorState==1 && Inspect(TEXT("closed"));
    FTimerHandle Timer;
    World->GetTimerManager().SetTimer(Timer,[Door,Inspect,Built,Opened,Closed]()
    {
        const bool Pass=IsValid(Door) && Built && Opened && Closed && Inspect(TEXT("settled"));
        UE_LOG(LogTemp,Display,TEXT("CF_HOME_PALETTE %s built=%d opened=%d closed=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Built,Opened,Closed);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    },2.f,false);
}
