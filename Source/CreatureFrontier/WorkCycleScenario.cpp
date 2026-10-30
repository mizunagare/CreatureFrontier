#include "WorkCycleScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "WorkStation.h"
#include "FarmCropComponent.h"
#include "ResourceNode.h"
#include "BaseBuildingComponent.h"
#include "InventoryComponent.h"
#include "FrontierSaveGame.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Camera/CameraComponent.h"
#include "InputRoutingQA.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

void RunWorkCycleScenario(UWorld* World, ACFPlayerCharacter* Hero)
{
    if(FParse::Param(FCommandLine::Get(),TEXT("CFWorkAimTest")))
    {
        Hero->GetCharacterMovement()->DisableMovement();Hero->SetActorLocation(FVector(-400,-1250,40));
        Hero->Inventory->Restore({{EFrontierItem::Material,100}});
        auto Build=[&](EWorkSpecialty Kind,FVector Position){Hero->Building->Selected=Kind;return Hero->Building->BuildAt(FTransform(FRotator::ZeroRotator,Position,FVector(2,1,.7)));};
        auto* Near=Build(EWorkSpecialty::Gathering,FVector(-100,-1100,-15));
        auto* Far=Build(EWorkSpecialty::Farming,FVector(-400,-700,-15));
        auto* Trough=Build(EWorkSpecialty::Feeding,FVector(-700,-1000,-15));
        UCreatureDefinition* Leaf=nullptr;
        for(TActorIterator<ACreatureCharacter> It(World);It;++It){if(It->Definition && It->Definition->Form==ECreatureForm::Leaf)Leaf=It->Definition;It->Destroy();}
        if(!Near || !Far || !Trough || !Leaf){UE_LOG(LogTemp,Error,TEXT("CF_WORK_AIM FAIL fixture %s"),*Hero->Feedback);FPlatformMisc::RequestExitWithStatus(false,1);return;}
        const FTransform At(FVector(-550,-1200,40));auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),At,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Pet->Definition=Leaf;Pet->FinishSpawning(At);Pet->Tame(Hero->GetController());
        auto Aim=[Hero](FVector Target){const auto View=(Target-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);};
        Aim(Far->GetActorLocation()+FVector(0,0,20));
        const bool Target=CFInputQA::Press(Hero,EKeys::E) && Pet->WorkSite==Far && Pet->Activity==ECreatureActivity::Work;
        Aim(Hero->FirstPersonCamera->GetComponentLocation()+FVector(0,0,500));
        const bool Fallback=CFInputQA::Press(Hero,EKeys::E) && Pet->WorkSite==Near;
        Hero->CommandFollow();Aim(Trough->GetActorLocation()+FVector(0,0,20));
        const bool Feeding=CFInputQA::Press(Hero,EKeys::E) && !Pet->WorkSite && Pet->Activity==ECreatureActivity::Follow;
        const bool Pass=Target && Fallback && Feeding;
        UE_LOG(LogTemp,Display,TEXT("CF_WORK_AIM %s aimedFar=%d nearestFallback=%d feedingPreserved=%d noSave=1"),Pass?TEXT("PASS"):TEXT("FAIL"),Target,Fallback,Feeding);
        if(Pass && FParse::Param(FCommandLine::Get(),TEXT("CFWorkAimPreview")))
        {
            const FString Root=FPaths::ProjectSavedDir()/TEXT("WorkAimPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits);
            IFileManager::Get().MakeDirectory(*Root,true);UE_LOG(LogTemp,Display,TEXT("CF_WORK_AIM_PREVIEW_ROOT %s"),*Root);
            Aim(Far->GetActorLocation()+FVector(0,0,20));CFInputQA::Press(Hero,EKeys::E);
            FTimerHandle A,B,C,D,E,Done;
            World->GetTimerManager().SetTimer(A,[Root](){FScreenshotRequest::RequestScreenshot(Root/TEXT("Aimed.png"),false,false);},.7f,false);
            World->GetTimerManager().SetTimer(B,[Hero,Aim](){Aim(Hero->FirstPersonCamera->GetComponentLocation()+FVector(0,0,500));CFInputQA::Press(Hero,EKeys::E);},1.2f,false);
            World->GetTimerManager().SetTimer(C,[Root](){FScreenshotRequest::RequestScreenshot(Root/TEXT("Fallback.png"),false,false);},1.7f,false);
            World->GetTimerManager().SetTimer(D,[Hero,Trough,Aim](){Hero->CommandFollow();Aim(Trough->GetActorLocation()+FVector(0,0,20));CFInputQA::Press(Hero,EKeys::E);},2.2f,false);
            World->GetTimerManager().SetTimer(E,[Root](){FScreenshotRequest::RequestScreenshot(Root/TEXT("Feeding.png"),false,false);},2.7f,false);
            World->GetTimerManager().SetTimer(Done,[Root](){bool Ready=true;for(const TCHAR* Scene:{TEXT("Aimed"),TEXT("Fallback"),TEXT("Feeding")})Ready &= IFileManager::Get().FileSize(*(Root/(FString(Scene)+TEXT(".png"))))>1000;
                UE_LOG(LogTemp,Display,TEXT("CF_WORK_AIM_PREVIEW %s"),Ready?TEXT("PASS"):TEXT("FAIL"));FPlatformMisc::RequestExitWithStatus(false,Ready?0:1);},3.5f,false);
            return;
        }
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return;
    }
    UCreatureDefinition* Leaf = nullptr;
    for (TActorIterator<ACreatureCharacter> It(World); It; ++It)
    { if (It->Definition && It->Definition->Form == ECreatureForm::Leaf) Leaf = It->Definition; It->Destroy(); }
    if (!Leaf) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Hero->SetActorLocation(FVector(-350,-1050,40)); Hero->GetCharacterMovement()->DisableMovement();
    Hero->Inventory->Restore({{EFrontierItem::Material,100}});
    auto At = [](float X) { return FTransform(FRotator::ZeroRotator,FVector(X,-700,-15),FVector(2,1,.7)); };
    Hero->Building->Selected = EWorkSpecialty::Farming;
    auto* Garden = Hero->Building->BuildAt(At(0));
    if(Garden) Garden->Crop->Seeds=10; // 恢复已有种子的农业工作，而非免费生产。
    if (!Garden) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    const FGuid Id = Garden->PersistentId;
    const FTransform Transform(FRotator::ZeroRotator,FVector(0,-900,40));
    auto* Pet = World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    Pet->Definition = Leaf; Pet->FinishSpawning(Transform); Pet->Tame(Hero->GetController());
    Pet->SetActorTickEnabled(false); Pet->GetCharacterMovement()->DisableMovement(); Pet->WorkSite = Garden;
    Pet->Nutrition = 0; Pet->Stamina = 100; Pet->SetActivity(ECreatureActivity::Work); Pet->Tick(0);
    bool Hunger = Pet->bResumeWorkAfterRest && Pet->Activity == ECreatureActivity::Rest;
    Pet->Nutrition = 19; Pet->Tick(0); Hunger &= Pet->Activity == ECreatureActivity::Rest;
    Pet->Nutrition = 20; Pet->Tick(0); Hunger &= Pet->Activity == ECreatureActivity::Work && !Pet->bResumeWorkAfterRest;
    Pet->Nutrition = 60; Pet->Stamina=20; Pet->bResumeWorkAfterRest = true; Pet->SetActivity(ECreatureActivity::Rest);
    Hero->CommandRest(); Pet->Tick(1);
    bool Manual = Pet->bResumeWorkAfterRest && Pet->Activity == ECreatureActivity::Rest && Pet->WorkSite==Garden && FMath::IsNearlyEqual(Pet->Stamina,32.f,.01f);
    Pet->Stamina=80;Pet->Tick(0);Manual &= Pet->Activity==ECreatureActivity::Work && !Pet->bResumeWorkAfterRest && Pet->WorkSite==Garden;
    UGameplayStatics::SetGamePaused(World,true);Hero->CommandRest();
    Manual &= Pet->Activity==ECreatureActivity::Work && Pet->WorkSite==Garden && !Pet->bResumeWorkAfterRest;
    UGameplayStatics::SetGamePaused(World,false);
    // 没有原任务时仍是普通基地休息，不凭空创建工作或恢复跟随前已取消的任务。
    Hero->CommandFollow();Hero->CommandRest();Pet->Tick(1);
    Manual &= !Pet->bResumeWorkAfterRest && Pet->Activity==ECreatureActivity::Rest;
    Pet->bResumeWorkAfterRest = true; Hero->CommandFollow();
    Manual &= !Pet->bResumeWorkAfterRest && !Pet->WorkSite && Pet->Activity == ECreatureActivity::Follow;
    AResourceNode* Field=nullptr;for(TActorIterator<AResourceNode> It(World);It;++It){Field=*It;break;}
    Manual &= Field && Pet->AssignFieldResource(Field);
    Hero->CommandRest();Manual &= Pet->Activity==ECreatureActivity::Rest && Pet->bResumeWorkAfterRest && Pet->GetFieldResource()==Field && !Pet->WorkSite;
    Pet->Stamina=80;Pet->Tick(0);Manual &= Pet->Activity==ECreatureActivity::Work && Pet->GetFieldResource()==Field;
    Hero->CommandFollow();Manual &= !Pet->GetFieldResource() && !Pet->bResumeWorkAfterRest;
    // 原运输设施和已携食物也必须保留，不能因主动休息丢失货物。
    Garden->RequiredSpecialty=EWorkSpecialty::Transport;Pet->WorkSite=Garden;Pet->CarriedFood=2;Pet->SetActivity(ECreatureActivity::Work);
    Hero->CommandRest();Manual &= Pet->Activity==ECreatureActivity::Rest && Pet->bResumeWorkAfterRest && Pet->WorkSite==Garden && Pet->CarriedFood==2;
    Hero->CommandFollow();Pet->CarriedFood=0;Garden->RequiredSpecialty=EWorkSpecialty::Farming;
    Pet->WorkSite = Garden; Pet->SetActivity(ECreatureActivity::Rest); Pet->bResumeWorkAfterRest = true; Pet->Stamina = 20;
    auto* Enemy = World->SpawnActor<ACreatureCharacter>(FVector(200,-1200,40),FRotator::ZeroRotator);
    Enemy->SetActorTickEnabled(false);
    Manual &= Pet->OrderAttack(Enemy) && !Pet->bResumeWorkAfterRest && !Pet->WorkSite && Pet->Activity == ECreatureActivity::Combat;
    Pet->WorkSite = Garden; Pet->SetActivity(ECreatureActivity::Rest); Pet->bResumeWorkAfterRest = true;
    UGameplayStatics::ApplyDamage(Pet,1000,Enemy->GetController(),Enemy,nullptr);
    const bool Downed = Pet->Health == 0 && !Pet->bResumeWorkAfterRest;
    Enemy->Destroy(); Pet->Health = Pet->GetMaxHealth();
    Hero->Building->Selected = EWorkSpecialty::Gathering; auto* Temporary = Hero->Building->BuildAt(At(-300));
    if (!Temporary) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Pet->SetActorLocation(FVector(-300,-900,40)); Pet->WorkSite = Temporary; Pet->SetActivity(ECreatureActivity::Work); Pet->Stamina = .01f; Pet->Tick(.1f);
    bool Removed = Pet->bResumeWorkAfterRest && Hero->Building->DismantleStation(Temporary) && !Pet->bResumeWorkAfterRest && !Pet->WorkSite;
    Pet->SetActivity(ECreatureActivity::Rest); Pet->bResumeWorkAfterRest = true; Pet->Tick(0);
    Removed &= !Pet->bResumeWorkAfterRest && Pet->Activity == ECreatureActivity::Follow;
    Pet->SetActorLocation(Transform.GetLocation()); Pet->WorkSite = Garden;
    Pet->Health = Pet->GetMaxHealth()*.5f; Pet->Nutrition = 60; Pet->Stamina = .01f;
    Pet->SetActivity(ECreatureActivity::Work); Pet->Tick(.1f);
    const bool Exhausted = Pet->Activity == ECreatureActivity::Rest && Pet->bResumeWorkAfterRest && Pet->Stamina == 0;
    const FString Slot = TEXT("FrontierWorkCycleAutomation");
    bool Saved = Hero->SaveProgress(Slot) && Hero->LoadProgress(Slot);
    Pet = nullptr;
    for (TActorIterator<ACreatureCharacter> It(World); It; ++It) if (It->Tamer == Hero->GetController()) Pet = *It;
    Saved &= Pet && Pet->bResumeWorkAfterRest && Pet->WorkSite && Pet->WorkSite->PersistentId == Id && Pet->Activity == ECreatureActivity::Rest;
    if (!Pet) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    // 读回暂停的工作后，必须通过真实游戏更新完成恢复、续工与生产。
    Pet->GetCharacterMovement()->DisableMovement();
    FTimerHandle Complete;
    World->GetTimerManager().SetTimer(Complete,[World,Hero,Pet,Slot,Hunger,Manual,Downed,Removed,Exhausted,Saved]()
    {
        const bool Cycle = !Pet->bResumeWorkAfterRest && Pet->Activity == ECreatureActivity::Work && Pet->Stamina > 60 &&
            Pet->Health >= Pet->GetMaxHealth()*.8f && Pet->WorkSite && Pet->WorkSite->StoredFood>0;
        auto* Legacy = Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0));
        bool Migration = Legacy != nullptr;
        if (Legacy)
        {
            Legacy->FormatVersion = 10; for (auto& Record : Legacy->Creatures) Record.bResumeWorkAfterRest = false;
            for(auto& Facility:Legacy->Facilities) if(Facility.Specialty==EWorkSpecialty::Farming) Facility.StoredFood=0;
            Migration &= UGameplayStatics::SaveGameToSlot(Legacy,TEXT("FrontierLegacyWorkCycleAutomation"),0) && Hero->LoadProgress(TEXT("FrontierLegacyWorkCycleAutomation"));
            for (TActorIterator<ACreatureCharacter> It(World); It; ++It) if (It->Tamer == Hero->GetController())
            { It->Stamina = 100; It->Health = It->GetMaxHealth(); It->Nutrition = 100; It->Tick(0); Migration &= !It->bResumeWorkAfterRest && It->Activity == ECreatureActivity::Rest; }
        }
        const bool Pass = Hunger && Manual && Downed && Removed && Exhausted && Saved && Cycle && Migration;
        UE_LOG(LogTemp,Display,TEXT("CF_WORK_CYCLE %s hunger=%d manual=%d downed=%d removed=%d exhausted=%d save=%d cycle=%d migration=%d"),
            Pass ? TEXT("PASS") : TEXT("FAIL"),Hunger,Manual,Downed,Removed,Exhausted,Saved,Cycle,Migration);
        FPlatformMisc::RequestExitWithStatus(false,Pass ? 0 : 1);
    },11.0f,false);
}
