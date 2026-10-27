#include "WorkCycleScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "WorkStation.h"
#include "BaseBuildingComponent.h"
#include "InventoryComponent.h"
#include "FrontierSaveGame.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "HAL/PlatformMisc.h"

void RunWorkCycleScenario(UWorld* World, ACFPlayerCharacter* Hero)
{
    UCreatureDefinition* Leaf = nullptr;
    for (TActorIterator<ACreatureCharacter> It(World); It; ++It)
    { if (It->Definition && It->Definition->Form == ECreatureForm::Leaf) Leaf = It->Definition; It->Destroy(); }
    if (!Leaf) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Hero->SetActorLocation(FVector(-350,-1050,40)); Hero->GetCharacterMovement()->DisableMovement();
    Hero->Inventory->Restore({{EFrontierItem::Material,100}});
    auto At = [](float X) { return FTransform(FRotator::ZeroRotator,FVector(X,-700,-15),FVector(2,1,.7)); };
    Hero->Building->Selected = EWorkSpecialty::Farming;
    auto* Garden = Hero->Building->BuildAt(At(0));
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
    Pet->Nutrition = 60; Pet->bResumeWorkAfterRest = true; Pet->SetActivity(ECreatureActivity::Rest);
    Hero->CommandRest(); Pet->Tick(1);
    bool Manual = !Pet->bResumeWorkAfterRest && Pet->Activity == ECreatureActivity::Rest;
    Pet->bResumeWorkAfterRest = true; Hero->CommandFollow();
    Manual &= !Pet->bResumeWorkAfterRest && !Pet->WorkSite && Pet->Activity == ECreatureActivity::Follow;
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
    // Actual game ticking must recover, resume, and produce after loading a paused job.
    Pet->GetCharacterMovement()->DisableMovement();
    FTimerHandle Complete;
    World->GetTimerManager().SetTimer(Complete,[World,Hero,Pet,Slot,Hunger,Manual,Downed,Removed,Exhausted,Saved]()
    {
        const bool Cycle = !Pet->bResumeWorkAfterRest && Pet->Activity == ECreatureActivity::Work && Pet->Stamina > 60 &&
            Pet->Health >= Pet->GetMaxHealth()*.8f && Hero->Inventory->Count(EFrontierItem::Food) > 0;
        auto* Legacy = Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0));
        bool Migration = Legacy != nullptr;
        if (Legacy)
        {
            Legacy->FormatVersion = 10; for (auto& Record : Legacy->Creatures) Record.bResumeWorkAfterRest = false;
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
