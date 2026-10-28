#include "BefriendingScenario.h"
#include "CFPlayerCharacter.h"
#include "CFHUD.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "InventoryComponent.h"
#include "InputRoutingQA.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
struct FPeacefulPreview
{
    UWorld* World;ACFPlayerCharacter* Hero;ACreatureCharacter* Pet;ACFHUD* HUD;FGuid Id;FString Root;
    int32 Phase=0;double Next=0,Deadline=0;uint64 Frame=0;
    void Aim()
    {const auto View=(Pet->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);}
    bool Fail()
    {UE_LOG(LogTemp,Error,TEXT("CF_BEFRIEND_PREVIEW FAIL phase=%d feedback=%s"),Phase,*Hero->Feedback);FPlatformMisc::RequestExitWithStatus(false,1);return false;}
    bool Tick()
    {
        const double Now=FPlatformTime::Seconds();if(Now>Deadline)return Fail();if(Now<Next)return true;
        const auto& Audit=HUD->GetLayoutAudit();
        if(Phase==0 || Phase==2)
        {
            if(Audit.Frame<=Frame)return true;
            const bool Joined=Phase==2;
            if(!IsValid(Pet) || Pet->PersistentId!=Id || Pet->Health!=Pet->GetMaxHealth() ||
                Hero->Inventory->Count(EFrontierItem::Food)!=(Joined?7:10) || Hero->Inventory->Count(EFrontierItem::CaptureDevice)!=5 ||
                (Joined?(Pet->Tamer!=Hero->GetController() || Pet->BondPoints!=3 || !Hero->HasJournalEntry(Pet->Definition,true)):(Pet->Tamer!=nullptr || Pet->Disposition!=ECreatureDisposition::Wild)) ||
                !Audit.bFits || !Audit.bNoOverlap || !Audit.bCenterClear || Audit.Context!=(Joined?EFrontierHUDContext::Companion:EFrontierHUDContext::Wild))return Fail();
            UE_LOG(LogTemp,Display,TEXT("CF_BEFRIEND_FRAME PASS joined=%d viewport=%.0fx%.0f"),Joined,Audit.Viewport.X,Audit.Viewport.Y);
            FScreenshotRequest::RequestScreenshot(Root/(Joined?TEXT("Joined.png"):TEXT("Approach.png")),true,false);
            ++Phase;Next=Now+.3;return true;
        }
        if(Phase==1)
        {
            if(IFileManager::Get().FileSize(*(Root/TEXT("Approach.png")))<1000)return true;
            Aim();if(!CFInputQA::Press(Hero,EKeys::J) || Pet->Tamer!=Hero->GetController() ||
                !Hero->SaveProgress(TEXT("BefriendPreview")) || !Hero->LoadProgress(TEXT("BefriendPreview")))return Fail();
            Pet=nullptr;for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->PersistentId==Id){Pet=*It;break;}
            if(!Pet)return Fail();
            // 结交/保存/恢复已经实际执行，恢复后的画面仅冻结伙伴取景，非跟随移动验收。
            Pet->SetActorTickEnabled(false);Pet->GetCharacterMovement()->DisableMovement();
            Hero->GetCharacterMovement()->DisableMovement();Aim();Frame=HUD->GetLayoutAudit().Frame;Phase=2;Next=Now+.6;return true;
        }
        if(IFileManager::Get().FileSize(*(Root/TEXT("Joined.png")))<1000)return true;
        UE_LOG(LogTemp,Display,TEXT("CF_BEFRIEND_PREVIEW PASS scenes=2 input=1 restored=1"));FPlatformMisc::RequestExitWithStatus(false,0);return false;
    }
};
}
void RunBefriendingPreview(UWorld* World,ACFPlayerCharacter* Hero)
{
    auto* Definition=LoadObject<UCreatureDefinition>(nullptr,TEXT("/Game/Creatures/DA_Mosskip.DA_Mosskip"));
    auto* PC=Cast<APlayerController>(Hero->GetController());auto* HUD=PC?Cast<ACFHUD>(PC->GetHUD()):nullptr;
    if(!Definition || Definition->PeacefulFoodCost!=3 || !HUD){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)It->Destroy();
    Hero->SetActorLocation(FVector(-450,-1100,40));Hero->GetCharacterMovement()->DisableMovement();
    Hero->Inventory->Restore({{EFrontierItem::Food,10},{EFrontierItem::CaptureDevice,5}});
    const FTransform At(FRotator::ZeroRotator,Hero->GetActorLocation()+FVector(180,0,0));
    auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),At);Pet->Definition=Definition;Pet->FinishSpawning(At);
    // 无栖息地的静止夹具保留实际Actor Tick，用于近距平静与J路由，不冒充巡游专项。
    Pet->GetCharacterMovement()->DisableMovement();Hero->Feedback=TEXT("轻轻靠近苔跳兽，按J分享3份食物，无需伤害或捕捉器");
    auto State=MakeShared<FPeacefulPreview>();State->World=World;State->Hero=Hero;State->Pet=Pet;State->HUD=HUD;State->Id=Pet->PersistentId;
    State->Root=FPaths::ProjectSavedDir()/TEXT("BefriendPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits);IFileManager::Get().MakeDirectory(*State->Root,true);
    UE_LOG(LogTemp,Display,TEXT("CF_BEFRIEND_PREVIEW_ROOT %s"),*State->Root);State->Aim();State->Next=FPlatformTime::Seconds()+.7;State->Deadline=FPlatformTime::Seconds()+30;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float){return State->Tick();}),.1f);
}
