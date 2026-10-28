#include "CombatScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformMisc.h"
#include "TimerManager.h"
#include "CompanionSkillTrail.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

void RunCompanionCombatScenario(UWorld* World, ACFPlayerCharacter* Hero)
{
    TMap<ECreatureForm, UCreatureDefinition*> Definitions;
    for (TActorIterator<ACreatureCharacter> It(World); It; ++It)
    {
        if (It->Definition) Definitions.Add(It->Definition->Form, It->Definition);
        It->Destroy();
    }
    if (Definitions.Num() != 3) { FPlatformMisc::RequestExitWithStatus(true, 11); return; }
    Hero->SetActorLocation(FVector(-1500,-1800,40));
    Hero->GetCharacterMovement()->DisableMovement();
    auto Spawn = [World, Hero](FVector Location, UCreatureDefinition* Definition, bool bOwned)
    {
        auto* Creature = World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(), FTransform(Location), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Creature) return static_cast<ACreatureCharacter*>(nullptr);
        Creature->Definition = Definition;
        Creature->FinishSpawning(FTransform(Location));
        Creature->GetCharacterMovement()->DisableMovement();
        if (bOwned) Creature->Tame(Hero->GetController());
        else { Creature->Health = 1000; Creature->SetActorTickEnabled(false); }
        return Creature;
    };
    auto* Leaf = Spawn(FVector(-1300,-1500,40), Definitions[ECreatureForm::Leaf], true);
    auto* Ember = Spawn(FVector(-1300,-1200,40), Definitions[ECreatureForm::Ember], true);
    auto* Stone = Spawn(FVector(-1300,-900,40), Definitions[ECreatureForm::Stone], true);
    auto* LeafEnemy = Spawn(FVector(-1000,-1500,40), nullptr, false);
    auto* EmberEnemy = Spawn(FVector(-1000,-1200,40), nullptr, false);
    auto* StoneEnemy = Spawn(FVector(-1000,-900,40), nullptr, false);
    auto* SplashEnemy = Spawn(FVector(-1000,-1040,40), nullptr, false);
    if (!Leaf || !Ember || !Stone || !LeafEnemy || !EmberEnemy || !StoneEnemy || !SplashEnemy)
    { FPlatformMisc::RequestExitWithStatus(true, 11); return; }
    Hero->FirstPersonCamera->SetWorldRotation((LeafEnemy->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());
    Hero->CommandFocus();
    const bool bFocus = Leaf->FocusTarget == LeafEnemy && Ember->FocusTarget == LeafEnemy && Stone->FocusTarget == LeafEnemy;
    Hero->CommandSpecial();
    const bool bAccepted = Ember->RequestSpecial(EmberEnemy) && Stone->RequestSpecial(StoneEnemy);
    auto* Ally = Spawn(FVector(-900,-1200,40), nullptr, true);
    if (!Ally) { FPlatformMisc::RequestExitWithStatus(true, 11); return; }
    Ally->SetActorTickEnabled(false);
    if(FParse::Param(FCommandLine::Get(),TEXT("CFSkillTrailPreview")))
    {
        FTimerHandle Shot;
        World->GetTimerManager().SetTimer(Shot,[World]()
        {
            int32 Count=0;for(TActorIterator<ACompanionSkillTrail> It(World);It;++It)++Count;
            UE_LOG(LogTemp,Display,TEXT("CF_SKILL_TRAIL_SHOT active=%d frame=%llu"),Count,static_cast<uint64>(GFrameCounter));
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("SkillTrailPreview.png"),true,false);
        },.8f,false);
    }
    FTimerHandle Check;
    World->GetTimerManager().SetTimer(Check, [World, Hero, Leaf, Ember, Stone, LeafEnemy, EmberEnemy, StoneEnemy, SplashEnemy, Ally, Spawn, bFocus, bAccepted]()
    {
        const bool bDamage = FMath::IsNearlyEqual(LeafEnemy->Health, 982.0f) && FMath::IsNearlyEqual(EmberEnemy->Health, 967.5f) && FMath::IsNearlyEqual(StoneEnemy->Health, 964.0f);
        LeafEnemy->Tick(0);
        const bool bSlow = LeafEnemy->SlowRemaining > 0 && LeafEnemy->GetCharacterMovement()->MaxWalkSpeed == 160;
        const bool bSplash = FMath::IsNearlyEqual(SplashEnemy->Health, 983.75f) && Ally->Health == 100;
        const float BeforeGuard = Stone->Health;
        UGameplayStatics::ApplyDamage(Stone, 40, StoneEnemy->GetController(), StoneEnemy, nullptr);
        const bool bGuard = Stone->GuardRemaining > 0 && FMath::IsNearlyEqual(BeforeGuard-Stone->Health, 20.0f);
        const bool bCost = Leaf->Stamina == 80 && Ember->Stamina == 80 && Stone->Stamina == 80 && !Leaf->RequestSpecial(LeafEnemy);
        Ally->Stamina = 19;
        const bool bLowStamina = !Ally->RequestSpecial(LeafEnemy);
        Ally->Stamina = 100; Ally->SetActivity(ECreatureActivity::Rest);
        const bool bRestBlocked = !Ally->RequestSpecial(LeafEnemy);

        Hero->SetActorLocation(FVector(0,0,40));
        auto* Provoked = Spawn(FVector(900,0,40), nullptr, false);
        auto* CoveredCaster = Spawn(FVector(500,-1000,40), nullptr, true);
        auto* CoveredEnemy = Spawn(FVector(800,-1000,40), nullptr, false);
        auto* Wall = World->SpawnActor<AStaticMeshActor>(FVector(650,-1000,40), FRotator::ZeroRotator);
        if (!Provoked || !CoveredCaster || !CoveredEnemy || !Wall) { FPlatformMisc::RequestExitWithStatus(true, 11); return; }
        Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
        Wall->SetActorScale3D(FVector(.5,1,2));
        const bool bCoverRequest = CoveredCaster->RequestSpecial(CoveredEnemy);
        CoveredCaster->Tick(0.1f);
        const bool bCover = bCoverRequest && CoveredCaster->SpecialWindupRemaining == 0 && CoveredCaster->Stamina == 100 && CoveredEnemy->Health == 1000;
        Provoked->AggroRange = 100;
        Provoked->Tick(0);
        const bool bInitiallyIdle = Provoked->Activity == ECreatureActivity::Idle;
        UGameplayStatics::ApplyDamage(Provoked, 10, Hero->GetController(), Hero, nullptr);
        Provoked->Tick(0.1f);
        const bool bRetaliation = bInitiallyIdle && Provoked->Activity == ECreatureActivity::Combat && Provoked->Disposition == ECreatureDisposition::Hostile;

        const float SavedCooldown = Leaf->SpecialCooldownRemaining;
        int32 TrailCount=0;bool TrailGeometry=true;
        for(TActorIterator<ACompanionSkillTrail> It(World);It;++It)
        {
            ++TrailCount;TrailGeometry &= !It->GetActorEnableCollision();
            TInlineComponentArray<UStaticMeshComponent*> Marks(*It);
            TrailGeometry &= Marks.Num()==7;
            for(auto* Mark:Marks)TrailGeometry &= Mark->GetStaticMesh()!=nullptr && Mark->GetCollisionEnabled()==ECollisionEnabled::NoCollision && !Mark->CanEverAffectNavigation() && !Mark->GetComponentLocation().ContainsNaN();
        }
        const bool bTrails=TrailCount==3 && TrailGeometry;
        UCreatureDefinition* LeafDefinition = Leaf->Definition;
        const FString Slot = TEXT("FrontierCombatAutomation");
        const bool bSaved = Hero->SaveProgress(Slot);
        Leaf->SpecialCooldownRemaining = 0;
        const bool bLoaded = Hero->LoadProgress(Slot);
        bool bCooldown = false;
        for (TActorIterator<ACreatureCharacter> It(World); It; ++It)
            if (It->Tamer == Hero->GetController() && It->Definition == LeafDefinition)
                bCooldown = SavedCooldown > 0 && FMath::IsNearlyEqual(It->SpecialCooldownRemaining, SavedCooldown) && It->SpecialWindupRemaining == 0;
        const bool bPass = bTrails && bFocus && bAccepted && bDamage && bSlow && bSplash && bGuard && bCost && bLowStamina && bRestBlocked && bCover && bRetaliation && bSaved && bLoaded && bCooldown;
        UE_LOG(LogTemp, Display, TEXT("CF_COMBAT %s focus=%d accepted=%d damage=%d slow=%d splash=%d guard=%d cost=%d lowStamina=%d rest=%d cover=%d retaliation=%d cooldownSave=%d"), bPass ? TEXT("PASS") : TEXT("FAIL"), bFocus, bAccepted, bDamage, bSlow, bSplash, bGuard, bCost, bLowStamina, bRestBlocked, bCover, bRetaliation, bSaved && bLoaded && bCooldown);
        FTimerHandle Cleanup;
        World->GetTimerManager().SetTimer(Cleanup,[World,bPass,bTrails]()
        {
            bool Cleared=true;for(TActorIterator<ACompanionSkillTrail> It(World);It;++It)Cleared=false;
            UE_LOG(LogTemp,Display,TEXT("CF_SKILL_TRAIL %s geometry=%d cleanup=%d"),bPass && Cleared?TEXT("PASS"):TEXT("FAIL"),bTrails,Cleared);
            FPlatformMisc::RequestExitWithStatus(false,bPass && Cleared?0:11);
        },.65f,false);
    }, 0.9f, false);
}
