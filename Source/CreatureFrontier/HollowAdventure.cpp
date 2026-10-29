#include "HollowScenario.h"
#include "CFPlayerCharacter.h"
#include "CFHUD.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "WildEncounter.h"
#include "WorldLoot.h"
#include "ResourceNode.h"
#include "InventoryComponent.h"
#include "ResearchComponent.h"
#include "HomeScenario.h"
#include "InputRoutingQA.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Controller.h"
#include "EngineUtils.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformMisc.h"
#include "Algo/Reverse.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "PlayerPaceComponent.h"
#include "CompanionSkillTrail.h"
#include "InputKeyEventArgs.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "TimerManager.h"
#include "BaseBuildingComponent.h"
#include "FrontierSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "AIController.h"

namespace
{
AResourceNode* HollowCache(UWorld* World)
{for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,11))return *It;return nullptr;}
AWildEncounter* HollowCamp(UWorld* World)
{for(TActorIterator<AWildEncounter> It(World);It;++It)if(It->EncounterId==TEXT("EmberHollow"))return *It;return nullptr;}
ACreatureCharacter* AdventurePet(UWorld* World,ACFPlayerCharacter* Hero)
{for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController())return *It;return nullptr;}
bool DownedCheckpointMatches(UWorld* World,ACFPlayerCharacter* Hero,const UFrontierSaveGame* Save)
{
    auto* Camp=HollowCamp(World);auto* Enemy=Camp?Camp->GetResident():nullptr;auto* Pet=AdventurePet(World,Hero);auto* Cache=HollowCache(World);
    if(!Save || Save->FormatVersion<28 || Save->FormatVersion>FrontierCurrentSaveVersion || Save->Health!=0 || Hero->Health!=0 || !Pet || !Enemy || !Cache || Cache->Remaining!=6 ||
        Pet->Health<=0 || Pet->Health>=Pet->GetMaxHealth() || Enemy->Health<=0 || Enemy->Health>=Enemy->GetMaxHealth() ||
        Hero->GetActorLocation().Y>=-2700 || !Hero->GetActorLocation().Equals(Save->PlayerTransform.GetLocation(),.1f) ||
        Hero->GetCharacterMovement()->MovementMode!=MOVE_None || Hero->Pace->IsSlowWalking() || Hero->GetRespawnProtectionRemaining()>0)return false;
    for(int32 I=0;I<static_cast<int32>(EFrontierItem::Count);++I)
        if(Hero->Inventory->Count(static_cast<EFrontierItem>(I))!=Save->InventoryStacks.FindRef(static_cast<EFrontierItem>(I)))return false;
    int32 Creatures=0,Owned=0,Nodes=0;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)
    {
        ++Creatures;if(It->Tamer==Hero->GetController())++Owned;
        const auto* Record=Save->Creatures.FindByPredicate([&](const auto& R){return R.Id==It->PersistentId;});
        if(!Record || !FMath::IsNearlyEqual(It->Health,Record->Health) || !FMath::IsNearlyEqual(It->Stamina,Record->Stamina) ||
            !FMath::IsNearlyEqual(It->Nutrition,Record->Nutrition) || !FMath::IsNearlyEqual(It->SpecialCooldownRemaining,Record->SpecialCooldown) ||
            It->Level!=Record->Level || It->Experience!=Record->Experience || It->EncounterId!=Record->EncounterId ||
            (It->Tamer==Hero->GetController())!=Record->bOwned || !It->GetActorLocation().Equals(Record->Transform.GetLocation(),.1f))return false;
    }
    for(TActorIterator<AResourceNode> It(World);It;++It)
    {
        ++Nodes;const auto* Record=Save->ResourceNodes.FindByPredicate([&](const auto& R){return R.Id==It->PersistentId;});
        if(!Record || It->Remaining!=Record->Remaining || !FMath::IsNearlyEqual(It->HarvestCooldown,Record->HarvestCooldown) || !FMath::IsNearlyEqual(It->RegrowRemaining,Record->RegrowRemaining))return false;
    }
    return Owned==1 && Creatures==Save->Creatures.Num() && Nodes==Save->ResourceNodes.Num();
}
bool AdventureSnapshot(UWorld* World,ACFPlayerCharacter* Hero)
{
    auto* Cache=HollowCache(World);auto* Camp=HollowCamp(World);auto* Pet=AdventurePet(World,Hero);
    int32 Owned=0;for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController())++Owned;
    return Cache && Cache->Remaining==0 && Cache->RegrowRemaining==0 && Camp && !Camp->GetResident() && Camp->GetSavedRemaining()>0 &&
        Owned==1 && Pet && Pet->PersistentId.IsValid() && Pet->Definition && Pet->Definition->Form==ECreatureForm::Stone && Pet->Health>0 && Pet->Level==2 && Pet->Experience==50 &&
        Hero->Health>0 && Hero->Inventory->Count(EFrontierItem::Material)==6 && Hero->Inventory->Count(EFrontierItem::Food)==9 &&
        Hero->Inventory->Count(EFrontierItem::Scrap)==4 && Hero->Inventory->Count(EFrontierItem::Crystal)==0 && Hero->Inventory->Count(EFrontierItem::ResonanceDevice)==2;
}
struct FAdventure
{
    UWorld* World;ACFPlayerCharacter* Hero;ACreatureCharacter* Pet;ACreatureCharacter* Enemy;AWildEncounter* Camp;UHomeMovementProbe* Input;
    TArray<FVector> Points;int32 Index=0,Harvested=0,Mode=0;bool Battle=false,Defeated=false,LootDone=false,Returning=false,Skill=false,Incoming=false;
    FVector Previous,PreviousPet;float Travel=0,PetSouth=0;double Deadline=0;
    FString Shots;TSet<FString> Taken;double FinishAfter=0;
    int32 RecoveryStage=0;float PetTravel=0,ReturnStartTravel=0,SecondHeroHP=0,SecondPetHP=0;double NextRecoveryLog=0;bool RecoveryAttackSent=false;
    TMap<EFrontierItem,int32> RecoveryBag;
    double DownedShotAfter=0,RejoinedShotAfter=0;
    uint64 LastShotAuditFrame=MAX_uint64;
    bool WriteDownedCheckpoint=false,ResumedCheckpoint=false;
    float ObservedHeroHP=-1,ObservedPetHP=-1,ObservedEnemyHP=-1;
    double NextRouteLog=0;
    double NextRouteSample=0;
    FVector RouteProgressAnchor=FVector::ZeroVector;
    double RouteProgressAt=0;
    bool SameRecoveryBag() const
    {for(int32 I=0;I<static_cast<int32>(EFrontierItem::Count);++I)if(Hero->Inventory->Count(static_cast<EFrontierItem>(I))!=RecoveryBag.FindRef(static_cast<EFrontierItem>(I)))return false;return true;}
    void Shot(const TCHAR* Name)
    {
        if(Shots.IsEmpty() || Taken.Contains(Name))return;
        auto* PC=Cast<APlayerController>(Hero->GetController());auto* HUD=PC?Cast<ACFHUD>(PC->GetHUD()):nullptr;
        if(!HUD || !HUD->GetLayoutAudit().bFits || !HUD->GetLayoutAudit().bNoOverlap || !HUD->GetLayoutAudit().bCenterClear)return;
        if(FScreenshotRequest::IsScreenshotRequested() || LastShotAuditFrame==HUD->GetLayoutAudit().Frame)return;
        LastShotAuditFrame=HUD->GetLayoutAudit().Frame;
        Taken.Add(Name);FScreenshotRequest::RequestScreenshot(Shots/(FString(Name)+TEXT(".png")),true,false);
        UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_ADVENTURE_SHOT %s viewport=%.0fx%.0f frame=%llu"),Name,HUD->GetLayoutAudit().Viewport.X,HUD->GetLayoutAudit().Viewport.Y,HUD->GetLayoutAudit().Frame);
    }
    bool Fail(const TCHAR* Reason)
    {
        UE_LOG(LogTemp,Error,TEXT("CF_HOLLOW_ADVENTURE FAIL %s index=%d return=%d hero=%s hp=%.1f pet=%s hp=%.1f enemy=%s hp=%.1f skill=%d incoming=%d"),Reason,Index,Returning,*Hero->GetActorLocation().ToString(),Hero->Health,*Pet->GetActorLocation().ToString(),Pet->Health,*Enemy->GetActorLocation().ToString(),Enemy->Health,Skill,Incoming);
        if(FParse::Param(FCommandLine::Get(),TEXT("CFHollowFailureSnapshot")))
        {
            // 只在显式诊断失败后保存到唯一槽，不覆盖玩家槽或测试输入。
            const FString Slot=TEXT("HollowFailure-")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
            const bool Saved=Hero->SaveProgress(Slot);
            const auto* Snapshot=Saved?Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0)):nullptr;
            const bool Verified=Snapshot && Snapshot->FormatVersion==FrontierCurrentSaveVersion && Snapshot->Health==Hero->Health &&
                Snapshot->PlayerTransform.GetLocation().Equals(Hero->GetActorLocation(),.01);
            UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_FAILURE_SNAPSHOT saved=%d verified=%d slot=%s index=%d stage=%d battle=%d focus=%s"),
                Saved,Verified,*Slot,Index,RecoveryStage,Battle,*GetNameSafe(Pet->FocusTarget));
        }
        FPlatformMisc::RequestExitWithStatus(false,1);return false;
    }
    void Drive(const FVector& Goal)
    {const auto Delta=Goal-Hero->GetActorLocation();Input->Direction=FVector(Delta.X,Delta.Y,0).GetSafeNormal();Hero->SetActorRotation(Input->Direction.Rotation());Hero->GetController()->SetControlRotation(Input->Direction.Rotation());}
    bool Tick()
    {
        const FVector Now=Hero->GetActorLocation(),PetNow=Pet->GetActorLocation();
        // 固定频率记录原路线输入与真实路径段，覆盖未触发局部停滞阈值的振荡。
        if(Mode==3 && RecoveryStage==0 && FParse::Param(FCommandLine::Get(),TEXT("CFHollowRouteTrace")) && FPlatformTime::Seconds()>=NextRouteSample)
        {
            NextRouteSample=FPlatformTime::Seconds()+.5;
            const auto* AI=Cast<AAIController>(Pet->GetController());
            UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_ROUTE_SAMPLE time=%.3f index=%d battle=%d hero=%s input=%s velocity=%s pet=%s petVelocity=%s petActivity=%d focusId=%s focus=%s moveStatus=%d segment=%s enemy=%s enemyHP=%.1f heroHP=%.1f petHP=%.1f aimed=%s"),
                World->GetTimeSeconds(),Index,Battle,*Now.ToString(),*Input->Direction.ToString(),*Hero->GetVelocity().ToString(),*PetNow.ToString(),*Pet->GetVelocity().ToString(),
                static_cast<int32>(Pet->Activity),Pet->FocusTarget?*Pet->FocusTarget->PersistentId.ToString():TEXT("none"),*GetNameSafe(Pet->FocusTarget),
                AI?static_cast<int32>(AI->GetMoveStatus()):-1,AI?*AI->GetImmediateMoveDestination().ToString():TEXT("none"),*Enemy->GetActorLocation().ToString(),Enemy->Health,Hero->Health,Pet->Health,*GetNameSafe(Hero->TraceCreature()));
        }
        // 只记录QA自然交战时序，不更改目标、生命、输入或恢复判据。
        if(Mode==3 && RecoveryStage==0 && (Hero->Health!=ObservedHeroHP || Pet->Health!=ObservedPetHP || Enemy->Health!=ObservedEnemyHP))
        {
            UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_RECOVERY_HEALTH time=%.3f heroHP=%.2f petHP=%.2f enemyHP=%.2f heroDistance=%.2f petDistance=%.2f battle=%d attackSent=%d petFocus=%s petActivity=%d"),
                World->GetTimeSeconds(),Hero->Health,Pet->Health,Enemy->Health,Enemy->GetDistanceTo(Hero),Enemy->GetDistanceTo(Pet),Battle,RecoveryAttackSent,*GetNameSafe(Pet->FocusTarget),static_cast<int32>(Pet->Activity));
            ObservedHeroHP=Hero->Health;ObservedPetHP=Pet->Health;ObservedEnemyHP=Enemy->Health;
        }
        const float Step=FVector::Dist(Now,Previous),PetStep=FVector::Dist(PetNow,PreviousPet);Travel+=FVector::Dist2D(Now,Previous);PetTravel+=FVector::Dist2D(PetNow,PreviousPet);Previous=Now;PreviousPet=PetNow;PetSouth=FMath::Min(PetSouth,PetNow.Y);
        // 卡路时记录真实胶囊前方的阻挡物，先区分洞壁与伙伴碰撞，不改变路线或碰撞规则。
        if(FVector::Dist2D(Now,RouteProgressAnchor)>80)
        {RouteProgressAnchor=Now;RouteProgressAt=FPlatformTime::Seconds();}
        // 以持续未离开局部区域判定，覆盖每帧仍有小幅移动的往返振荡。
        if(Mode==3 && RecoveryStage==0 && FPlatformTime::Seconds()-RouteProgressAt>3 && !Input->Direction.IsNearlyZero() && FPlatformTime::Seconds()>NextRouteLog)
        {
            NextRouteLog=FPlatformTime::Seconds()+5;
            FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(HollowRouteObstacle),false,Hero);
            const auto* Capsule=Hero->GetCapsuleComponent();
            const bool Blocked=World->SweepSingleByChannel(Hit,Now,Now+Input->Direction.GetSafeNormal()*100,FQuat::Identity,ECC_Pawn,
                FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight()),Query);
            FHitResult Sight;
            const FVector Eye=Hero->FirstPersonCamera->GetComponentLocation();
            World->LineTraceSingleByChannel(Sight,Eye,Enemy->GetActorLocation(),ECC_Visibility,Query);
            UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_ROUTE_STALL index=%d hero=%s goal=%s input=%s velocity=%s blocked=%d actor=%s component=%s impact=%s battle=%d focus=%s"),
                Index,*Now.ToString(),Points.IsValidIndex(Index)?*Points[Index].ToString():TEXT("none"),*Input->Direction.ToString(),
                *Hero->GetVelocity().ToString(),Blocked,*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()),*Hit.ImpactPoint.ToString(),Battle,*GetNameSafe(Pet->FocusTarget));
            UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_ROUTE_SIGHT index=%d actor=%s component=%s enemy=%s enemyHP=%.1f pet=%s petHP=%.1f aimed=%s"),
                Index,*GetNameSafe(Sight.GetActor()),*GetNameSafe(Sight.GetComponent()),*Enemy->GetActorLocation().ToString(),Enemy->Health,
                *PetNow.ToString(),Pet->Health,*GetNameSafe(Hero->TraceCreature()));
        }
        Incoming |= Mode==3 && RecoveryStage==2?(Hero->Health<SecondHeroHP || Pet->Health<SecondPetHP):(Hero->Health<100 || Pet->Health<Pet->GetMaxHealth());Skill |= Pet->SpecialCooldownRemaining>0 && Pet->Stamina<=80;
        if(!Returning && Now.Y<-2200)Shot(TEXT("Entrance"));
        if(Pet->SpecialWindupRemaining>0)Shot(TEXT("Skill"));
        for(TActorIterator<ACompanionSkillTrail> It(World);It;++It)
            if(It->GetOwner()==Pet){Shot(TEXT("Impact"));break;}
        if(FPlatformTime::Seconds()>Deadline || Step>160 || PetStep>160 || Now.Z<0 || PetNow.Z<-10 || Pet->Health<=0 ||
            Hero->GetCapsuleComponent()->GetCollisionEnabled()==ECollisionEnabled::NoCollision || Pet->GetCapsuleComponent()->GetCollisionEnabled()==ECollisionEnabled::NoCollision ||
            !Pet->IsActorTickEnabled() || !Enemy->IsActorTickEnabled() || !Camp->IsActorTickEnabled())return Fail(TEXT("continuous/alive/deadline"));
        if(Mode==3 && RecoveryStage==0 && Hero->Health<=0)
        {
            UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_RECOVERY_DEATH_GATES south=%d near=%d enemyAlive=%d enemyHurt=%d petHurt=%d resident=%d noFocus=%d noWindup=%d noSlow=%d"),
                Now.Y<-2700,Enemy->GetDistanceTo(Hero)<=300,Enemy->Health>0,Enemy->Health<Enemy->GetMaxHealth(),Pet->Health<Pet->GetMaxHealth(),Camp->GetResident()==Enemy,!Pet->FocusTarget,Pet->SpecialWindupRemaining<=0,!Hero->Pace->IsSlowWalking());
            // 受伤出发后必须由自然住民实际击倒；残血目标才能有效验证复活不补血。
            if(Now.Y>=-2700 || Enemy->GetDistanceTo(Hero)>300 || Enemy->Health<=0 || Enemy->Health>=Enemy->GetMaxHealth() || Pet->Health>=Pet->GetMaxHealth() || Camp->GetResident()!=Enemy || Pet->FocusTarget || Pet->SpecialWindupRemaining>0 || Hero->Pace->IsSlowWalking())return Fail(TEXT("natural-death-state"));
            Input->Direction=FVector::ZeroVector;
            if(WriteDownedCheckpoint)
            {
                const bool Saved=Hero->SaveProgress(TEXT("HollowDownedCross"));
                const auto* Snapshot=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("HollowDownedCross"),0));
                const bool Pass=Saved && DownedCheckpointMatches(World,Hero,Snapshot);
                UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_DOWNED_WRITE %s pet=%s enemy=%s petHP=%.1f enemyHP=%.1f"),Pass?TEXT("PASS"):TEXT("FAIL"),*Pet->PersistentId.ToString(),*Enemy->PersistentId.ToString(),Pet->Health,Enemy->Health);
                FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
            }
            if(!Shots.IsEmpty())
            {
                if(DownedShotAfter==0){Shot(TEXT("Downed"));if(Taken.Contains(TEXT("Downed")))DownedShotAfter=FPlatformTime::Seconds()+.35;return true;}
                if(FPlatformTime::Seconds()<DownedShotAfter)return true;
            }
            RecoveryBag=Hero->Inventory->GetStacks();
            const float EnemyHP=Enemy->Health,PetHP=Pet->Health;const FGuid PetId=Pet->PersistentId;
            if(!CFInputQA::Press(Hero,EKeys::R) || Hero->Health!=100 || FVector::Dist2D(Hero->GetActorLocation(),UBaseBuildingComponent::BaseOrigin())>1201 || Hero->GetRespawnProtectionRemaining()<=0 ||
                Pet->PersistentId!=PetId || Pet->Health!=PetHP || Enemy->Health!=EnemyHP || Camp->GetResident()!=Enemy || !SameRecoveryBag() || !HollowCache(World) || HollowCache(World)->Remaining!=6)return Fail(TEXT("respawn-preservation"));
            Previous=Hero->GetActorLocation();ReturnStartTravel=PetTravel;RecoveryStage=1;
            UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_RECOVERY_RESPAWN PASS death=%s base=%s petHP=%.1f enemyHP=%.1f"),*Now.ToString(),*Previous.ToString(),PetHP,EnemyHP);
            return true;
        }
        if(Hero->Health<=0)return Fail(TEXT("unexpected-player-death"));
        if(Mode==3 && RecoveryStage==1)
        {
            Input->Direction=FVector::ZeroVector;
            Shot(TEXT("Revived"));
            if(!SameRecoveryBag() || HollowCache(World)->Remaining!=6)return Fail(TEXT("return-conservation"));
            if(FPlatformTime::Seconds()>NextRecoveryLog)
            {NextRecoveryLog=FPlatformTime::Seconds()+5;UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_RECOVERY_RETURN pet=%s distance=%.1f travel=%.1f hp=%.1f activity=%d"),*PetNow.ToString(),Pet->GetDistanceTo(Hero),PetTravel-ReturnStartTravel,Pet->Health,static_cast<int32>(Pet->Activity));}
            if(Pet->GetDistanceTo(Hero)>300)return true;
            if(!Shots.IsEmpty())
            {
                // 图形夹具正常转头看向返家的伙伴，世界与导航继续运行。
                const auto View=(PetNow-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
                Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
                if(RejoinedShotAfter==0){Shot(TEXT("Rejoined"));if(Taken.Contains(TEXT("Rejoined")))RejoinedShotAfter=FPlatformTime::Seconds()+.35;return true;}
                if(FPlatformTime::Seconds()<RejoinedShotAfter)return true;
            }
            if(PetTravel-ReturnStartTravel<2500 || !CFInputQA::Press(Hero,EKeys::LeftShift) || !Hero->Pace->IsSlowWalking())return Fail(TEXT("return-navigation"));
            RecoveryStage=2;Index=0;Battle=false;Skill=false;Incoming=false;SecondHeroHP=Hero->Health;SecondPetHP=Pet->Health;
            UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_RECOVERY_REJOIN PASS travel=%.1f petHP=%.1f"),PetTravel-ReturnStartTravel,Pet->Health);
            return true;
        }
        if(!Defeated && Enemy->Disposition==ECreatureDisposition::Hostile && Now.Y<-2700)Battle=true;
        // 恢复QA提前进入可瞄准的交战准备区，不等待残血玩家先被自然索敌。
        if(Mode==3 && RecoveryStage==0 && Now.Y<-2700 && Hero->GetDistanceTo(Enemy)<750)Battle=true;
        if(Battle && !Defeated)
        {
            Input->Direction=FVector::ZeroVector;
            if(Enemy->Health>0)
            {
                const auto View=(Enemy->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
                if(Mode==3 && RecoveryStage==0)
                {
                    if(Pet->Health>=Pet->GetMaxHealth() || Enemy->Health>=Enemy->GetMaxHealth())
                    {
                        // 通过真实集火指令建立自然伤害前置；玩家保持距离，避免被先击倒。
                        if(Pet->FocusTarget!=Enemy && Hero->TraceCreature()!=Enemy)
                        {
                            // 距离进入准备区不代表已穿过洞壁看到敌人，仍沿原路线前进。
                            if(Points.IsValidIndex(Index))
                            {
                                if(FVector::Dist2D(Now,Points[Index])<70)++Index;
                                else Drive(Points[Index]);
                            }
                            return true;
                        }
                        if(Pet->FocusTarget!=Enemy && Hero->TraceCreature()==Enemy)
                            if(!CFInputQA::Press(Hero,EKeys::Q))return Fail(TEXT("recovery-prepare-input"));
                        if(Hero->GetDistanceTo(Enemy)<450)Drive(Now+FVector(0,250,0));
                        Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
                        return true;
                    }
                    if(Pet->FocusTarget)
                    {
                        // 使用玩家正式姿态指令撤回自动护主，保留移动、受伤和正常AI。
                        // 不关闭伙伴Tick，也不降低原18生命出发夹具。
                        if(!Pet->bAvoidCombat && !CFInputQA::Press(Hero,EKeys::F8))return Fail(TEXT("recovery-avoid-input"));
                        if(!Pet->bAvoidCombat)return Fail(TEXT("recovery-avoid-state"));
                        if(!CFInputQA::Press(Hero,EKeys::F) || Pet->FocusTarget)return Fail(TEXT("recovery-prepare-follow"));
                        // 取消显式集火不关闭自动护主；及时松开慢行接近并用近战吸引敌人，
                        // 避免在远处缓慢接近期间让自动支援先击败自然住民。
                        if(!CFInputQA::Release(Hero,EKeys::LeftShift) || Hero->Pace->IsSlowWalking())return Fail(TEXT("recovery-prepare-pace"));
                        UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_RECOVERY_PREPARED PASS heroHP=%.1f petHP=%.1f enemyHP=%.1f"),Hero->Health,Pet->Health,Enemy->Health);
                        UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_RECOVERY_AVOID PASS"));
                    }
                    if(Hero->GetDistanceTo(Enemy)>160)Drive(Enemy->GetActorLocation());
                    Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
                    if(!RecoveryAttackSent && Hero->GetDistanceTo(Enemy)<=180 && Hero->TraceCreature()==Enemy)
                    {
                        // 近战是Action映射，必须经过引擎输入链，不能用仅查KeyBindings的帮助函数。
                        auto* PC=CastChecked<APlayerController>(Hero->GetController());
                        PC->InputKey(FInputKeyEventArgs(nullptr,IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(),EKeys::LeftMouseButton,IE_Pressed,FPlatformTime::Cycles64()));RecoveryAttackSent=true;
                        FTimerHandle Release;TWeakObjectPtr<APlayerController> WeakPC=PC;
                        World->GetTimerManager().SetTimer(Release,[WeakPC](){if(auto* Controller=WeakPC.Get())Controller->InputKey(FInputKeyEventArgs(nullptr,IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(),EKeys::LeftMouseButton,IE_Released,FPlatformTime::Cycles64()));},.15f,false);
                    }
                    return true;
                }
                if(Hero->TraceCreature()==Enemy)
                {if(Pet->FocusTarget!=Enemy)CFInputQA::Press(Hero,EKeys::Q);if(!Skill)CFInputQA::Press(Hero,EKeys::G);}
                return true;
            }
            if(!Skill || !Incoming)return Fail(TEXT("combat-evidence"));
            Defeated=true;UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_ADVENTURE_BATTLE PASS heroHP=%.1f petHP=%.1f skill=1 incoming=1"),Hero->Health,Pet->Health);
        }
        if(Defeated && !LootDone)
        {
            const int32 Bag=Hero->Inventory->Count(EFrontierItem::Material);int32 OnGround=0;AWorldLoot* Target=nullptr;
            for(TActorIterator<AWorldLoot> It(World);It;++It){OnGround+=It->Materials;if(It->Materials>0)Target=*It;}
            if(Bag+OnGround!=6)return Fail(TEXT("loot-conservation"));
            if(Bag==6)LootDone=true;
            else if(Target){if(FVector::Dist2D(Now,Target->GetActorLocation())>170)Drive(Target->GetActorLocation());else Input->Direction=FVector::ZeroVector;return true;}
            else return Fail(TEXT("loot-missing"));
        }
        if(Index<Points.Num())
        {if(FVector::Dist2D(Now,Points[Index])<70){++Index;Input->Direction=FVector::ZeroVector;}else Drive(Points[Index]);return true;}
        Input->Direction=FVector::ZeroVector;
        if(!Returning)
        {
            if(!Defeated || !LootDone)return Fail(TEXT("bypassed-combat"));
            auto* Cache=HollowCache(World);if(!Cache)return Fail(TEXT("cache"));
            if(Harvested<6)
            {
                if(Cache->HarvestCooldown>0)return true;
                const auto View=(Cache->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
                const int32 Before=Hero->Inventory->Count(EFrontierItem::Scrap);
                if(!CFInputQA::Press(Hero,EKeys::M) || Hero->Inventory->Count(EFrontierItem::Scrap)!=Before+1)return Fail(TEXT("harvest"));
                ++Harvested;return true;
            }
            if(PetSouth>-3600 || Pet->GetDistanceTo(Hero)>330)return true;
            Shot(TEXT("Cache"));
            Algo::Reverse(Points);Points.Add(FVector(-250,-400,40));Index=0;Returning=true;return true;
        }
        if(Pet->GetDistanceTo(Hero)>300)return true;
        if(!Shots.IsEmpty())
        {
            if(FinishAfter==0){Shot(TEXT("Home"));FinishAfter=FPlatformTime::Seconds()+.6;return true;}
            if(FPlatformTime::Seconds()<FinishAfter)return true;
        }
        const FGuid Id=Pet->PersistentId;
        bool Pass=Hero->Pace->IsSlowWalking() && CFInputQA::Release(Hero,EKeys::LeftShift) && !Hero->Pace->IsSlowWalking();
        Pass &= Travel>7000 && PetSouth<-3600 && Hero->CraftRecipe(TEXT("ScrapResonator")) && AdventureSnapshot(World,Hero);
        const FString Slot=ResumedCheckpoint?TEXT("HollowCheckpointFinal"):Mode==3?TEXT("HollowRecoveryCross"):TEXT("HollowAdventureCross");
        Pass &= (Mode!=3 || RecoveryStage==2) && Hero->SaveProgress(Slot) && Hero->LoadProgress(Slot) && AdventureSnapshot(World,Hero);
        auto* Restored=AdventurePet(World,Hero);Pass &= Restored && Restored->PersistentId==Id;
        UE_LOG(LogTemp,Display,TEXT("%s %s travel=%.1f petSouth=%.1f harvested=%d skill=%d incoming=%d"),ResumedCheckpoint?TEXT("CF_HOLLOW_DOWNED_RESUME"):Mode==3?TEXT("CF_HOLLOW_RECOVERY"):Mode==1?TEXT("CF_HOLLOW_ADVENTURE_WRITE"):TEXT("CF_HOLLOW_ADVENTURE"),Pass?TEXT("PASS"):TEXT("FAIL"),Travel,PetSouth,Harvested,Skill,Incoming);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
};
void StartAdventure(UWorld* World,ACFPlayerCharacter* Hero,ACreatureCharacter* Pet,AWildEncounter* Camp,int Mode,bool WriteDowned=false,bool Resumed=false)
{
    auto State=MakeShared<FAdventure>();State->World=World;State->Hero=Hero;State->Pet=Pet;State->Enemy=Camp->GetResident();State->Camp=Camp;State->Mode=Mode;
    State->WriteDownedCheckpoint=WriteDowned;State->ResumedCheckpoint=Resumed;
    State->Input=NewObject<UHomeMovementProbe>(Hero);State->Input->RegisterComponent();State->Points=MakeHollowRoutePoints();State->Previous=Hero->GetActorLocation();State->PreviousPet=Pet->GetActorLocation();State->Deadline=FPlatformTime::Seconds()+(Mode==3?180:75);
    if(Mode==3 && !Resumed && FParse::Param(FCommandLine::Get(),TEXT("CFHollowCornerProbe")))
    {
        // 限定诊断的初始夹具取自失败位置；不冒充原路线重放或正常通行验收。
        // 此后不传送、不停AI、不关闭碰撞，沿原第六目标持续输入并记录真实阻挡。
        Hero->SetActorLocation(FVector(167.180,-3219.343,40));Pet->SetActorLocation(FVector(145.985,-3154.643,40));
        State->Index=6;State->Previous=Hero->GetActorLocation();State->PreviousPet=Pet->GetActorLocation();
        UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_CORNER_PROBE fixture=1 initialPlacementOnly=1"));
    }
    if(!Resumed && (!CFInputQA::Press(Hero,EKeys::LeftShift) || !Hero->Pace->IsSlowWalking())){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    if(FParse::Param(FCommandLine::Get(),TEXT("CFHollowAdventurePreview")))
    {State->Shots=FPaths::ProjectSavedDir()/TEXT("HollowAdventurePreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits);IFileManager::Get().MakeDirectory(*State->Shots,true);UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_ADVENTURE_ROOT %s"),*State->Shots);}
    UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_ADVENTURE_START resident=%s hp=%.1f petHP=%.1f"),*State->Enemy->GetActorLocation().ToString(),State->Enemy->Health,Pet->Health);
    // 显式负向诊断用于验证失败快照路径；必定失败，不能计入正常恢复通过。
    if(FParse::Param(FCommandLine::Get(),TEXT("CFHollowFailureProbe")))
    {State->Fail(TEXT("diagnostic-negative-control"));return;}
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float){return State->Tick();}),.1f);
}
}
void RunHollowAdventure(UWorld* World,ACFPlayerCharacter* Hero,int Mode)
{
    if(FParse::Param(FCommandLine::Get(),TEXT("CFHollowInspectFailure")))
    {
        // 只读取隔离输入并查询恢复后的静态现场；不继续冒险、不写档，不冒充瞬态重放。
        const FString Slot=TEXT("HollowFailureInspectInput");
        const auto* Input=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0));
        bool Ready=Input && Input->FormatVersion==FrontierCurrentSaveVersion && Hero->LoadProgress(Slot) &&
            Hero->Health==Input->Health && Hero->GetActorLocation().Equals(Input->PlayerTransform.GetLocation(),.01);
        auto* Pet=Ready?AdventurePet(World,Hero):nullptr;auto* Camp=Ready?HollowCamp(World):nullptr;auto* Enemy=Camp?Camp->GetResident():nullptr;
        Ready &= Pet && Enemy;
        if(!Ready){UE_LOG(LogTemp,Error,TEXT("CF_HOLLOW_INSPECT FAIL load"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
        UGameplayStatics::SetGamePaused(World,true);
        for(auto* Actor:{Pet,Enemy})
        {
            const auto* Record=Input->Creatures.FindByPredicate([Actor](const FCreatureSaveRecord& R){return R.Id==Actor->PersistentId;});
            Ready &= Record && Actor->GetActorLocation().Equals(Record->Transform.GetLocation(),.01) && Actor->Health==Record->Health;
            UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_INSPECT_ACTOR owned=%d id=%s location=%s hp=%.1f activity=%d savedActivity=%d focus=%s actor=%s radius=%.2f"),
                Actor==Pet,*Actor->PersistentId.ToString(),*Actor->GetActorLocation().ToString(),Actor->Health,static_cast<int32>(Actor->Activity),Record?static_cast<int32>(Record->Activity):-1,*GetNameSafe(Actor->FocusTarget),*Actor->GetName(),Actor->GetCapsuleComponent()->GetScaledCapsuleRadius());
        }
        auto QueryPath=[World](const TCHAR* Label,AActor* Actor,const FVector& Goal)
        {
            auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Actor->GetActorLocation(),Goal,Actor);
            UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_INSPECT_PATH label=%s valid=%d partial=%d points=%d length=%.1f from=%s goal=%s"),Label,Path && Path->IsValid(),Path && Path->IsPartial(),Path?Path->PathPoints.Num():0,Path?Path->GetPathLength():0.,*Actor->GetActorLocation().ToString(),*Goal.ToString());
        };
        const FVector Goal=MakeHollowRoutePoints()[6];
        QueryPath(TEXT("hero-route6"),Hero,Goal);QueryPath(TEXT("hero-enemy"),Hero,Enemy->GetActorLocation());QueryPath(TEXT("pet-enemy"),Pet,Enemy->GetActorLocation());
        FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(HollowInspect),false,Hero);const auto* Capsule=Hero->GetCapsuleComponent();
        const FVector Direction=(Goal-Hero->GetActorLocation()).GetSafeNormal2D();
        const bool Blocked=World->SweepSingleByChannel(Hit,Hero->GetActorLocation(),Hero->GetActorLocation()+Direction*100,FQuat::Identity,ECC_Pawn,
            FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight()),Query);
        UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_INSPECT_SWEEP blocked=%d actor=%s component=%s impact=%s pet=%d penetrating=%d depth=%.3f heroRadius=%.2f distance=%.3f"),Blocked,*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()),*Hit.ImpactPoint.ToString(),Hit.GetActor()==Pet,Hit.bStartPenetrating,Hit.PenetrationDepth,Capsule->GetScaledCapsuleRadius(),Hero->GetDistanceTo(Pet));
        FHitResult Sight;World->LineTraceSingleByChannel(Sight,Hero->FirstPersonCamera->GetComponentLocation(),Enemy->GetActorLocation(),ECC_Visibility,Query);
        UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_INSPECT_SIGHT actor=%s component=%s hero=%s hp=%.1f control=%s camera=%s"),*GetNameSafe(Sight.GetActor()),*GetNameSafe(Sight.GetComponent()),*Hero->GetActorLocation().ToString(),Hero->Health,*Hero->GetController()->GetControlRotation().ToString(),*Hero->FirstPersonCamera->GetComponentRotation().ToString());
        UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_INSPECT %s snapshotOnly=1 transientReplay=0"),Ready?TEXT("PASS"):TEXT("FAIL"));
        if(Ready && FParse::Param(FCommandLine::Get(),TEXT("CFHollowInspectDynamic")))
        {
            // 快照不保存集火与路径瞬态。此处显式重建假设，仅作受控观察，不冒充原失败重放。
            Pet->FocusTarget=Enemy;
            if(FParse::Param(FCommandLine::Get(),TEXT("CFHollowDetourTrace")))
            {
                // 从实际胶囊中心检查退让短段，不用导航投影后的起点替代真实起点。
                const FVector Origin=Pet->GetActorLocation();const auto* Shape=Pet->GetCapsuleComponent();
                auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
                for(int32 I=0;I<32;++I)
                {
                    const float Angle=(I%16)*PI/8;const float Radius=I<16?120.f:240.f;
                    const FVector End=Origin+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*Radius;
                    FHitResult RetreatHit;FCollisionQueryParams RetreatQuery(SCENE_QUERY_STAT(HollowRetreatProbe),false,Pet);
                    const bool RetreatBlocked=World->SweepSingleByChannel(RetreatHit,Origin,End,FQuat::Identity,ECC_Pawn,
                        FCollisionShape::MakeCapsule(Shape->GetScaledCapsuleRadius(),Shape->GetScaledCapsuleHalfHeight()),RetreatQuery);
                    FNavLocation Projected;
                    const bool OnNav=Nav && Nav->ProjectPointToNavigation(End,Projected,FVector(40,40,120));
                    UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_RETREAT_PROBE index=%d blocked=%d actor=%s onNav=%d end=%s projected=%s"),
                        I,RetreatBlocked,*GetNameSafe(RetreatHit.GetActor()),OnNav,*End.ToString(),OnNav?*Projected.Location.ToString():TEXT("none"));
                }
            }
            auto* Movement=NewObject<UHomeMovementProbe>(Hero);Movement->RegisterComponent();
            UGameplayStatics::SetGamePaused(World,false);
            const double Started=FPlatformTime::Seconds();const FVector Start=Hero->GetActorLocation();
            const bool WaitForPartner=FParse::Param(FCommandLine::Get(),TEXT("CFHollowInspectWait"));
            const bool CancelRetreat=FParse::Param(FCommandLine::Get(),TEXT("CFHollowCancelRetreat"));
            auto Cancelled=MakeShared<bool>(false);
            UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_DYNAMIC_BEGIN reconstructedFocus=1 deterministicReplay=0 waitForPartner=%d"),WaitForPartner);
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([World,Hero,Pet,Enemy,Movement,Goal,Started,Start,WaitForPartner,CancelRetreat,Cancelled](float)
            {
                if(!IsValid(Hero) || !IsValid(Pet) || !IsValid(Enemy))
                {UE_LOG(LogTemp,Error,TEXT("CF_HOLLOW_DYNAMIC FAIL actors"));FPlatformMisc::RequestExitWithStatus(false,1);return false;}
                const FVector Now=Hero->GetActorLocation();
                if(CancelRetreat && !*Cancelled && Pet->HasCombatRoute())
                {
                    const bool Pass=CFInputQA::Press(Hero,EKeys::F8) && Pet->bAvoidCombat && !Pet->FocusTarget && !Pet->HasCombatRoute();
                    UE_LOG(LogTemp,Display,TEXT("CF_RETREAT_CANCEL %s activeBefore=1 clearedImmediately=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Pass);
                    if(!Pass){FPlatformMisc::RequestExitWithStatus(false,1);return false;}
                    *Cancelled=true;
                }
                Movement->Direction=WaitForPartner || FVector::Dist2D(Now,Goal)<70?FVector::ZeroVector:(Goal-Now).GetSafeNormal2D();
                if(WaitForPartner)
                {
                    const auto View=(Enemy->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
                    Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
                }
                else if(!Movement->Direction.IsNearlyZero())
                {Hero->SetActorRotation(Movement->Direction.Rotation());Hero->GetController()->SetControlRotation(Movement->Direction.Rotation());}
                FHitResult Block;FCollisionQueryParams Params(SCENE_QUERY_STAT(HollowDynamic),false,Hero);
                const auto* Shape=Hero->GetCapsuleComponent();
                World->SweepSingleByChannel(Block,Now,Now+Movement->Direction*100,FQuat::Identity,ECC_Pawn,
                    FCollisionShape::MakeCapsule(Shape->GetScaledCapsuleRadius(),Shape->GetScaledCapsuleHalfHeight()),Params);
                const auto* AI=Cast<AAIController>(Pet->GetController());
                const FVector Segment=AI?AI->GetImmediateMoveDestination():Pet->GetActorLocation();
                FHitResult PetHit;FCollisionQueryParams PetQuery(SCENE_QUERY_STAT(HollowDynamicPet),false,Pet);
                const auto* PetShape=Pet->GetCapsuleComponent();const FVector PetPosition=Pet->GetActorLocation();
                World->SweepSingleByChannel(PetHit,PetPosition,PetPosition+(Segment-PetPosition).GetSafeNormal2D()*100,FQuat::Identity,ECC_Pawn,
                    FCollisionShape::MakeCapsule(PetShape->GetScaledCapsuleRadius(),PetShape->GetScaledCapsuleHalfHeight()),PetQuery);
                UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_DYNAMIC_PATH moveStatus=%d segment=%s blocker=%s component=%s heroBlock=%d penetrating=%d depth=%.3f"),
                    AI?static_cast<int32>(AI->GetMoveStatus()):-1,*Segment.ToString(),*GetNameSafe(PetHit.GetActor()),*GetNameSafe(PetHit.GetComponent()),PetHit.GetActor()==Hero,PetHit.bStartPenetrating,PetHit.PenetrationDepth);
                UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_DYNAMIC_SAMPLE time=%.2f hero=%s velocity=%s pet=%s petVelocity=%s activity=%d focus=%s blocker=%s petBlock=%d heroHP=%.1f petHP=%.1f enemyHP=%.1f distance=%.1f"),
                    FPlatformTime::Seconds()-Started,*Now.ToString(),*Hero->GetVelocity().ToString(),*Pet->GetActorLocation().ToString(),*Pet->GetVelocity().ToString(),
                    static_cast<int32>(Pet->Activity),*GetNameSafe(Pet->FocusTarget),*GetNameSafe(Block.GetActor()),Block.GetActor()==Pet,Hero->Health,Pet->Health,Enemy->Health,FVector::Dist2D(Now,Goal));
                if(FPlatformTime::Seconds()-Started<15)return true;
                if(CancelRetreat)
                {
                    const bool Pass=*Cancelled && !Pet->HasCombatRoute() && !Pet->FocusTarget && Pet->bAvoidCombat && Enemy->Health==Enemy->GetMaxHealth();
                    UE_LOG(LogTemp,Display,TEXT("CF_RETREAT_CANCEL_FINAL %s enemyUnhurt=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Enemy->Health==Enemy->GetMaxHealth());
                    if(!Pass){FPlatformMisc::RequestExitWithStatus(false,1);return false;}
                }
                Movement->Direction=FVector::ZeroVector;
                UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_DYNAMIC OBSERVED displacement=%.1f distance=%.1f reconstructedFocus=1 deterministicReplay=0"),FVector::Dist2D(Start,Now),FVector::Dist2D(Now,Goal));
                FPlatformMisc::RequestExitWithStatus(false,0);return false;
            }),.5f);
            return;
        }
        FPlatformMisc::RequestExitWithStatus(false,Ready?0:1);return;
    }
    if(Mode==2 || Mode==4 || Mode==7)
    {const bool Pass=Hero->LoadProgress(Mode==7?TEXT("HollowCheckpointFinal"):Mode==4?TEXT("HollowRecoveryCross"):TEXT("HollowAdventureCross")) && AdventureSnapshot(World,Hero);UE_LOG(LogTemp,Display,TEXT("%s %s"),Mode==7?TEXT("CF_HOLLOW_CHECKPOINT_READ"):Mode==4?TEXT("CF_HOLLOW_RECOVERY_READ"):TEXT("CF_HOLLOW_ADVENTURE_READ"),Pass?TEXT("PASS"):TEXT("FAIL"));FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return;}
    if(Mode==6)
    {
        // 先保留磁盘中的期望值，再通过正常读档事务恢复，不重新赠送夹具物品或伙伴。
        const auto* Snapshot=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("HollowDownedCross"),0));
        const bool Pass=Snapshot && Hero->LoadProgress(TEXT("HollowDownedCross")) && DownedCheckpointMatches(World,Hero,Snapshot);
        UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_DOWNED_LOAD %s"),Pass?TEXT("PASS"):TEXT("FAIL"));
        if(!Pass){FPlatformMisc::RequestExitWithStatus(false,1);return;}
        StartAdventure(World,Hero,AdventurePet(World,Hero),HollowCamp(World),3,false,true);return;
    }
    const bool WriteDowned=Mode==5;if(WriteDowned)Mode=3;
    auto* Camp=HollowCamp(World);auto* Enemy=Camp?Camp->GetResident():nullptr;auto* Stone=LoadObject<UCreatureDefinition>(nullptr,TEXT("/Game/Creatures/DA_Craglet.DA_Craglet"));
    if(!Camp || !Enemy || !Stone || Camp->bBoss || Enemy->Level!=3 || !Camp->IsActorTickEnabled() || AdventurePet(World,Hero))
    {UE_LOG(LogTemp,Error,TEXT("CF_HOLLOW_ADVENTURE FAIL natural-population"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    // 仅设置出发夹具；途中保留所有自然住民，不造敌人、不直接扣血或发奖励。
    Hero->SetActorLocation(FVector(-250,-400,40));Hero->Inventory->Restore({{EFrontierItem::Material,4},{EFrontierItem::Crystal,1},{EFrontierItem::Food,9}});
    if(Mode==3)Hero->Health=18;
    FResearchProgress Study;Study.bCrystalSample=true;Study.bCrystalStudy=true;Hero->Research->Restore(Study);
    const FTransform At(Hero->GetActorLocation()+FVector(0,180,0));auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),At);
    if(!Pet){FPlatformMisc::RequestExitWithStatus(false,1);return;}Pet->Definition=Stone;Pet->FinishSpawning(At);Pet->Tame(Hero->GetController());
    StartAdventure(World,Hero,Pet,Camp,Mode,WriteDowned);
}
