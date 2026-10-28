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
    auto Inspect=[Door](const TCHAR* Stage,int32 Preview=-1)
    {
        TArray<UStaticMeshComponent*> Meshes;Door->GetComponents(Meshes);
        bool Pass=Meshes.Num()==4;
        UMaterialInstanceDynamic* Frame=nullptr;
        UMaterialInstanceDynamic* Leaf=nullptr;
        for(const auto* Mesh:Meshes)
        {
            auto* Dynamic=Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
            const bool IsLeaf=Mesh->GetFName()==FName(TEXT("Part3"));
            if(IsLeaf) Leaf=Dynamic; else if(!Frame) Frame=Dynamic; else Pass &= Frame==Dynamic;
            const FLinearColor Expected=Preview>=0?(Preview?FLinearColor(.08f,.8f,.15f):FLinearColor(.9f,.04f,.02f)):
                (IsLeaf?Door->Definition->DoorLeafColor:Door->Definition->Color);
            const FLinearColor Actual=Dynamic?Dynamic->K2_GetVectorParameterValue(TEXT("Tint")):FLinearColor::Transparent;
            const bool Matches=Dynamic && Actual.Equals(Expected,.0001f);
            UE_LOG(LogTemp,Display,TEXT("CF_HOME_PALETTE_PART stage=%s part=%s dynamic=%d matches=%d actual=%s expected=%s material=%s"),
                Stage,*Mesh->GetName(),Dynamic!=nullptr,Matches,*Actual.ToString(),*Expected.ToString(),*GetPathNameSafe(Dynamic));
            Pass &= Matches;
        }
        const bool Independent=Frame && Leaf && Frame!=Leaf;
        UE_LOG(LogTemp,Display,TEXT("CF_HOME_PALETTE_INDEPENDENT stage=%s pass=%d"),Stage,Independent);
        return Pass && Independent;
    };
    const bool Built=Inspect(TEXT("built"));
    // 普通模块不能被预览提示染色；重构、红绿切换后须恢复定义颜色。
    Door->SetPreviewTint(false);
    bool PreviewPass=Inspect(TEXT("normalGuard"));
    Door->OnConstruction(Door->GetActorTransform());
    PreviewPass &= Inspect(TEXT("reconstructed"));
    Door->bPreview=true;Door->OnConstruction(Door->GetActorTransform());
    Door->SetPreviewTint(true);PreviewPass &= Inspect(TEXT("previewValid"),1);
    Door->SetPreviewTint(false);PreviewPass &= Inspect(TEXT("previewInvalid"),0);
    Door->SetPreviewTint(true);PreviewPass &= Inspect(TEXT("previewValidAgain"),1);
    Door->bPreview=false;Door->OnConstruction(Door->GetActorTransform());
    PreviewPass &= Inspect(TEXT("restored"));
    const auto Facing=(FVector(-555,-1000,120)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
    Hero->GetController()->SetControlRotation(Facing);Hero->FirstPersonCamera->SetWorldRotation(Facing);
    const bool Opened=Door->ToggleDoor(Hero) && Door->DoorState==2 && Inspect(TEXT("opened"));
    const bool Closed=Door->ToggleDoor(Hero) && Door->DoorState==1 && Inspect(TEXT("closed"));
    FTimerHandle Timer;
    World->GetTimerManager().SetTimer(Timer,[Door,Inspect,Built,Opened,Closed,PreviewPass]()
    {
        const bool Pass=IsValid(Door) && Built && Opened && Closed && PreviewPass && Inspect(TEXT("settled"));
        UE_LOG(LogTemp,Display,TEXT("CF_HOME_PALETTE_PREVIEW %s"),PreviewPass?TEXT("PASS"):TEXT("FAIL"));
        UE_LOG(LogTemp,Display,TEXT("CF_HOME_PALETTE %s built=%d opened=%d closed=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Built,Opened,Closed);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    },2.f,false);
}
