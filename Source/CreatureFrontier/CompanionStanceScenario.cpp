#include "BefriendingScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "FrontierSaveGame.h"
#include "InputRoutingQA.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "WorkStation.h"

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
    // 单选切回护主必须实际恢复自动攻击，不能只检查布尔状态。
    const bool Selected=Hero->SelectCompanion(Ally);
    bool Scope=Selected && CFInputQA::Press(Hero,EKeys::F8) && !Ally->bAvoidCombat && Other->bAvoidCombat;
    Ally->Tick(.1f);
    const bool Guard=Enemy->Health<EnemyHP;
    Scope &= CFInputQA::Press(Hero,EKeys::F8) && Ally->bAvoidCombat && Other->bAvoidCombat;
    auto* Far=Spawn(FVector(2500,180,0),true);auto* Down=Spawn(FVector(-200,180,0),true);Down->Health=0;
    Hero->ClearCompanionSelection();
    bool Bounds=CFInputQA::Press(Hero,EKeys::F8) && !Ally->bAvoidCombat && !Other->bAvoidCombat && !Far->bAvoidCombat && !Down->bAvoidCombat;
    Bounds &= !Enemy->SetAvoidCombat(true) && !Down->SetAvoidCombat(true);
    Bounds &= CFInputQA::Press(Hero,EKeys::F8) && Ally->bAvoidCombat && Other->bAvoidCombat && !Far->bAvoidCombat && !Down->bAvoidCombat;
    const float HeroHP=Hero->Health;Hero->Health=0;
    Bounds &= CFInputQA::Press(Hero,EKeys::F8) && Ally->bAvoidCombat && Other->bAvoidCombat;Hero->Health=HeroHP;
    AWorkStation* Site=nullptr;for(TActorIterator<AWorkStation> It(World);It;++It){Site=*It;break;}
    bool Work=Site!=nullptr;
    if(Site)
    {
        Other->WorkSite=Site;Other->SetActivity(ECreatureActivity::Work);
        Work &= CFInputQA::Press(Hero,EKeys::F8) && Other->WorkSite==Site && Other->Activity==ECreatureActivity::Work;
        Work &= CFInputQA::Press(Hero,EKeys::F8) && Other->WorkSite==Site && Other->Activity==ECreatureActivity::Work;
        Other->WorkSite=nullptr;Other->SetActivity(ECreatureActivity::Follow);
    }
    // 手动推进冷却，不借此声称完成真实时间/移动验收。
    for(int32 I=0;I<20;++I)Ally->Tick(.1f);
    const float BeforeExplicit=Enemy->Health;
    const auto View=(Enemy->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
    Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
    const bool Ordered=CFInputQA::Press(Hero,EKeys::Q) && Ally->FocusTarget==Enemy;
    Ally->Tick(.1f);
    const bool Explicit=Ordered && Enemy->Health<BeforeExplicit && Ally->bAvoidCombat;
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
    const bool Pass=Defaults && Passive && Explicit && Cancel && Invalid && Persist && Legacy && Scope && Guard && Bounds && Work;
    UE_LOG(LogTemp,Display,TEXT("CF_STANCE_BOUNDARIES %s scope=%d guard=%d bounds=%d work=%d"),Scope && Guard && Bounds && Work?TEXT("PASS"):TEXT("FAIL"),Scope,Guard,Bounds,Work);
    UE_LOG(LogTemp,Display,TEXT("CF_STANCE %s defaults=%d passive=%d explicit=%d cancel=%d invalid=%d persist=%d legacy=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Defaults,Passive,Explicit,Cancel,Invalid,Persist,Legacy);
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}
