#include "RescueScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "InventoryComponent.h"
#include "InputRoutingQA.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "HollowScenario.h"
#include "HomeScenario.h"
#include "CreatureDefinition.h"
#include "WildEncounter.h"
#include "ResourceNode.h"
#include "FrontierSaveGame.h"
#include "PlayerPaceComponent.h"
#include "Containers/Ticker.h"
#include "GameFramework/Controller.h"
#include "WorldLoot.h"
#include "BaseBuildingComponent.h"
#include "Algo/Reverse.h"
#include "CFHUD.h"
#include "GameFramework/PlayerController.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Components/CapsuleComponent.h"

namespace
{
FVector RescueWalkingDirection(UWorld* World,ACFPlayerCharacter* Hero,const FVector& Goal,bool& Avoided)
{
    const FVector Toward=(Goal-Hero->GetActorLocation()).GetSafeNormal2D();const auto* Capsule=Hero->GetCapsuleComponent();
    const auto Shape=FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight()-2);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(RescueLocalWalking),false,Hero);
    for(float Angle:{0.f,-45.f,45.f,-90.f,90.f,-135.f,135.f,180.f})
    {
        const FVector Direction=FRotator(0,Angle,0).RotateVector(Toward);FHitResult Hit;
        const bool Blocked=World->SweepSingleByChannel(Hit,Hero->GetActorLocation(),Hero->GetActorLocation()+Direction*100,FQuat::Identity,ECC_Pawn,Shape,Params);
        if(!Blocked || (Hit.bStartPenetrating && FVector::DotProduct(Direction,Hit.Normal)>.25f))
        {Avoided |= Angle!=0;return Direction;}
    }
    Avoided=true;return FVector::ZeroVector;
}

