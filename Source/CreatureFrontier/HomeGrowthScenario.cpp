#include "ConstructionScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "HomeModule.h"
#include "HomeBuildingComponent.h"
#include "InventoryComponent.h"
#include "StorageInteractionComponent.h"
#include "WorkStation.h"
#include "FarmCropComponent.h"
#include "FrontierSaveGame.h"
#include "InputRoutingQA.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Controller.h"
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
// 从真实胜利档使用原输入放地板／箱子和存料；不移动玩家、不直接建造或转移库存。
struct FHomeGrowth
{
    UWorld* World;ACFPlayerCharacter* Hero;TWeakObjectPtr<ACreatureCharacter> Pet;
    FGuid PetId;FString Nickname,Root;int32 Phase=0;double At=0,Deadline=0;bool Preview=false;
    FFacilitySaveRecord Garden;
    const FVector FloorPoint=FVector(0,-400,-40),BoxPoint=FVector(-100,-300,-30);
    void Face(FVector Point){const auto R=(Point-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(R);Hero->FirstPersonCamera->SetWorldRotation(R);}
    void Photo(const TCHAR* Name){if(Preview)FScreenshotRequest::RequestScreenshot(Root/(FString(Name)+TEXT(".png")),false,false);At=World->GetTimeSeconds()+.4;}
    bool End(bool Pass,const TCHAR* Reason){if(!Pass)Hero->SaveProgress(TEXT("HomeGrowthBlocked"));UE_LOG(LogTemp,Display,TEXT("CF_HOME_GROWTH %s reason=%s phase=%d feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Reason,Phase,*Hero->Feedback);FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;}
    AHomeModule* Find(EHomeModuleKind Kind)const{for(TActorIterator<AHomeModule> It(World);It;++It)if(!It->bPreview && !It->bStaged && It->GetKind()==Kind)return *It;return nullptr;}
    bool Budget()const
    {
        int32 Total=Hero->Inventory->Count(EFrontierItem::Material),Farms=0;
        for(TActorIterator<AHomeModule> It(World);It;++It)if(!It->bPreview && !It->bStaged){if(It->bUnderConstruction)return false;Total+=It->PaidMaterials+It->StorageInventory->Count(EFrontierItem::Material);}
        for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt)
        {++Farms;if(It->PersistentId!=Garden.Id || !It->GetActorTransform().Equals(Garden.Transform,.01) || It->StoredFood!=Garden.StoredFood || It->Crop->Seeds!=Garden.Seeds || It->Crop->bPlanted!=Garden.bPlanted || !FMath::IsNearlyEqual(It->Crop->GrowthWork,Garden.GrowthWork,.01f))return false;}
        return Total==28 && Farms==1 && Pet.IsValid() && Pet->PersistentId==PetId && Pet->Nickname==Nickname && Pet->Level==2 && Pet->Experience==37 && Pet->BondPoints==3 && Pet->Health>0 && Hero->Health>0 && Hero->GetObjectives().Claimed==5 && Hero->GetObjectives().bAlphaResolved && Hero->Inventory->Count(EFrontierItem::CaptureDevice)==10 && Hero->Inventory->Count(EFrontierItem::Food)==2 && Hero->Inventory->Count(EFrontierItem::Seeds)==1;
    }
    bool Tick()
    {
        if(FPlatformTime::Seconds()>Deadline || !Budget())return End(false,TEXT("budget-lifetime"));
        if(World->GetTimeSeconds()<At)return true;
        switch(Phase++)
        {
        case 0:
            if(!Hero->GetObjectiveHint().Contains(TEXT("H治疗自己")))return End(false,TEXT("recovery-hint"));Photo(TEXT("Recovery"));return true;
        case 1:
            if(!CFInputQA::Press(Hero,EKeys::H) || !CFInputQA::Press(Hero,EKeys::H) || Hero->Health!=100 || Hero->Inventory->Count(EFrontierItem::Medicine)!=0 || !Hero->GetObjectiveHint().Contains(TEXT("木地板")))return End(false,TEXT("floor-hint"));
            Photo(TEXT("FloorHelp"));return true;
        case 2:
            Face(FloorPoint);if(!CFInputQA::Press(Hero,EKeys::B) || !CFInputQA::Press(Hero,EKeys::Eight))return End(false,TEXT("floor-menu"));
            Hero->HomeBuilding->UpdatePreview();
            if(!Hero->HomeBuilding->GetPreview() || !Hero->HomeBuilding->GetPreview()->GetActorLocation().Equals(FloorPoint,.01))return End(false,TEXT("floor-preview"));Photo(TEXT("FloorPreview"));return true;
        case 3:
            if(!CFInputQA::Action(Hero,TEXT("Attack"),EKeys::LeftMouseButton) || !Find(EHomeModuleKind::Floor) || Hero->Inventory->Count(EFrontierItem::Material)!=20 || !CFInputQA::Press(Hero,EKeys::B) || !Hero->GetObjectiveHint().Contains(TEXT("储物箱")))return End(false,TEXT("floor-build"));
            Photo(TEXT("StorageHelp"));return true;
        case 4:
            Face(BoxPoint);if(!CFInputQA::Press(Hero,EKeys::B) || !CFInputQA::Press(Hero,EKeys::Eight))return End(false,TEXT("storage-menu"));
            for(int32 I=0;I<5;++I)if(!CFInputQA::Press(Hero,EKeys::V))return End(false,TEXT("storage-select"));
            Hero->HomeBuilding->UpdatePreview();if(!Hero->HomeBuilding->GetPreview() || !Hero->HomeBuilding->GetPreview()->GetActorLocation().Equals(BoxPoint,.01))return End(false,TEXT("storage-preview"));Photo(TEXT("StoragePreview"));return true;
        case 5:
            if(!CFInputQA::Action(Hero,TEXT("Attack"),EKeys::LeftMouseButton) || !Find(EHomeModuleKind::Storage) || Hero->Inventory->Count(EFrontierItem::Material)!=14 || !CFInputQA::Press(Hero,EKeys::B) || !Hero->GetObjectiveHint().Contains(TEXT("施工架")))return End(false,TEXT("storage-build"));
            Photo(TEXT("Ready"));return true;
        case 6:
            Face(BoxPoint+FVector(0,0,35));if(!CFInputQA::Press(Hero,EKeys::E) || Hero->Storage->GetCrate()!=Find(EHomeModuleKind::Storage) || !CFInputQA::Press(Hero,EKeys::Right) || Hero->Inventory->Count(EFrontierItem::Material)!=0 || Find(EHomeModuleKind::Storage)->StorageInventory->Count(EFrontierItem::Material)!=14)return End(false,TEXT("storage-supply"));
            Photo(TEXT("Stocked"));return true;
        default:
            if(!CFInputQA::Press(Hero,EKeys::E) || !Hero->SaveProgress(TEXT("HomeGrowthDone")) || !Hero->LoadProgress(TEXT("HomeGrowthDone")))return End(false,TEXT("save"));
            Pet.Reset();for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->PersistentId==PetId)Pet=*It;
            if(!Budget() || !Find(EHomeModuleKind::Storage) || Find(EHomeModuleKind::Storage)->StorageInventory->Count(EFrontierItem::Material)!=14 || !Hero->GetObjectiveHint().Contains(TEXT("施工架")))return End(false,TEXT("restore"));
            UE_LOG(LogTemp,Display,TEXT("CF_HOME_GROWTH_LIFE PASS version=38 hints=4 paid=14 stored=14 heroHP=100 medicineUsed=2 homeRetained=1"));return End(true,TEXT("complete"));
        }
    }
};
}
void RunHomeGrowthScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto S=MakeShared<FHomeGrowth>();S->World=World;S->Hero=Hero;S->Deadline=FPlatformTime::Seconds()+60;
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("HomeGrowthInput"),0));
    if(!Source || Source->FormatVersion!=38 || Source->Facilities.Num()!=1 || !Hero->LoadProgress(TEXT("HomeGrowthInput"))){S->End(false,TEXT("source"));return;}S->Garden=Source->Facilities[0];
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController())S->Pet=*It;
    if(!S->Pet.IsValid()){S->End(false,TEXT("partner"));return;}S->PetId=S->Pet->PersistentId;S->Nickname=S->Pet->Nickname;
    if(!S->Budget() || !Hero->GetActorTransform().Equals(Source->PlayerTransform,.01)){S->End(false,TEXT("source-budget"));return;}
    if(Read)
    {
        bool Pass=Source->HomeModules.Num()==2 && Hero->Health==100 && Hero->Inventory->Count(EFrontierItem::Medicine)==0 && S->Find(EHomeModuleKind::Floor) && S->Find(EHomeModuleKind::Storage) && S->Find(EHomeModuleKind::Storage)->StorageInventory->Count(EFrontierItem::Material)==14 && Hero->GetObjectiveHint().Contains(TEXT("施工架"));
        Pass &= Hero->SaveProgress(TEXT("HomeGrowthDone"));UE_LOG(LogTemp,Display,TEXT("CF_HOME_GROWTH_READ %s paid=14 stored=14 hints=1"),Pass?TEXT("PASS"):TEXT("FAIL"));S->End(Pass,TEXT("read"));return;
    }
    if(!Source->HomeModules.IsEmpty() || Hero->Inventory->Count(EFrontierItem::Material)!=28 || Hero->Inventory->Count(EFrontierItem::Medicine)!=2 || Hero->Health>=100){S->End(false,TEXT("victory-source"));return;}
    S->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFHomeGrowthPreview"));
    if(S->Preview){S->Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("HomeGrowthPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*S->Root,true);UE_LOG(LogTemp,Display,TEXT("CF_HOME_GROWTH_PREVIEW_ROOT %s"),*S->Root);}
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){return S->Tick();}));
}
