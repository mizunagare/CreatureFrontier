#include "CropScenario.h"
#include "CFPlayerCharacter.h"
#include "BaseBuildingComponent.h"
#include "FarmCropComponent.h"
#include "WorkStation.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "FrontierSaveGame.h"
#include "InventoryComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace CFCropQA
{
AWorkStation* Garden(UWorld* World)
{for(TActorIterator<AWorkStation> It(World);It;++It) if(It->bPlayerBuilt && It->RequiredSpecialty==EWorkSpecialty::Farming) return *It;return nullptr;}
ACreatureCharacter* Farmer(UWorld* World,ACFPlayerCharacter* Hero)
{for(TActorIterator<ACreatureCharacter> It(World);It;++It) if(It->Tamer==Hero->GetController()) return *It;return nullptr;}
bool Snapshot(UWorld* World,ACFPlayerCharacter* Hero)
{
    auto* Site=Garden(World);auto* Pet=Farmer(World,Hero);
    return Site && Pet && Pet->WorkSite==Site && Site->Crop->Seeds==1 && Site->Crop->bPlanted && Site->Crop->GrowthWork==2.5f &&
        Site->StoredFood==0 && Hero->Inventory->Count(EFrontierItem::Seeds)==1 && Pet->Experience==0;
}
}
void RunCropScenario(UWorld* World,ACFPlayerCharacter* Hero,int32 Mode)
{
    using namespace CFCropQA; bool Pass=true;
    if(Mode==3)
    {
        const FString Slot=TEXT("FrontierFarmingAutomation");
        auto* Old=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0));Pass=Old && Old->FormatVersion<24;
        for(int32 Attempt=0;Attempt<2;++Attempt)
        {
            Pass &= Hero->LoadProgress(Slot);auto* Site=Garden(World);auto* Pet=Farmer(World,Hero);
            Pass &= Site && Pet && Pet->WorkSite==Site && Site->StoredFood==1 && Site->Crop->Seeds==0 && !Site->Crop->bPlanted &&
                Site->Crop->GrowthWork==0 && Hero->Inventory->Count(EFrontierItem::Seeds)==0;
            if(Pet && Site){Pet->SetActorTickEnabled(false);Pet->Tick(.1f);Pass &= Site->StoredFood==1 && !Site->Crop->bPlanted;}
        }
        UE_LOG(LogTemp,Display,TEXT("CF_CROP_LEGACY_READ %s"),Pass?TEXT("PASS"):TEXT("FAIL"));FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return;
    }
    if(Mode==2) Pass=Hero->LoadProgress(TEXT("CropCross")) && Snapshot(World,Hero) && Hero->LoadProgress(TEXT("CropCross")) && Snapshot(World,Hero);
    else
    {
        UCreatureDefinition* Leaf=nullptr;
        for(TActorIterator<ACreatureCharacter> It(World);It;++It){if(It->Definition && It->Definition->Form==ECreatureForm::Leaf) Leaf=It->Definition;It->Destroy();}
        Hero->GetCharacterMovement()->DisableMovement();Hero->SetActorLocation(FVector(-450,0,90));
        Hero->Inventory->Restore({{EFrontierItem::Material,100},{EFrontierItem::Fiber,1},{EFrontierItem::Food,1}});
        Pass=Leaf && Hero->CraftRecipe(TEXT("PlantingSeeds")) && Hero->Inventory->Count(EFrontierItem::Seeds)==3 && Hero->Inventory->Count(EFrontierItem::Food)==0;
        Hero->Building->Selected=EWorkSpecialty::Farming;
        auto* Site=Hero->Building->BuildAt(FTransform(FRotator::ZeroRotator,FVector(-400,-700,-15),FVector(2,1,.7)));
        if(!Site || !Leaf){UE_LOG(LogTemp,Error,TEXT("CF_CROP FAIL fixture"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
        Hero->SetActorLocation(FVector(-400,-930,40));Hero->FirstPersonCamera->SetWorldRotation((Site->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());
        Hero->DepositGardenSeed();Hero->DepositGardenSeed();Hero->InteractGarden();Hero->InteractGarden();
        Pass &= Site->Crop->Seeds==1 && Hero->Inventory->Count(EFrontierItem::Seeds)==1 && Site->Crop->bPlanted && Site->Crop->GrowthWork==1;
        Hero->InteractGarden();Pass &= Site->Crop->GrowthWork==1;
        Hero->Health=0;Hero->InteractGarden();Pass &= Site->Crop->GrowthWork==1;Hero->Health=100;
        UGameplayStatics::SetGamePaused(World,true);Hero->InteractGarden();Pass &= Site->Crop->GrowthWork==1;UGameplayStatics::SetGamePaused(World,false);
        const FTransform At(FVector(-400,-900,40));auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),At,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Pet->Definition=Leaf;Pet->FinishSpawning(At);Pet->Tame(Hero->GetController());Pet->WorkSite=Site;Pet->SetActivity(ECreatureActivity::Work);
        Pet->SetActorTickEnabled(false);Pet->GetCharacterMovement()->DisableMovement();
        Site->Crop->Work(Pet,Hero,1.5f);
        UE_LOG(LogTemp,Display,TEXT("CF_CROP_STAGE manual pass=%d seeds=%d held=%d growth=%.2f planted=%d feedback=%s"),Pass,Site->Crop->Seeds,Hero->Inventory->Count(EFrontierItem::Seeds),Site->Crop->GrowthWork,Site->Crop->bPlanted,*Hero->Feedback);
        Pass &= Snapshot(World,Hero) && !Hero->Building->DismantleStation(Site) && Hero->SaveProgress(TEXT("CropCross"));
        if(Mode==0) Pass &= Hero->LoadProgress(TEXT("CropCross")) && Snapshot(World,Hero) && Hero->LoadProgress(TEXT("CropCross")) && Snapshot(World,Hero);
    }
    if(Mode!=1)
    {
        UE_LOG(LogTemp,Display,TEXT("CF_CROP_STAGE snapshot pass=%d"),Pass);
        auto* Site=Garden(World);auto* Pet=Farmer(World,Hero);
        if(!Site || !Pet){UE_LOG(LogTemp,Error,TEXT("CF_CROP FAIL restore"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
        Pet->SetActorTickEnabled(false);Pet->GetCharacterMovement()->DisableMovement();
        for(int32 Case=0;Case<8;++Case)
        {
            auto* Bad=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("CropCross"),0));
            auto* R=Bad->Facilities.FindByPredicate([](const FFacilitySaveRecord& V){return V.Specialty==EWorkSpecialty::Farming;});
            if(!R){Pass=false;break;}
            if(Case==0) R->Seeds=-1;
            if(Case==1) R->Seeds=31;
            if(Case==2) R->GrowthWork=7;
            if(Case==3) R->GrowthWork=-1;
            if(Case==4) R->bPlanted=false;
            if(Case==5) R->Specialty=EWorkSpecialty::Gathering;
            if(Case==6) {const auto Copy=*R;Bad->Facilities.Add(Copy);}
            if(Case==7) Bad->FormatVersion=23;
            Pass &= UGameplayStatics::SaveGameToSlot(Bad,TEXT("CropBad"),0) && !Hero->LoadProgress(TEXT("CropBad")) && Garden(World)==Site && Snapshot(World,Hero);
        }
        Pet->Tick(2);Pass &= Site->StoredFood==1 && Pet->Experience==1 && !Site->Crop->bPlanted;
        UE_LOG(LogTemp,Display,TEXT("CF_CROP_STAGE first pass=%d food=%d xp=%d growth=%.2f"),Pass,Site->StoredFood,Pet->Experience,Site->Crop->GrowthWork);
        Pet->Tick(3.4f);Pass &= Site->StoredFood==2 && Pet->Experience==2 && Site->Crop->Seeds==0;
        Pet->Tick(3.4f);Pass &= Site->StoredFood==2 && Pet->Experience==2 && !Site->Crop->bPlanted;
        Site->StoredFood=30;Site->Crop->Seeds=1;Pet->Tick(3.4f);
        Pass &= Site->StoredFood==30 && Site->Crop->Stage()==3 && Site->Crop->Seeds==0 && Pet->Experience==2 && Hero->SaveProgress(TEXT("CropRipe")) && Hero->LoadProgress(TEXT("CropRipe"));
        Site=Garden(World);Pet=Farmer(World,Hero);Pet->SetActorTickEnabled(false);Pet->GetCharacterMovement()->DisableMovement();
        Site->AdjustFoodStock(-1);Pet->Tick(.1f);Pass &= Site->StoredFood==30 && !Site->Crop->bPlanted && Pet->Experience==3;
        Pass &= Hero->LoadProgress(TEXT("CropCross"));Site=Garden(World);
        UE_LOG(LogTemp,Display,TEXT("CF_CROP_STAGE ripe pass=%d"),Pass);
        Hero->SetActorLocation(Site->GetActorLocation()+FVector(-250,0,55)); // Clear the farmer's body; keep the real visibility gate.
        Hero->FirstPersonCamera->SetWorldRotation((Site->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());
        Hero->Inventory->Restore({{EFrontierItem::Seeds,999}});Hero->WithdrawGardenSeed();Pass &= Site->Crop->Seeds==1;
        Hero->Inventory->Consume(EFrontierItem::Seeds,1);Hero->WithdrawGardenSeed();Pass &= Site->Crop->Seeds==0 && Hero->Inventory->Count(EFrontierItem::Seeds)==999;
        UE_LOG(LogTemp,Display,TEXT("CF_CROP_STAGE withdraw pass=%d reserve=%d held=%d feedback=%s"),Pass,Site->Crop->Seeds,Hero->Inventory->Count(EFrontierItem::Seeds),*Hero->Feedback);
        auto* Legacy=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("CropCross"),0));Legacy->FormatVersion=23;Legacy->InventoryStacks.Remove(EFrontierItem::Seeds);
        Pass &= UGameplayStatics::SaveGameToSlot(Legacy,TEXT("CropLegacy23"),0) && Hero->LoadProgress(TEXT("CropLegacy23"));Site=Garden(World);Pet=Farmer(World,Hero);
        Pass &= Site && Site->StoredFood==0 && Site->Crop->Seeds==0 && !Site->Crop->bPlanted && Site->Crop->GrowthWork==0 && Hero->Inventory->Count(EFrontierItem::Seeds)==0;
        if(Site && Pet){Site->Crop->Work(Pet,Hero,100);Pass &= Site->StoredFood==0 && !Site->Crop->bPlanted;}
        UE_LOG(LogTemp,Display,TEXT("CF_CROP_STAGE legacy pass=%d"),Pass);
    }
    UE_LOG(LogTemp,Display,TEXT("CF_CROP%s %s"),Mode==1?TEXT("_WRITE"):Mode==2?TEXT("_READ"):TEXT(""),Pass?TEXT("PASS"):TEXT("FAIL"));
    if(Pass && Mode==0 && FParse::Param(FCommandLine::Get(),TEXT("CFCropPreview")))
    {
        if(!Hero->LoadProgress(TEXT("CropCross"))){FPlatformMisc::RequestExitWithStatus(false,1);return;}
        for(TActorIterator<ACreatureCharacter> It(World);It;++It) It->SetActorTickEnabled(false);
        auto* Camera=World->SpawnActor<ACameraActor>();auto* Site=Garden(World);
        Camera->SetActorLocation(Site->GetActorLocation()+FVector(-400,-450,320));
        Camera->SetActorRotation((Site->GetActorLocation()+FVector(0,0,60)-Camera->GetActorLocation()).Rotation());Camera->GetCameraComponent()->FieldOfView=65;
        CastChecked<APlayerController>(Hero->GetController())->SetViewTarget(Camera);
        const FString Root=FPaths::ProjectSavedDir()/TEXT("CropPreview");IFileManager::Get().MakeDirectory(*Root,true);
        FTimerHandle Growing,Ripe,Shot,Exit;
        World->GetTimerManager().SetTimer(Growing,[Root](){FScreenshotRequest::RequestScreenshot(Root/TEXT("Growing.png"),false,false);},1.f,false);
        World->GetTimerManager().SetTimer(Ripe,[World,Hero](){Hero->LoadProgress(TEXT("CropRipe"));for(TActorIterator<ACreatureCharacter> It(World);It;++It) It->SetActorTickEnabled(false);},2.f,false);
        World->GetTimerManager().SetTimer(Shot,[Root](){FScreenshotRequest::RequestScreenshot(Root/TEXT("Ripe.png"),false,false);},3.f,false);
        World->GetTimerManager().SetTimer(Exit,[Root](){const bool Ready=IFileManager::Get().FileSize(*(Root/TEXT("Growing.png")))>1000 && IFileManager::Get().FileSize(*(Root/TEXT("Ripe.png")))>1000;
            UE_LOG(LogTemp,Display,TEXT("CF_CROP_PREVIEW %s"),Ready?TEXT("PASS"):TEXT("FAIL"));FPlatformMisc::RequestExitWithStatus(false,Ready?0:1);},4.f,false);
        return;
    }
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}
