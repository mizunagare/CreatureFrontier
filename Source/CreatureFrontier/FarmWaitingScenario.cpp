#include "FarmingScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "FarmCropComponent.h"
#include "WorkStation.h"
#include "HomeLayout.h"
#include "InventoryComponent.h"
#include "FrontierSaveGame.h"
#include "WildEncounter.h"
#include "InputRoutingQA.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/Ticker.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

namespace
{
struct FFarmWaitTest
{
    UWorld* World;ACFPlayerCharacter* Hero;bool Full=false,Read=false,Preview=false;int32 Phase=0;
    TWeakObjectPtr<ACreatureCharacter> Pet;TWeakObjectPtr<AWorkStation> Farm;FGuid PetId,FarmId;
    double Deadline=0,At=0,NextPhoto=0;float Before=0;int32 BeforeXp=9;FString ImageRoot;TSet<FString> Photos;
    void Face(){const auto R=(Farm->GetActorLocation()+FVector(0,0,30)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(R);Hero->FirstPersonCamera->SetWorldRotation(R);}
    bool Bind()
    {
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController())Pet=*It;
        for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt && It->GetActorLocation().Equals(FVector(-1200,-300,325),.01))Farm=*It;
        if(!Pet.IsValid() || !Farm.IsValid() || (PetId.IsValid() && Pet->PersistentId!=PetId) || (FarmId.IsValid() && Farm->PersistentId!=FarmId))return false;
        PetId=Pet->PersistentId;FarmId=Farm->PersistentId;
        for(TActorIterator<AWildEncounter> It(World);It;++It)It->SetActorTickEnabled(false);
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(*It!=Pet.Get()){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();}
        return true;
    }
    bool End(bool Pass,const TCHAR* Reason)
    {
        const FString Feedback=Hero->Feedback;if(!Pass)Hero->SaveProgress(TEXT("FarmWaitingBlocked"));
        UE_LOG(LogTemp,Display,TEXT("CF_FARM_WAIT %s mode=%s reason=%s phase=%d stamina=%.3f before=%.3f seeds=%d planted=%d growth=%.3f stock=%d xp=%d distance=%.2f feedback=%s"),
            Pass?TEXT("PASS"):TEXT("FAIL"),Full?TEXT("full"):TEXT("empty"),Reason,Phase,Pet.IsValid()?Pet->Stamina:-1.f,Before,
            Farm.IsValid()?Farm->Crop->Seeds:-1,Farm.IsValid() && Farm->Crop->bPlanted,Farm.IsValid()?Farm->Crop->GrowthWork:-1.f,Farm.IsValid()?Farm->StoredFood:-1,Pet.IsValid()?Pet->Experience:-1,Pet.IsValid() && Farm.IsValid()?Pet->GetDistanceTo(Farm.Get()):-1.f,*Feedback);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    void BeginWait(int32 Next){Phase=Next;Before=Pet->Stamina;BeforeXp=Pet->Experience;At=World->GetTimeSeconds();}
    bool Tick()
    {
        if(!Pet.IsValid() || !Farm.IsValid() || FPlatformTime::Seconds()>Deadline || HomeLayout::Collect(World).Num()!=25)return End(false,TEXT("lifetime"));
        if(!Pet->GetActorEnableCollision() || !Pet->IsActorTickEnabled() || Pet->GetCharacterMovement()->MovementMode==MOVE_None || Pet->Health<=0 || Pet->WorkSite!=Farm.Get() || Pet->Activity!=ECreatureActivity::Work)return End(false,TEXT("worker-state"));
        // 仅延后交互，不暂停世界；伙伴需求与原工作任务继续运行。
        if(FPlatformTime::Seconds()<NextPhoto)return true;
        if(Phase==0)
        {
            const bool Ready=Full?Farm->Crop->Stage()==3:!Farm->Crop->bPlanted && Farm->Crop->Seeds==0;
            if(Ready && Pet->GetActorLocation().Z>350 && Pet->GetDistanceTo(Farm.Get())<=220)BeginWait(1);
            return true;
        }
        if(Phase==1 || Phase==3)
        {
            if(World->GetTimeSeconds()-At<5)return true;
            const FString Scene=Phase==1?TEXT("Waiting"):TEXT("WaitingAgain");
            if(Preview && !Photos.Contains(Scene))
            {
                Face();if(Hero->TraceFacility()!=Farm.Get())return End(false,TEXT("preview-aim"));Photos.Add(Scene);
                FScreenshotRequest::RequestScreenshot(ImageRoot/(Scene+TEXT(".png")),false,false);NextPhoto=FPlatformTime::Seconds()+.2;return true;
            }
            const float Loss=Before-Pet->Stamina;
            UE_LOG(LogTemp,Display,TEXT("CF_FARM_WAIT_MEASURE mode=%s stage=%d seconds=%.3f loss=%.3f xp=%d"),Full?TEXT("full"):TEXT("empty"),Phase,World->GetTimeSeconds()-At,Loss,Pet->Experience);
            if(FMath::Abs(Loss)>.05f || Pet->Experience!=BeforeXp || (Phase==3 && (Farm->Crop->Stage()!=3 || Farm->Crop->Seeds!=0 || Farm->StoredFood!=30)))return End(false,TEXT("idle-cost"));
            if(Phase==1 && !Read && !Hero->SaveProgress(TEXT("FarmWaitingHold")))return End(false,TEXT("hold-save"));
            Face();
            if(Full)
            {
                const int32 Stock=Farm->StoredFood;
                if(Stock!=30 || Hero->TraceFacility()!=Farm.Get() || !CFInputQA::Press(Hero,EKeys::O) || Farm->StoredFood!=25)return End(false,TEXT("withdraw"));
            }
            else if(!CFInputQA::Press(Hero,EKeys::F2) || Farm->Crop->Seeds!=1)return End(false,TEXT("seed"));
            UE_LOG(LogTemp,Display,TEXT("CF_FARM_WAIT_RESUME PASS mode=%s cycle=%d originalTask=1"),Full?TEXT("full"):TEXT("empty"),Phase==1?1:2);
            Before=Pet->Stamina;Phase=Phase==1?2:4;At=World->GetTimeSeconds();return true;
        }
        if(Phase==5)
        {
            if(Farm->Crop->Stage()!=3){if(World->GetTimeSeconds()-At>30)return End(false,TEXT("second-growth"));return true;}
            if(Pet->Stamina>=Before-1 || Farm->StoredFood!=30 || Pet->Experience!=10)return End(false,TEXT("second-effort"));
            BeginWait(3);return true;
        }
        const int32 ExpectedXp=Phase==4?11:10;
        if(Pet->Experience<ExpectedXp){if(World->GetTimeSeconds()-At>30)return End(false,TEXT("resume-timeout"));return true;}
        // 成熟作物仅结算不再扣生长体力；补种后的新生长必须仍有正常工作消耗。
        if(Pet->Experience!=ExpectedXp || Farm->StoredFood!=(Full?26:1) || Farm->Crop->bPlanted || Farm->Crop->Seeds!=0 ||
            (!Full && Pet->Stamina>=Before-1) || (Full && FMath::Abs(Pet->Stamina-Before)>.05f))return End(false,TEXT("production-or-effort"));
        if(Full){Face();if(!CFInputQA::Press(Hero,EKeys::U) || Farm->StoredFood!=30)return End(false,TEXT("refill"));}
        if(Full && Phase==2)
        {
            Face();if(!CFInputQA::Press(Hero,EKeys::F2) || Farm->Crop->Seeds!=1)return End(false,TEXT("full-reservation"));
            Before=Pet->Stamina;At=World->GetTimeSeconds();Phase=5;return true;
        }
        if(!Hero->SaveProgress(TEXT("FarmWaitingDone")) || !Hero->LoadProgress(TEXT("FarmWaitingDone")) || !Bind() || Pet->WorkSite!=Farm.Get() || Pet->Experience!=ExpectedXp ||
            Farm->StoredFood!=(Full?30:1) || Hero->Inventory->Count(EFrontierItem::Food)!=(Full?2:0) || Hero->Inventory->Count(EFrontierItem::Seeds)!=0 || Hero->Inventory->Count(EFrontierItem::Material)!=0)return End(false,TEXT("done-save"));
        UE_LOG(LogTemp,Display,TEXT("CF_FARM_WAIT_SAVED PASS version=37 mode=%s modules=25 sameTask=1 food=%d seeds=0 xp=%d"),Full?TEXT("full"):TEXT("empty"),Full?32:1,ExpectedXp);return End(true,TEXT("complete"));
    }
};
}
void RunFarmWaitingScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Full,bool Read)
{
    auto State=MakeShared<FFarmWaitTest>();State->World=World;State->Hero=Hero;State->Full=Full;State->Read=Read;State->Deadline=FPlatformTime::Seconds()+100;
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("FarmWaitingInput"),0));
    if(!Source || Source->FormatVersion!=37 || Source->HomeModules.Num()!=25 || Source->Facilities.Num()!=3 || !Hero->LoadProgress(TEXT("FarmWaitingInput")) || !State->Bind() ||
        State->Pet->Experience!=9 || State->Pet->BondPoints!=12){State->End(false,TEXT("source"));return;}
    State->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFFarmWaitPreview"));
    if(State->Preview){State->ImageRoot=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("FarmWaitingPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*State->ImageRoot,true);UE_LOG(LogTemp,Display,TEXT("CF_FARM_WAIT_PREVIEW_ROOT %s"),*State->ImageRoot);}
    if(Read)
    {
        if(State->Pet->WorkSite!=State->Farm.Get() || State->Farm->StoredFood!=(Full?30:0) || State->Farm->Crop->Seeds!=0 || (Full?State->Farm->Crop->Stage()!=3:State->Farm->Crop->bPlanted)){State->End(false,TEXT("restore"));return;}
        UE_LOG(LogTemp,Display,TEXT("CF_FARM_WAIT_RESTORE PASS version=37 mode=%s sameTask=1"),Full?TEXT("full"):TEXT("empty"));
    }
    else
    {
        // 明确的容量／预留种子单元夹具；作物必须走原按键播种和真实工作成熟，不能直接填生长进度。
        if(Hero->Inventory->Count(EFrontierItem::Seeds)!=0 || Hero->Inventory->Count(EFrontierItem::Food)!=0 || !Hero->Inventory->AddItem(EFrontierItem::Seeds,Full?2:1) ||
            (Full && !State->Farm->AdjustFoodStock(30))){State->End(false,TEXT("fixture"));return;}
        Hero->SetActorLocation(FVector(-1330,-450,380.15));State->Face();
        if(Full && (!CFInputQA::Press(Hero,EKeys::F2) || !CFInputQA::Press(Hero,EKeys::G) || !State->Farm->Crop->bPlanted || State->Farm->Crop->GrowthWork!=0)){State->End(false,TEXT("sow"));return;}
        if(!CFInputQA::Press(Hero,EKeys::E) || State->Pet->WorkSite!=State->Farm.Get()){State->End(false,TEXT("order"));return;}
        UE_LOG(LogTemp,Display,TEXT("CF_FARM_WAIT_FIXTURE mode=%s initialFood=%d initialSeeds=%d cropProgress=0 actualWorker=1"),Full?TEXT("full"):TEXT("empty"),Full?30:0,Full?2:1);
    }
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float){return State->Tick();}));
}
