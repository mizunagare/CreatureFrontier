#include "BefriendingScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "FrontierSaveGame.h"
#include "InputRoutingQA.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

void RunCompanionStanceScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)It->SetActorTickEnabled(false);
    auto* Definition=LoadObject<UCreatureDefinition>(nullptr,TEXT("/Game/Creatures/DA_Craglet.DA_Craglet"));
    if(!Definition){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    const FVector Origin(-450,-1100,40);Hero->SetActorLocation(Origin);
    auto Spawn=[&](FVector Offset,bool Owned)
    {
        const FTransform At(Origin+Offset);auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),At);
        Pet->Definition=Definition;Pet->FinishSpawning(At);if(Owned)Pet->Tame(Hero->GetController());Pet->SetActorTickEnabled(false);return Pet;
    };
    auto* Ally=Spawn(FVector(80,90,0),true);auto* Other=Spawn(FVector(-100,180,0),true);
    auto* Enemy=Spawn(FVector(180,0,0),false);Enemy->Disposition=ECreatureDisposition::Hostile;
    const FGuid Id=Ally->PersistentId;
    const bool Defaults=!Ally->bAvoidCombat && !Other->bAvoidCombat && CFInputQA::UniqueBindings(Hero);
    Hero->ClearCompanionSelection();
    bool Passive=CFInputQA::Press(Hero,EKeys::F8) && Ally->bAvoidCombat && Other->bAvoidCombat;
    const float EnemyHP=Enemy->Health;
    for(int32 I=0;I<20;++I)Ally->Tick(.1f);
    Passive &= Enemy->Health==EnemyHP && !Ally->FocusTarget && Ally->Activity==ECreatureActivity::Follow;
    const auto View=(Enemy->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
    Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
    const bool Ordered=CFInputQA::Press(Hero,EKeys::Q) && Ally->FocusTarget==Enemy;
    Ally->Tick(.1f);
    const bool Explicit=Ordered && Enemy->Health<EnemyHP && Ally->bAvoidCombat;
    const bool Cancel=Ally->SetAvoidCombat(true) && !Ally->FocusTarget && Ally->SpecialWindupRemaining==0;
    const bool Saved=Hero->SaveProgress(TEXT("StanceGood"));
    auto* Snapshot=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("StanceGood"),0));
    bool Invalid=Snapshot!=nullptr;
    if(Snapshot)
    {
        for(auto& Record:Snapshot->Creatures)if(Record.Id==Id)Record.CombatStance=255;
        Invalid &= UGameplayStatics::SaveGameToSlot(Snapshot,TEXT("StanceBad"),0) && !Hero->LoadProgress(TEXT("StanceBad")) && IsValid(Ally) && Ally->bAvoidCombat;
    }
    const bool Loaded=Saved && Hero->LoadProgress(TEXT("StanceGood"));
    auto Find=[&](){for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->PersistentId==Id)return *It;return static_cast<ACreatureCharacter*>(nullptr);};
    auto* Restored=Find();const bool Persist=Loaded && Restored && Restored->bAvoidCombat;
    bool Legacy=false;
    if(Snapshot)
    {
        Snapshot->FormatVersion=30;
        Legacy=UGameplayStatics::SaveGameToSlot(Snapshot,TEXT("StanceLegacy30"),0) && Hero->LoadProgress(TEXT("StanceLegacy30"));
        Restored=Find();Legacy &= Restored && !Restored->bAvoidCombat;
    }
    const bool Pass=Defaults && Passive && Explicit && Cancel && Invalid && Persist && Legacy;
    UE_LOG(LogTemp,Display,TEXT("CF_STANCE %s defaults=%d passive=%d explicit=%d cancel=%d invalid=%d persist=%d legacy=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Defaults,Passive,Explicit,Cancel,Invalid,Persist,Legacy);
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}