void ReplayRescueReturn(UWorld* World,ACFPlayerCharacter* Hero)
{
    if(!Hero->LoadProgress(TEXT("RescueReturnFailure"))){UE_LOG(LogTemp,Error,TEXT("CF_RESCUE_RETURN_REPLAY FAIL load"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    auto* Input=NewObject<UHomeMovementProbe>(Hero);Input->RegisterComponent();CFInputQA::Press(Hero,EKeys::LeftShift);
    const FVector Goal(260,-3250,40);const double Deadline=FPlatformTime::Seconds()+10;
    const bool Direct=FParse::Param(FCommandLine::Get(),TEXT("CFRescueReturnDirect"));
    FHitResult InitialHit;FCollisionQueryParams InitialParams(SCENE_QUERY_STAT(RescueReturnBaseline),false,Hero);const auto* Capsule=Hero->GetCapsuleComponent();
    World->SweepSingleByChannel(InitialHit,Hero->GetActorLocation(),Hero->GetActorLocation()+(Goal-Hero->GetActorLocation()).GetSafeNormal2D()*100,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight()-2),InitialParams);
    const auto* InitialCreature=Cast<ACreatureCharacter>(InitialHit.GetActor());const bool DeadBlocker=InitialCreature && InitialCreature->Health==0;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([World,Hero,Input,Goal,Deadline,Direct,DeadBlocker,Avoided=false,Travel=0.f,Previous=Hero->GetActorLocation()](float) mutable
    {
        const float Step=FVector::Dist2D(Hero->GetActorLocation(),Previous);Travel+=Step;Previous=Hero->GetActorLocation();
        const bool Arrived=FVector::Dist2D(Hero->GetActorLocation(),Goal)<70;
        if(Arrived || FPlatformTime::Seconds()>Deadline || Hero->Health<=0 || Step>160 || Hero->GetActorLocation().Z<0)
        {
            if(Direct)
            {Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::LeftShift);const bool Blocked=DeadBlocker && !Arrived && Travel<100 && Hero->Health>0;UE_LOG(LogTemp,Display,TEXT("CF_RESCUE_RETURN_BASELINE %s deadBlocker=%d travel=%.1f"),Blocked?TEXT("BLOCKED"):TEXT("FAIL"),DeadBlocker,Travel);FPlatformMisc::RequestExitWithStatus(false,Blocked?0:1);return false;}
            Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::LeftShift);const bool Pass=Arrived && Avoided && Travel>300 && Hero->Health>0 && Step<=160 && Hero->GetActorLocation().Z>=0 && FPlatformTime::Seconds()<=Deadline && Hero->GetCapsuleComponent()->GetCollisionEnabled()!=ECollisionEnabled::NoCollision;
            UE_LOG(LogTemp,Display,TEXT("CF_RESCUE_RETURN_REPLAY %s avoided=%d travel=%.1f hero=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Avoided,Travel,*Hero->GetActorLocation().ToString());FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
        }
        Input->Direction=Direct?(Goal-Hero->GetActorLocation()).GetSafeNormal2D():RescueWalkingDirection(World,Hero,Goal,Avoided);Hero->GetController()->SetControlRotation(Input->Direction.Rotation());return true;
    }),.1f);
}

struct FRescueShots
{
    FString Root,Aiming;TSet<FString> Taken;uint64 LastFrame=MAX_uint64,AimFrame=0;
    bool Ready(ACFPlayerCharacter* Hero,ACreatureCharacter* Pet,const TCHAR* Name)
    {
        if(Root.IsEmpty())return true;
        if(Taken.Contains(Name))return !FScreenshotRequest::IsScreenshotRequested();
        const auto View=(Pet->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
        auto* PC=Cast<APlayerController>(Hero->GetController());auto* HUD=PC?Cast<ACFHUD>(PC->GetHUD()):nullptr;
        if(!HUD || FScreenshotRequest::IsScreenshotRequested())return false;
        const auto& Audit=HUD->GetLayoutAudit();if(!Audit.bFits || !Audit.bNoOverlap || !Audit.bCenterClear || Audit.Frame==LastFrame)return false;
        if(Aiming!=Name){Aiming=Name;AimFrame=Audit.Frame;return false;}if(Audit.Frame<=AimFrame)return false;
        // 请求前保持一个布局帧，让真实镜头与身体姿态进入渲染；不暂停世界。
        LastFrame=Audit.Frame;Taken.Add(Name);FScreenshotRequest::RequestScreenshot(Root/(FString(Name)+TEXT(".png")),true,false);
        UE_LOG(LogTemp,Display,TEXT("CF_RESCUE_SHOT %s viewport=%.0fx%.0f frame=%llu"),Name,Audit.Viewport.X,Audit.Viewport.Y,Audit.Frame);return false;
    }
};
}

void RunRescueGateScenario(UWorld* World, ACFPlayerCharacter* Hero)
{
    // 隔离验证输入/消耗事务；直接伤害夹具不是自然战斗证据。
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)It->Destroy();
    Hero->SetActorLocation(FVector(-450,0,40));Hero->GetCharacterMovement()->DisableMovement();
    auto* Pet=World->SpawnActor<ACreatureCharacter>(FVector(-450,180,40),FRotator::ZeroRotator);
    if(!Pet){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    Pet->Tame(Hero->GetController());Pet->SetActorTickEnabled(false);Pet->GetCharacterMovement()->DisableMovement();
    UGameplayStatics::ApplyDamage(Pet,1000,nullptr,Hero,nullptr);
    const FGuid Id=Pet->PersistentId;const FVector Position=Pet->GetActorLocation();
    auto Aim=[&](){const auto View=(Pet->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);};
    auto Bag=[&](int32 Medicine){Hero->Inventory->Restore({{EFrontierItem::Medicine,Medicine},{EFrontierItem::Material,7}});};
    auto Press=[&](){return CFInputQA::Press(Hero,EKeys::P);};
    Aim();Bag(0);
    const bool NoMedicine=Hero->TraceCreature()==Pet && Press() && Pet->Health==0 && Hero->Inventory->Count(EFrontierItem::Medicine)==0;
    Bag(3);Hero->Health=0;
    const bool OwnerDown=Press() && Pet->Health==0 && Hero->Inventory->Count(EFrontierItem::Medicine)==3;Hero->Health=100;
    // 实体墙阻挡真实交互射线，不用篡改目标返回值。
    auto* Wall=World->SpawnActor<AActor>();auto* Mesh=NewObject<UStaticMeshComponent>(Wall);Wall->SetRootComponent(Mesh);Mesh->RegisterComponent();
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Mesh->SetCollisionProfileName(TEXT("BlockAll"));Wall->SetActorLocation(FVector(-450,90,70));Wall->SetActorScale3D(FVector(2,.2,2));
    const bool Cover=Hero->TraceCreature()!=Pet && Press() && Pet->Health==0 && Hero->Inventory->Count(EFrontierItem::Medicine)==3;
    Wall->Destroy();
    Pet->Tamer=nullptr;
    const bool Ownership=Press() && Pet->Health==0 && Hero->Inventory->Count(EFrontierItem::Medicine)==3;Pet->Tamer=Hero->GetController();
    const bool Saved=Hero->SaveProgress(TEXT("RescueGateDead")) && Hero->LoadProgress(TEXT("RescueGateDead"));
    Pet=nullptr;for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->PersistentId==Id)Pet=*It;
    if(!Pet){UE_LOG(LogTemp,Error,TEXT("CF_RESCUE_GATES FAIL restore"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    Pet->SetActorTickEnabled(false);Pet->GetCharacterMovement()->DisableMovement();Aim();
    const bool Restored=Saved && Pet->Health==0 && Pet->GetActorLocation().Equals(Position,.1f) && Hero->Inventory->Count(EFrontierItem::Medicine)==3;
    const bool Treatment=Hero->TraceCreature()==Pet && Press() && Pet->Health==40 && Pet->PersistentId==Id && Pet->GetActorLocation().Equals(Position,.1f) && Hero->Inventory->Count(EFrontierItem::Medicine)==2 && Hero->Inventory->Count(EFrontierItem::Material)==7;
    Pet->Health=Pet->GetMaxHealth();
    const bool Full=Press() && Pet->Health==Pet->GetMaxHealth() && Hero->Inventory->Count(EFrontierItem::Medicine)==2;
    const bool Pass=NoMedicine && OwnerDown && Cover && Ownership && Restored && Treatment && Full;
    UE_LOG(LogTemp,Display,TEXT("CF_RESCUE_GATES %s noMedicine=%d ownerDown=%d cover=%d ownership=%d restored=%d treatment=%d full=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),NoMedicine,OwnerDown,Cover,Ownership,Restored,Treatment,Full);
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}

void RunNaturalRescueCheckpoint(UWorld* World, ACFPlayerCharacter* Hero)
{
    AWildEncounter* Camp=nullptr;for(TActorIterator<AWildEncounter> It(World);It;++It)if(It->EncounterId==TEXT("EmberHollow"))Camp=*It;
    auto* Enemy=Camp?Camp->GetResident():nullptr;auto* Definition=LoadObject<UCreatureDefinition>(nullptr,TEXT("/Game/Creatures/DA_Craglet.DA_Craglet"));
    if(!Enemy || !Definition || Enemy->Level!=3){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    // 仅出发时设为已负伤伙伴；途中完全使用自然住民的AI与伤害。
    Hero->SetActorLocation(FVector(-250,-400,40));Hero->Inventory->Restore({{EFrontierItem::Medicine,1},{EFrontierItem::Material,7},{EFrontierItem::Food,9}});
    const FTransform At(Hero->GetActorLocation()+FVector(0,180,0));auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),At);
    if(!Pet){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    Pet->Definition=Definition;Pet->FinishSpawning(At);Pet->Tame(Hero->GetController());Pet->Health=20;
    auto* Input=NewObject<UHomeMovementProbe>(Hero);Input->RegisterComponent();
    auto Points=MakeHollowRoutePoints();const double Deadline=FPlatformTime::Seconds()+100;
    if(!CFInputQA::Press(Hero,EKeys::LeftShift)){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([World,Hero,Pet,Enemy,Camp,Input,Points,Deadline,Index=0,Previous=Hero->GetActorLocation(),Travel=0.f,Engaged=false](float) mutable
    {
        const FVector Now=Hero->GetActorLocation();const float Step=FVector::Dist2D(Now,Previous);Travel+=Step;Previous=Now;
        if(FPlatformTime::Seconds()>Deadline || Hero->Health<=0 || Step>160 || !Enemy->IsActorTickEnabled() || !Pet->IsActorTickEnabled() || Camp->GetResident()!=Enemy)
        {UE_LOG(LogTemp,Error,TEXT("CF_RESCUE_NATURAL_WRITE FAIL index=%d heroHP=%.1f petHP=%.1f enemyHP=%.1f"),Index,Hero->Health,Pet->Health,Enemy->Health);FPlatformMisc::RequestExitWithStatus(false,1);return false;}
        if(Pet->Health<=0)
        {
            Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::LeftShift);
            AResourceNode* Cache=nullptr;for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,11))Cache=*It;
            const bool Pass=Engaged && Travel>2000 && Now.Y<-2700 && Enemy->Health>0 && Enemy->Health<Enemy->GetMaxHealth() && Cache && Cache->Remaining==6 && Hero->Inventory->Count(EFrontierItem::Medicine)==1 && Hero->Inventory->Count(EFrontierItem::Material)==7 && Hero->SaveProgress(TEXT("RescueNaturalDowned"));
            UE_LOG(LogTemp,Display,TEXT("CF_RESCUE_NATURAL_WRITE %s travel=%.1f heroHP=%.1f petHP=%.1f enemyHP=%.1f pet=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Travel,Hero->Health,Pet->Health,Enemy->Health,*Pet->PersistentId.ToString());FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
        }
        if(Now.Y<-2700 && Enemy->Disposition==ECreatureDisposition::Hostile)
        {
            Engaged=true;Input->Direction=FVector::ZeroVector;const auto View=(Enemy->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
            if(Hero->TraceCreature()==Enemy && Pet->FocusTarget!=Enemy)CFInputQA::Press(Hero,EKeys::Q);
            return true;
        }
        if(Index<Points.Num())
        {if(FVector::Dist2D(Now,Points[Index])<70){++Index;Input->Direction=FVector::ZeroVector;}else{Input->Direction=(Points[Index]-Now).GetSafeNormal2D();Hero->GetController()->SetControlRotation(Input->Direction.Rotation());}}
        return true;
    }),.1f);
}

void RunNaturalRescueResume(UWorld* World, ACFPlayerCharacter* Hero, bool ReadFinal)
{
    if(FParse::Param(FCommandLine::Get(),TEXT("CFRescueReturnReplay"))){ReplayRescueReturn(World,Hero);return;}
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("RescueNaturalDowned"),0));
    const auto* Record=Source?Source->Creatures.FindByPredicate([](const auto& R){return R.bOwned && R.Health==0;}):nullptr;
    if(!Record || !Hero->LoadProgress(ReadFinal?TEXT("RescueNaturalFinal"):TEXT("RescueNaturalDowned")))
    {UE_LOG(LogTemp,Error,TEXT("CF_RESCUE_NATURAL_LOAD FAIL source"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    ACreatureCharacter* Pet=nullptr;AWildEncounter* Camp=nullptr;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->PersistentId==Record->Id)Pet=*It;
    for(TActorIterator<AWildEncounter> It(World);It;++It)if(It->EncounterId==TEXT("EmberHollow"))Camp=*It;
    auto Snapshot=[World,Hero,Pet,Camp]()
    {
        int32 Materials=Hero->Inventory->Count(EFrontierItem::Material);for(TActorIterator<AWorldLoot> It(World);It;++It)Materials+=It->Materials;
        AResourceNode* Cache=nullptr;for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,11))Cache=*It;
        return Pet && Pet->Health>0 && Pet->Tamer==Hero->GetController() && Pet->GetDistanceTo(Hero)<300 && Hero->Health>0 &&
            FVector::Dist2D(Hero->GetActorLocation(),UBaseBuildingComponent::BaseOrigin())<800 && Camp && !Camp->GetResident() && Cache && Cache->Remaining==6 &&
            Materials==9 && Hero->Inventory->Count(EFrontierItem::Medicine)==0 && Hero->Inventory->Count(EFrontierItem::Food)==9;
    };
    if(ReadFinal){const bool Pass=Snapshot();UE_LOG(LogTemp,Display,TEXT("CF_RESCUE_NATURAL_READ %s"),Pass?TEXT("PASS"):TEXT("FAIL"));FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return;}
    auto* Enemy=Camp?Camp->GetResident():nullptr;const auto* EnemyRecord=Enemy?Source->Creatures.FindByPredicate([Enemy](const auto& R){return R.Id==Enemy->PersistentId;}):nullptr;
    bool Match=Pet && EnemyRecord && Pet->Health==0 && Pet->GetActorLocation().Equals(Record->Transform.GetLocation(),.1f) && Pet->Stamina==Record->Stamina && Pet->Nutrition==Record->Nutrition &&
        Enemy->Health==EnemyRecord->Health && Enemy->GetActorLocation().Equals(EnemyRecord->Transform.GetLocation(),.1f) && Hero->Health==Source->Health && Hero->GetActorLocation().Equals(Source->PlayerTransform.GetLocation(),.1f);
    for(int32 I=0;I<static_cast<int32>(EFrontierItem::Count);++I)Match &= Hero->Inventory->Count(static_cast<EFrontierItem>(I))==Source->InventoryStacks.FindRef(static_cast<EFrontierItem>(I));
    UE_LOG(LogTemp,Display,TEXT("CF_RESCUE_NATURAL_LOAD %s"),Match?TEXT("PASS"):TEXT("FAIL"));
    if(!Match){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    if(FParse::Param(FCommandLine::Get(),TEXT("CFRescueReturnProbe")))
    {
        // 仅诊断已记录的卡住位置；不保存、不把放置角色当作自然路线通过。
        Hero->SetActorLocation(FVector(-224.928,-3184.078,40.15));Hero->GetCharacterMovement()->DisableMovement();
        const FVector Start=Hero->GetActorLocation(),Target(260,-3250,40);
        auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Start,Target,Hero);
        if(!Path || !Path->IsValid()){UE_LOG(LogTemp,Error,TEXT("CF_RESCUE_RETURN_PROBE_ONCE FAIL path"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
        for(int32 I=0;I<Path->PathPoints.Num();++I)UE_LOG(LogTemp,Display,TEXT("CF_RESCUE_RETURN_PROBE_POINT index=%d position=%s"),I,*Path->PathPoints[I].ToString());
        FVector Goal=Target;for(const auto& Point:Path->PathPoints)if(FVector::Dist2D(Start,Point)>55){Goal=Point;break;}
        const auto* Capsule=Hero->GetCapsuleComponent();const auto Shape=FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight()-2);
        for(bool IgnoreCreatures:{false,true})
        {
            FCollisionQueryParams Params(SCENE_QUERY_STAT(RescueReturnProbeOnce),false,Hero);if(IgnoreCreatures)for(TActorIterator<ACreatureCharacter> It(World);It;++It)Params.AddIgnoredActor(*It);
            FHitResult Hit;World->SweepSingleByChannel(Hit,Start,Start+(Goal-Start).GetSafeNormal2D()*100,FQuat::Identity,ECC_Pawn,Shape,Params);
            UE_LOG(LogTemp,Display,TEXT("CF_RESCUE_RETURN_PROBE_ONCE ignoreCreatures=%d hero=%s goal=%s hit=%s component=%s normal=%s penetration=%d depth=%.2f"),IgnoreCreatures,*Start.ToString(),*Goal.ToString(),*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()),*Hit.Normal.ToString(),Hit.bStartPenetrating,Hit.PenetrationDepth);
        }
        UE_LOG(LogTemp,Display,TEXT("CF_RESCUE_RETURN_PROBE_ONCE DONE"));FPlatformMisc::RequestExitWithStatus(false,0);return;
    }
    auto* Input=NewObject<UHomeMovementProbe>(Hero);Input->RegisterComponent();auto Points=MakeHollowRoutePoints();Algo::Reverse(Points);Points.Add(FVector(-250,-400,40));
    // 先到最近的原路线点，后沿原折返通道返家，不把伙伴移回基地。
    int32 Nearest=0;for(int32 I=1;I<Points.Num();++I)if(FVector::Dist2D(Hero->GetActorLocation(),Points[I])<FVector::Dist2D(Hero->GetActorLocation(),Points[Nearest]))Nearest=I;
    auto Shots=MakeShared<FRescueShots>();
    if(FParse::Param(FCommandLine::Get(),TEXT("CFRescuePreview")))
    {Shots->Root=FPaths::ProjectSavedDir()/TEXT("RescuePreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits);IFileManager::Get().MakeDirectory(*Shots->Root,true);UE_LOG(LogTemp,Display,TEXT("CF_RESCUE_SHOT_ROOT %s"),*Shots->Root);}
    const double Deadline=FPlatformTime::Seconds()+120;CFInputQA::Press(Hero,EKeys::LeftShift);
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([World,Hero,Pet,Enemy,Camp,Input,Points,Snapshot,Shots,Deadline,Index=Nearest,Stage=0,Skill=false,Travel=0.f,NextReturnLog=0.,Previous=Pet->GetActorLocation()](float) mutable
    {
        const float Step=FVector::Dist(Pet->GetActorLocation(),Previous);Travel+=Step;Previous=Pet->GetActorLocation();
        if(FPlatformTime::Seconds()>Deadline || Hero->Health<=0 || (Stage>0 && Pet->Health<=0) || Step>160)
        {
            const TCHAR* FailureSlot=Stage==2?TEXT("RescueReturnFailure"):TEXT("RescueBattleFailure");const bool Saved=Hero->SaveProgress(FailureSlot);
            auto* PC=Cast<APlayerController>(Hero->GetController());
            UE_LOG(LogTemp,Error,TEXT("CF_RESCUE_NATURAL_RESUME FAIL stage=%d index=%d heroHP=%.1f petHP=%.1f travel=%.1f hero=%s pet=%s mode=%d ignored=%d velocity=%s input=%s snapshot=%s saved=%d"),Stage,Index,Hero->Health,Pet->Health,Travel,*Hero->GetActorLocation().ToString(),*Pet->GetActorLocation().ToString(),int32(Hero->GetCharacterMovement()->MovementMode),PC?PC->IsMoveInputIgnored():true,*Hero->GetVelocity().ToString(),*Hero->GetLastMovementInputVector().ToString(),FailureSlot,Saved);
            FPlatformMisc::RequestExitWithStatus(false,1);return false;
        }
        auto Aim=[Hero](AActor* Actor){const auto View=(Actor->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);};
        if(Stage==0)
        {
            if(!Shots->Root.IsEmpty() && !Shots->Taken.Contains(TEXT("Restored")) && Hero->GetDistanceTo(Pet)<450)
            {CFInputQA::Release(Hero,EKeys::LeftShift);Input->Direction=(Hero->GetActorLocation()-Pet->GetActorLocation()).GetSafeNormal2D();Aim(Pet);return true;}
            if(!Shots->Root.IsEmpty() && !Hero->Pace->IsSlowWalking())CFInputQA::Press(Hero,EKeys::LeftShift);
            Input->Direction=FVector::ZeroVector;if(!Shots->Ready(Hero,Pet,TEXT("Restored")))return true;
            // 先用真实侧移取得对敌人的视线，避免救起后仍被伙伴胶囊挡住指令射线。
            Aim(Enemy);if(Hero->TraceCreature()!=Enemy){const FVector Toward=(Enemy->GetActorLocation()-Hero->GetActorLocation()).GetSafeNormal2D();bool Avoided=false;Input->Direction=RescueWalkingDirection(World,Hero,Hero->GetActorLocation()+FVector(-Toward.Y,Toward.X,0)*200,Avoided);return true;}
            Aim(Pet);if(Hero->TraceCreature()!=Pet){bool Avoided=false;Input->Direction=RescueWalkingDirection(World,Hero,Pet->GetActorLocation(),Avoided);return true;}
            Input->Direction=FVector::ZeroVector;const FVector Position=Pet->GetActorLocation();
            const bool Pass=CFInputQA::Press(Hero,EKeys::P) && Pet->Health==40 && Pet->GetActorLocation().Equals(Position) && Hero->Inventory->Count(EFrontierItem::Medicine)==0;
            UE_LOG(LogTemp,Display,TEXT("CF_RESCUE_NATURAL_TREAT %s"),Pass?TEXT("PASS"):TEXT("FAIL"));if(!Pass){FPlatformMisc::RequestExitWithStatus(false,1);return false;}
            // 救助不会自动取消休息，按现有提示F恢复行动后才能接受Q/G。
            if(!CFInputQA::Press(Hero,EKeys::F) || Pet->Activity!=ECreatureActivity::Follow){FPlatformMisc::RequestExitWithStatus(false,1);return false;}Stage=1;
        }
        if(Stage==1)
        {
            if(!Shots->Ready(Hero,Pet,TEXT("Rescued")))return true;
            Input->Direction=FVector::ZeroVector;Skill |= Pet->SpecialCooldownRemaining>0;
            if(Enemy->Health>0){Aim(Enemy);if(Hero->TraceCreature()==Enemy){CFInputQA::Press(Hero,EKeys::Q);if(!Skill)CFInputQA::Press(Hero,EKeys::G);}else{const FVector Toward=(Enemy->GetActorLocation()-Hero->GetActorLocation()).GetSafeNormal2D();bool Avoided=false;Input->Direction=RescueWalkingDirection(World,Hero,Hero->GetActorLocation()+FVector(-Toward.Y,Toward.X,0)*200,Avoided);}return true;}
            Stage=2;CFInputQA::Press(Hero,EKeys::F);
        }
        if(Index<Points.Num())
        {
            if(FVector::Dist2D(Hero->GetActorLocation(),Points[Index])<70){++Index;Input->Direction=FVector::ZeroVector;}
            else
            {
                // 拍摄期间的正常后退可能跨过墙角；按真实导航拐点输入行走，不直线顶墙。
                auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Hero->GetActorLocation(),Points[Index],Hero);
                if(!Path || !Path->IsValid() || Path->IsPartial())
                {UE_LOG(LogTemp,Error,TEXT("CF_RESCUE_NATURAL_RESUME FAIL return-path index=%d hero=%s"),Index,*Hero->GetActorLocation().ToString());FPlatformMisc::RequestExitWithStatus(false,1);return false;}
                FVector Goal=Points[Index];for(const auto& Point:Path->PathPoints)if(FVector::Dist2D(Hero->GetActorLocation(),Point)>55){Goal=Point;break;}
                bool Avoided=false;Input->Direction=RescueWalkingDirection(World,Hero,Goal,Avoided);Hero->GetController()->SetControlRotation(Input->Direction.Rotation());
                if(FPlatformTime::Seconds()>NextReturnLog)
                {
                    NextReturnLog=FPlatformTime::Seconds()+5;FHitResult Hit;FCollisionQueryParams Params(SCENE_QUERY_STAT(RescueReturnProbe),false,Hero);
                    const auto* Capsule=Hero->GetCapsuleComponent();World->SweepSingleByChannel(Hit,Hero->GetActorLocation(),Hero->GetActorLocation()+Input->Direction*100,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight()-2),Params);
                    const auto* Blocker=Cast<ACreatureCharacter>(Hit.GetActor());
                    auto* PC=Cast<APlayerController>(Hero->GetController());
                    UE_LOG(LogTemp,Display,TEXT("CF_RESCUE_RETURN_PROBE index=%d hero=%s goal=%s path0=%s path1=%s blocker=%s creatureHP=%.1f mode=%d ignored=%d velocity=%s input=%s"),Index,*Hero->GetActorLocation().ToString(),*Goal.ToString(),*Path->PathPoints[0].ToString(),Path->PathPoints.Num()>1?*Path->PathPoints[1].ToString():TEXT("none"),*GetNameSafe(Hit.GetActor()),Blocker?Blocker->Health:-1.f,int32(Hero->GetCharacterMovement()->MovementMode),PC?PC->IsMoveInputIgnored():true,*Hero->GetVelocity().ToString(),*Hero->GetLastMovementInputVector().ToString());
                }
            }
            return true;
        }
        Input->Direction=FVector::ZeroVector;if(Pet->GetDistanceTo(Hero)>300)return true;
        if(!Shots->Ready(Hero,Pet,TEXT("Home")))return true;
        CFInputQA::Release(Hero,EKeys::LeftShift);const bool Pass=Skill && Travel>2000 && Snapshot() && Hero->SaveProgress(TEXT("RescueNaturalFinal"));
        UE_LOG(LogTemp,Display,TEXT("CF_RESCUE_NATURAL_RESUME %s petHP=%.1f travel=%.1f skill=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Pet->Health,Travel,Skill);FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }),.1f);
}
