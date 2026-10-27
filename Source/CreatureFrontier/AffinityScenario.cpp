#include "AffinityScenario.h"
#include "CreatureAffinity.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "CFPlayerCharacter.h"
#include "PlayerEquipmentComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameViewportClient.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

void RunAffinityScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    TMap<ECreatureForm,UCreatureDefinition*> Definitions;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It) { if(It->Definition) Definitions.Add(It->Definition->Form,It->Definition); It->Destroy(); }
    if(Definitions.Num()!=3) { UE_LOG(LogTemp,Error,TEXT("CF_AFFINITY FAIL definitions")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Hero->SetActorLocation(FVector(-1500,-1800,40)); Hero->GetCharacterMovement()->DisableMovement(); Hero->Equipment->Restore({});
    auto Spawn=[World,Hero](UCreatureDefinition* Definition,FVector Position,bool Owned=false)
    {
        const FTransform Transform(Position);
        auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Pet->Definition=Definition; Pet->FinishSpawning(Transform); Pet->SetActorTickEnabled(false); Pet->GetCharacterMovement()->DisableMovement();
        if(Owned) Pet->Tame(Hero->GetController()); return Pet;
    };
    auto Damage=[](ACreatureCharacter* Source,AActor* Target,float Amount) { return UGameplayStatics::ApplyDamage(Target,Amount,Source?Source->GetController():nullptr,Source,nullptr); };
    TArray<ACreatureCharacter*> Sources,Targets;
    for(int32 Index=0;Index<3;++Index) { Sources.Add(Spawn(Definitions.FindRef(static_cast<ECreatureForm>(Index)),FVector(1400,-1600+Index*200,40))); Targets.Add(Spawn(Definitions.FindRef(static_cast<ECreatureForm>(Index)),FVector(1700,-1600+Index*200,40))); }
    const float Expected[3][3]={{1,.75f,1.5f},{1.5f,1,.75f},{.75f,1.5f,1}};
    bool Matrix=true,Guard=true;
    for(int32 A=0;A<3;++A) for(int32 D=0;D<3;++D)
    {
        auto* Victim=Targets[D]; Victim->Health=Victim->GetMaxHealth(); Victim->GuardRemaining=0;
        const float Before=Victim->Health; const float Applied=Damage(Sources[A],Victim,20);
        Matrix &= CreatureAffinity::Multiplier(Sources[A]->Definition,Victim->Definition)==Expected[A][D] && FMath::IsNearlyEqual(Applied,20*Expected[A][D],.001f) && FMath::IsNearlyEqual(Before-Victim->Health,Applied,.001f);
        Victim->Health=Victim->GetMaxHealth(); Victim->GuardRemaining=5;
        Guard &= FMath::IsNearlyEqual(Damage(Sources[A],Victim,20),10*Expected[A][D],.001f);
    }
    auto* Untyped=Spawn(nullptr,FVector(1800,-1000,40)); auto* Leaf=Targets[0]; Leaf->Health=Leaf->GetMaxHealth(); Leaf->GuardRemaining=0;
    bool Neutral=Damage(Untyped,Leaf,20)==20 && CreatureAffinity::Multiplier(nullptr,Leaf->Definition)==1 && CreatureAffinity::Multiplier(Leaf->Definition,nullptr)==1;
    Leaf->Health=Leaf->GetMaxHealth(); Neutral &= UGameplayStatics::ApplyDamage(Leaf,20,Hero->GetController(),Hero,nullptr)==20;
    Leaf->Health=Leaf->GetMaxHealth(); Neutral &= Damage(nullptr,Leaf,20)==20;
    Hero->Health=100; Neutral &= Damage(Sources[1],Hero,20)==20 && Hero->Health==80; Hero->Health=100;
    Leaf->SetActivity(ECreatureActivity::ReturnHome); const float SafeHealth=Leaf->Health;
    bool Safety=Damage(Sources[1],Leaf,20)==0 && Leaf->Health==SafeHealth; Leaf->SetActivity(ECreatureActivity::Idle); Leaf->Health=0; Safety &= Damage(Sources[1],Leaf,20)==0;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It) It->Destroy();
    auto* Caster=Spawn(Definitions[ECreatureForm::Ember],FVector(-1300,-1500,40),true);
    auto* Primary=Spawn(Definitions[ECreatureForm::Leaf],FVector(-1000,-1500,40));
    auto* Splash=Spawn(Definitions[ECreatureForm::Stone],FVector(-1000,-1330,40));
    auto* Ally=Spawn(Definitions[ECreatureForm::Leaf],FVector(-900,-1500,40),true);
    const float PrimaryHP=Primary->Health,SplashHP=Splash->Health,AllyHP=Ally->Health;
    bool Skills=Caster->RequestSpecial(Primary); Caster->Tick(0); Caster->Tick(.65f);
    Skills &= FMath::IsNearlyEqual(PrimaryHP-Primary->Health,48.75f,.001f) && FMath::IsNearlyEqual(SplashHP-Splash->Health,12.1875f,.001f) && Ally->Health==AllyHP && Caster->Stamina==80;
    auto* Fighter=Spawn(Definitions[ECreatureForm::Leaf],FVector(-1300,-900,40),true);
    auto* BasicTarget=Spawn(Definitions[ECreatureForm::Stone],FVector(-1180,-900,40));
    const float BasicHP=BasicTarget->Health; Fighter->OrderAttack(BasicTarget); Fighter->Tick(0);
    const bool Basic=FMath::IsNearlyEqual(BasicHP-BasicTarget->Health,13.5f,.001f) && Fighter->Stamina==97;
    auto* Boss=Spawn(Definitions[ECreatureForm::Stone],FVector(0,-1200,40)); Boss->bAlpha=true; Boss->AlphaCooldown=0;
    TArray<ACreatureCharacter*> Defenders; TArray<float> Health;
    for(int32 Index=0;Index<3;++Index) { auto* Pet=Spawn(Definitions.FindRef(static_cast<ECreatureForm>(Index)),FVector(200,-1350+Index*150,40),true); Defenders.Add(Pet); Health.Add(Pet->Health); }
    Hero->SetActorLocation(FVector(0,-1450,40)); Boss->Tick(0); bool Quake=Boss->AlphaWindup>0; Boss->Tick(1.2f);
    const float QuakeDamage[]={20.25f,40.5f,27};
    for(int32 Index=0;Index<3;++Index) Quake &= FMath::IsNearlyEqual(Health[Index]-Defenders[Index]->Health,QuakeDamage[Index],.001f);
    Quake &= FMath::IsNearlyEqual(Hero->Health,73.f,.001f);
    for(TActorIterator<ACreatureCharacter> It(World);It;++It) It->Destroy();
    Hero->Health=100; Hero->SetActorLocation(FVector(-1050,-1050,40));
    auto* TeamLeaf=Spawn(Definitions[ECreatureForm::Leaf],FVector(-700,-1350,40),true);
    auto* TeamEmber=Spawn(Definitions[ECreatureForm::Ember],FVector(-800,-800,40),true);
    Spawn(Definitions[ECreatureForm::Stone],FVector(-600,-800,40),true);
    auto* Enemy=Spawn(Definitions[ECreatureForm::Stone],FVector(-350,-1050,40));
    Hero->ClearCompanionSelection(); bool Scope=Hero->GetGroupMatchupText(Enemy).Contains(TEXT("1 strong / 1 resisted / 1 normal"));
    Scope &= Hero->SelectCompanion(TeamLeaf) && Hero->GetGroupMatchupText(Enemy).Contains(TEXT("1 strong / 0 resisted / 0 normal")); Hero->ClearCompanionSelection();
    TeamEmber->Health=0; Scope &= Hero->GetGroupMatchupText(Enemy).Contains(TEXT("1 strong / 0 resisted / 1 normal")); TeamEmber->Health=TeamEmber->GetMaxHealth();
    bool Saved=Hero->SaveProgress(TEXT("FrontierAffinityAutomation")) && Hero->LoadProgress(TEXT("FrontierAffinityAutomation"));
    Enemy=nullptr; TeamLeaf=nullptr;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)
    {
        It->SetActorTickEnabled(false); It->GetCharacterMovement()->DisableMovement();
        if(It->Tamer!=Hero->GetController() && It->EncounterId.IsNone() && It->Definition==Definitions[ECreatureForm::Stone]) Enemy=*It;
        if(It->Tamer==Hero->GetController() && It->Definition==Definitions[ECreatureForm::Leaf]) TeamLeaf=*It;
    }
    Saved &= Enemy && TeamLeaf && Hero->GetGroupMatchupText(Enemy).Contains(TEXT("1 strong / 1 resisted / 1 normal"));
    if(Enemy && TeamLeaf) { const float Before=Enemy->Health; Saved &= FMath::IsNearlyEqual(Damage(TeamLeaf,Enemy,20),30.f,.001f) && FMath::IsNearlyEqual(Before-Enemy->Health,30.f,.001f); }
    const bool Passed=Matrix && Guard && Neutral && Safety && Skills && Basic && Quake && Scope && Saved;
    UE_LOG(LogTemp,Display,TEXT("CF_AFFINITY %s matrix=%d guard=%d neutral=%d safety=%d skills=%d basic=%d quake=%d scope=%d save=%d"),Passed?TEXT("PASS"):TEXT("FAIL"),Matrix,Guard,Neutral,Safety,Skills,Basic,Quake,Scope,Saved);
    if(Passed && FParse::Param(FCommandLine::Get(),TEXT("CFAffinityPreview")))
    {
        Hero->GetCharacterMovement()->DisableMovement(); const FRotator View=(Enemy->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation(); Hero->GetController()->SetControlRotation(View); Hero->FirstPersonCamera->SetWorldRotation(View);
        Hero->ObserveCreature(TeamLeaf);
        const FString Root=FPaths::ProjectSavedDir()/TEXT("AffinityPreview"); IFileManager::Get().MakeDirectory(*Root,true);
        FTimerHandle Shot,Journal,Research,Done;
        World->GetTimerManager().SetTimer(Shot,[Root](){FScreenshotRequest::RequestScreenshot(Root/TEXT("matchup.png"),false,false);},3.f,false);
        World->GetTimerManager().SetTimer(Journal,[Hero,TeamLeaf](){Hero->ToggleJournal(); for(int32 Index=0;Index<Hero->GetJournal().Num();++Index) { if(Hero->GetJournal()[Hero->GetJournalPage()].Definition.Get()==TeamLeaf->Definition) break; Hero->NextJournalPage(); }},5.f,false);
        World->GetTimerManager().SetTimer(Research,[Root](){FScreenshotRequest::RequestScreenshot(Root/TEXT("journal.png"),false,false);},7.f,false);
        World->GetTimerManager().SetTimer(Done,[](){FPlatformMisc::RequestExitWithStatus(false,0);},9.f,false); return;
    }
    FPlatformMisc::RequestExitWithStatus(false,Passed?0:1);
}
