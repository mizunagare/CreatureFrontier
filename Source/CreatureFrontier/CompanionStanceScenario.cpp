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
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HomeScenario.h"
#include "Containers/Ticker.h"
#include "Components/CapsuleComponent.h"
#include "WildEncounter.h"

void RunCompanionStanceScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    if(FParse::Param(FCommandLine::Get(),TEXT("CFStanceRead")))
    {
        // 只读取前一进程留下的快照，不重建姿态或赠送夹具。
        const auto* Input=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("StanceGood"),0));
        bool Pass=Input && Input->FormatVersion>=31 && Input->FormatVersion<=FrontierCurrentSaveVersion;
        int32 Avoid=0,Guard=0;
        if(Input)for(const auto& Record:Input->Creatures)if(Record.bOwned){if(Record.CombatStance==1)++Avoid;else if(Record.CombatStance==0)++Guard;else Pass=false;}
        Pass &= Avoid==2 && Guard==2;
        for(int32 Attempt=0;Attempt<2 && Pass;++Attempt)
        {
            Pass &= Hero->LoadProgress(TEXT("StanceGood"));
            int32 Matched=0;
            for(TActorIterator<ACreatureCharacter> It(World);It;++It)
            {
                const auto* Record=Input->Creatures.FindByPredicate([&](const FCreatureSaveRecord& R){return R.Id==It->PersistentId;});
                Pass &= Record && It->bAvoidCombat==(Record->CombatStance==1) && It->Health==Record->Health &&
                    ((It->Tamer==Hero->GetController())==Record->bOwned);
                ++Matched;
            }
            Pass &= Matched==Input->Creatures.Num();
        }
        UE_LOG(LogTemp,Display,TEXT("CF_STANCE_READ %s avoid=%d guard=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Avoid,Guard);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return;
    }
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
    // G必须经过真实按键绑定、技能蓄力和唯一伤害结算；避战不屏蔽明确指令。
    bool Skill=Hero->SelectCompanion(Ally) && CFInputQA::Press(Hero,EKeys::G);
    const float BeforeSkill=Enemy->Health,BeforeStamina=Ally->Stamina;
    Ally->Tick(.1f);
    Skill &= Ally->SpecialWindupRemaining>0 && Ally->SpecialCooldownRemaining>0 && FMath::IsNearlyEqual(Ally->Stamina,BeforeStamina-20,.01f) && Enemy->Health==BeforeSkill;
    for(int32 I=0;I<7;++I)Ally->Tick(.1f);
    const float ExpectedDamage=Ally->GetAttackDamage()*Definition->SpecialDamageScale;
    Skill &= Ally->bAvoidCombat && Ally->SpecialWindupRemaining==0 && FMath::IsNearlyEqual(Enemy->Health,BeforeSkill-ExpectedDamage,.01f);
    Ally->SetAvoidCombat(true);
    const float AfterSkill=Enemy->Health;
    for(int32 I=0;I<120;++I)Ally->Tick(.1f);
    Skill &= Enemy->Health==AfterSkill && !Ally->FocusTarget;
    bool SkillCancel=CFInputQA::Press(Hero,EKeys::G);
    Ally->Tick(.1f);SkillCancel &= Ally->SpecialWindupRemaining>0;
    // 已在避战时先F8切护主、再F8切回避战，取消明确蓄力。
    SkillCancel &= CFInputQA::Press(Hero,EKeys::F8) && CFInputQA::Press(Hero,EKeys::F8) && Ally->bAvoidCombat && Ally->SpecialWindupRemaining==0;
    for(int32 I=0;I<8;++I)Ally->Tick(.1f);
    SkillCancel &= Enemy->Health==AfterSkill && !Ally->FocusTarget;
    const bool Saved=Hero->SaveProgress(TEXT("StanceGood"));
    auto* Snapshot=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("StanceGood"),0));
    bool Invalid=Snapshot!=nullptr;
    if(Snapshot)
    {
        for(auto& Record:Snapshot->Creatures)if(Record.Id==Id)Record.CombatStance=255;
        Invalid &= UGameplayStatics::SaveGameToSlot(Snapshot,TEXT("StanceBad"),0) && !Hero->LoadProgress(TEXT("StanceBad")) && IsValid(Ally) && Ally->bAvoidCombat;
        for(auto& Record:Snapshot->Creatures)
        {
            if(Record.Id==Id)Record.CombatStance=1;
            if(Record.Id==Enemy->PersistentId)Record.CombatStance=1;
        }
        const float HeroHPBefore=Hero->Health;const FVector PositionBefore=Hero->GetActorLocation();
        Invalid &= UGameplayStatics::SaveGameToSlot(Snapshot,TEXT("StanceBad"),0) && !Hero->LoadProgress(TEXT("StanceBad")) &&
            IsValid(Ally) && IsValid(Enemy) && Ally->bAvoidCombat && !Enemy->bAvoidCombat && Hero->Health==HeroHPBefore && Hero->GetActorLocation().Equals(PositionBefore);
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
    const bool Pass=Defaults && Passive && Explicit && Cancel && Invalid && Persist && Legacy && Scope && Guard && Bounds && Work && Skill && SkillCancel;
    UE_LOG(LogTemp,Display,TEXT("CF_STANCE_SKILL %s explicit=%d cancel=%d"),Skill && SkillCancel?TEXT("PASS"):TEXT("FAIL"),Skill,SkillCancel);
    UE_LOG(LogTemp,Display,TEXT("CF_STANCE_BOUNDARIES %s scope=%d guard=%d bounds=%d work=%d"),Scope && Guard && Bounds && Work?TEXT("PASS"):TEXT("FAIL"),Scope,Guard,Bounds,Work);
    UE_LOG(LogTemp,Display,TEXT("CF_STANCE %s defaults=%d passive=%d explicit=%d cancel=%d invalid=%d persist=%d legacy=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Defaults,Passive,Explicit,Cancel,Invalid,Persist,Legacy);
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}

void RunCompanionRetreatScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    // 仅固定出发夹具；受测双方从生成后一直正常Tick，不手动结算伤害或瞬移。
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)It->SetActorTickEnabled(false);
    for(TActorIterator<AWildEncounter> It(World);It;++It)It->SetActorTickEnabled(false);
    auto* Definition=LoadObject<UCreatureDefinition>(nullptr,TEXT("/Game/Creatures/DA_Craglet.DA_Craglet"));
    if(!Definition){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    const FVector Origin(-450,-1100,40);Hero->SetActorLocation(Origin);
    auto Spawn=[&](FVector Offset,bool Owned)
    {
        const FTransform At(Origin+Offset);auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),At);
        if(Pet){Pet->Definition=Definition;Pet->FinishSpawning(At);if(Owned)Pet->Tame(Hero->GetController());else Pet->Disposition=ECreatureDisposition::Hostile;}return Pet;
    };
    auto* Ally=Spawn(FVector(80,90,0),true);auto* Enemy=Spawn(FVector(220,0,0),false);
    if(!Ally || !Enemy){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    auto* Input=NewObject<UHomeMovementProbe>(Hero);Input->RegisterComponent();
    struct FRetreatState { int32 Stage=0,Point=0;FVector HeroPrevious,PetPrevious;float HeroTravel=0,PetTravel=0,EnemyHP=0;double Deadline=0;bool Prepared=false; };
    auto State=MakeShared<FRetreatState>();State->HeroPrevious=Hero->GetActorLocation();State->PetPrevious=Ally->GetActorLocation();State->Deadline=FPlatformTime::Seconds()+30;
    Hero->ClearCompanionSelection();
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([World,Hero,Ally,Enemy,Input,State](float)
    {
        const FVector Now=Hero->GetActorLocation(),PetNow=Ally->GetActorLocation();
        const float HeroStep=FVector::Dist(Now,State->HeroPrevious),PetStep=FVector::Dist(PetNow,State->PetPrevious);
        State->HeroTravel+=HeroStep;State->PetTravel+=PetStep;State->HeroPrevious=Now;State->PetPrevious=PetNow;
        auto Finish=[&](bool Pass)
        {
            Input->Direction=FVector::ZeroVector;
            UE_LOG(LogTemp,Display,TEXT("CF_STANCE_RETREAT %s stage=%d prepared=%d heroTravel=%.1f petTravel=%.1f distance=%.1f heroHP=%.1f petHP=%.1f enemyHP=%.1f"),Pass?TEXT("PASS"):TEXT("FAIL"),State->Stage,State->Prepared,State->HeroTravel,State->PetTravel,Ally->GetDistanceTo(Hero),Hero->Health,Ally->Health,Enemy->Health);
            FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
        };
        if(FPlatformTime::Seconds()>State->Deadline || Hero->Health<=0 || Ally->Health<=0 || Enemy->Health<=0 || HeroStep>180 || PetStep>180 ||
            !Ally->IsActorTickEnabled() || !Enemy->IsActorTickEnabled() || Hero->GetCapsuleComponent()->GetCollisionEnabled()==ECollisionEnabled::NoCollision || Ally->GetCapsuleComponent()->GetCollisionEnabled()==ECollisionEnabled::NoCollision)return Finish(false);
        if(State->Stage==0)
        {
            const auto View=(Enemy->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
            if(Ally->FocusTarget!=Enemy && Hero->TraceCreature()==Enemy)CFInputQA::Press(Hero,EKeys::Q);
            if(Ally->Health<Ally->GetMaxHealth() && Enemy->Health<Enemy->GetMaxHealth())
            {
                if(!CFInputQA::Press(Hero,EKeys::F8) || !Ally->bAvoidCombat || !CFInputQA::Press(Hero,EKeys::F))return Finish(false);
                State->Prepared=true;State->EnemyHP=Enemy->Health;State->Stage=1;
                UE_LOG(LogTemp,Display,TEXT("CF_STANCE_RETREAT_SWITCH PASS petHP=%.1f enemyHP=%.1f"),Ally->Health,Enemy->Health);
            }
            return true;
        }
        if(!Ally->bAvoidCombat || Ally->FocusTarget || Ally->SpecialWindupRemaining>0 || Enemy->Health!=State->EnemyHP)return Finish(false);
        const FVector Points[]={FVector(-450,-750,40),FVector(-250,-400,40)};
        if(State->Point<2)
        {
            if(FVector::Dist2D(Now,Points[State->Point])<65){++State->Point;Input->Direction=FVector::ZeroVector;}
            else {Input->Direction=(Points[State->Point]-Now).GetSafeNormal2D();Hero->GetController()->SetControlRotation(Input->Direction.Rotation());}
            return true;
        }
        Input->Direction=FVector::ZeroVector;
        if(Ally->GetDistanceTo(Hero)>240)return true;
        return Finish(State->Prepared && State->HeroTravel>650 && State->PetTravel>500 && Ally->Activity==ECreatureActivity::Follow);
    }),.1f);
}
