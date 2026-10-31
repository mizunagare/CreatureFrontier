#include "ConstructionScenario.h"
#include "CFPlayerCharacter.h"
#include "CFGameMode.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "HomeScenario.h"
#include "HomeLayout.h"
#include "FrontierSaveGame.h"
#include "InventoryComponent.h"
#include "WorkStation.h"
#include "WildEncounter.h"
#include "ResourceNode.h"
#include "ResourceDefinition.h"
#include "InputRoutingQA.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/Ticker.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "BaseBuildingComponent.h"
#include "FarmCropComponent.h"
#include "CraftingCatalog.h"
#include "WorldLoot.h"
#include "PlayerGatheringComponent.h"
#include "Components/CapsuleComponent.h"
#include "PlayerEquipmentComponent.h"
#include "HomeModule.h"
#include "HomeBuildingComponent.h"
#include "StorageInteractionComponent.h"
#include "CompanionConstructionComponent.h"
#include "CompanionCargoComponent.h"
#include "InputKeyEventArgs.h"
#include "ResearchComponent.h"
#include "HollowScenario.h"
#include "CaptureComponent.h"
#include <limits>

// 施工闭环继续复用本文件的玩家导航探针和运动连续性检查。

namespace
{
// 从原地图与新玩家默认补给开始；不重置库存、生成伙伴或冻结野生生态。
struct FStarterJourney
{
    UWorld* World;ACFPlayerCharacter* Hero;TWeakObjectPtr<ACreatureCharacter> Pet;TWeakObjectPtr<AWorkStation> Bench;
    TWeakObjectPtr<UHomeMovementProbe> Input;TArray<FVector> Route;TMap<FGuid,FVector> WildPositions;
    FGuid PetId;FString Root;TSet<FString> Photos;bool Read=false,Preview=false,Joined=false;int32 Phase=-1,Point=0,SourceRemaining=0;
    double Deadline=0,At=0,ShotUntil=0,ProgressAt=0,LastTime=0,HeroTravel=0,PetTravel=0,WildTravel=0,NextDiagnostic=0;
    FVector LastH,LastP,VelocityH,VelocityP,Anchor;
    void Face(FVector Target)
    {const auto R=(Target-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->SetActorRotation(FRotator(0,R.Yaw,0));Hero->GetController()->SetControlRotation(R);Hero->FirstPersonCamera->SetWorldRotation(R);}
    bool Photo(const TCHAR* Name,FVector Target)
    {if(!Preview || Photos.Contains(Name))return false;Photos.Add(Name);Face(Target);FScreenshotRequest::RequestScreenshot(Root/(FString(Name)+TEXT(".png")),false,false);ShotUntil=FPlatformTime::Seconds()+.2;return true;}
    bool End(bool Pass,const TCHAR* Reason)
    {
        if(Input.IsValid())Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::LeftShift);if(!Pass)Hero->SaveProgress(TEXT("StarterJourneyBlocked"));
        UE_LOG(LogTemp,Display,TEXT("CF_STARTER_JOURNEY %s reason=%s phase=%d heroTravel=%.1f petTravel=%.1f wildTravel=%.1f hero=%s pet=%s feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Reason,Phase,HeroTravel,PetTravel,WildTravel,*Hero->GetActorLocation().ToString(),Pet.IsValid()?*Pet->GetActorLocation().ToString():TEXT("none"),*Hero->Feedback);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    bool Plan(FVector Goal,int32 Next)
    {
        auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);FNavLocation Ground;
        if(!Nav || !Nav->ProjectPointToNavigation(Goal,Ground,FVector(60,60,160)))return false;
        auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Hero->GetActorLocation(),Ground.Location,Hero);
        if(!Path || !Path->IsValid() || Path->IsPartial() || Path->PathPoints.Num()<2)return false;
        Route=Path->PathPoints;Point=1;Anchor=Hero->GetActorLocation();ProgressAt=World->GetTimeSeconds();Phase=Next;return CFInputQA::Press(Hero,EKeys::LeftShift);
    }
    int32 Move()
    {
        const auto Here=Hero->GetActorLocation();while(Point<Route.Num() && FVector::Dist2D(Here,Route[Point])<16)++Point;
        if(Point==Route.Num()){Input->Direction=FVector::ZeroVector;if(Hero->GetVelocity().Size2D()>3)return 0;CFInputQA::Release(Hero,EKeys::LeftShift);return 1;}
        const auto D=(Route[Point]-Here).GetSafeNormal2D();Input->Direction=D;Face(Here+D*500+FVector(0,0,64));
        if(FVector::Dist2D(Here,Anchor)>20){Anchor=Here;ProgressAt=World->GetTimeSeconds();}return World->GetTimeSeconds()-ProgressAt>5?-1:0;
    }
    bool Motion(const ACharacter* Pawn,FVector Before,FVector Velocity,double Delta) const
    {
        const auto* M=Pawn->GetCharacterMovement();const auto P=Pawn->GetActorLocation();
        return M && Delta>=0 && FMath::IsFinite(Delta) && !P.ContainsNaN() && Pawn->GetActorEnableCollision() && M->MovementMode!=MOVE_None &&
            FVector::Dist2D(Before,P)<=FMath::Max(static_cast<double>(M->GetMaxSpeed()),Velocity.Size2D())*Delta+3 &&
            FMath::Abs(P.Z-Before.Z)<=FMath::Max(FMath::Abs(Pawn->GetVelocity().Z),FMath::Abs(Velocity.Z))*Delta+.5*FMath::Abs(World->GetGravityZ())*Delta*Delta+M->MaxStepHeight+3;
    }
    bool Tick()
    {
        if(!Pet.IsValid() || !Bench.IsValid() || FPlatformTime::Seconds()>Deadline)return End(false,TEXT("lifetime"));
        const double Delta=World->GetTimeSeconds()-LastTime;const auto H=Hero->GetActorLocation(),P=Pet->GetActorLocation();
        if(!Motion(Hero,LastH,VelocityH,Delta) || !Motion(Pet.Get(),LastP,VelocityP,Delta) || Hero->Health<=0 || Pet->Health<=0 || !Pet->IsActorTickEnabled())return End(false,TEXT("motion-health"));
        HeroTravel+=FVector::Dist(H,LastH);PetTravel+=FVector::Dist(P,LastP);LastH=H;LastP=P;VelocityH=Hero->GetVelocity();VelocityP=Pet->GetVelocity();LastTime=World->GetTimeSeconds();
        int32 Ecology=0,Owned=0;for(TActorIterator<AWildEncounter> It(World);It;++It){++Ecology;if(!It->IsActorTickEnabled())return End(false,TEXT("ecology"));}
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)
        {
            if(It->Tamer==Hero->GetController())++Owned;
            if(It->Disposition!=ECreatureDisposition::Tamed && It->Health>0)
            {if(!It->IsActorTickEnabled() || !It->GetActorEnableCollision() || It->GetCharacterMovement()->MovementMode==MOVE_None)return End(false,TEXT("wild-disabled"));if(const auto* Old=WildPositions.Find(It->PersistentId))WildTravel+=FVector::Dist(*Old,It->GetActorLocation());WildPositions.Add(It->PersistentId,It->GetActorLocation());}
        }
        if(Ecology!=3 || Owned!=(Joined?1:0) || !HomeLayout::Collect(World).IsEmpty() || Hero->Inventory->Count(EFrontierItem::Medicine)!=1 || Hero->Inventory->Count(EFrontierItem::Food)!=(Joined?0:3))return End(false,TEXT("world-budget"));
        if(FPlatformTime::Seconds()<ShotUntil)return true;
        if(Phase==-1)
        {
            if(World->GetTimeSeconds()-At<.2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;
            if(Read){if(!Plan(Bench->GetActorLocation()+FVector(210,-180,40),3))return End(false,TEXT("return-path"));return true;}
            if(!Plan(Pet->GetActorLocation()+FVector(-190,0,0),0))return End(false,TEXT("approach-path"));return true;
        }
        if(Phase==0 || Phase==3)
        {
            const int32 Result=Move();if(Result<0)return End(false,TEXT("walk-blocked"));if(!Result)return true;Phase=Phase==0?1:4;At=World->GetTimeSeconds();return true;
        }
        if(Phase==1)
        {
            Face(P);if(Hero->TraceCreature()!=Pet.Get() || Hero->GetObjectiveProgress(0)!=1){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("observe"));return true;}
            if(Photo(TEXT("Approach"),P))return true;
            if(!CFInputQA::Press(Hero,EKeys::N) || Hero->GetObjectives().Claimed!=1 || Hero->Inventory->Count(EFrontierItem::CaptureDevice)!=7)return End(false,TEXT("discover-reward"));
            if(!CFInputQA::Press(Hero,EKeys::J) || Pet->Tamer!=Hero->GetController() || Pet->PersistentId!=PetId || Pet->Health!=Pet->GetMaxHealth() || Pet->BondPoints!=3 || Hero->Inventory->Count(EFrontierItem::Food)!=0)return End(false,TEXT("join"));Joined=true;
            if(!CFInputQA::Press(Hero,EKeys::N) || Hero->GetObjectives().Claimed!=2 || Hero->Inventory->Count(EFrontierItem::Material)!=18 || !Hero->SaveProgress(TEXT("StarterJourneyMid")))return End(false,TEXT("join-reward"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_JOIN PASS input=1 foodPaid=3 material=18 capture=7 samePet=1"));Photo(TEXT("Joined"),P);Phase=2;return true;
        }
        if(Phase==2){if(!Plan(Bench->GetActorLocation()+FVector(210,-180,40),3))return End(false,TEXT("return-path"));return true;}
        if(Phase==4)
        {
            Face(Bench->GetActorLocation());if(Hero->TraceFacility()!=Bench.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("bench-sight"));return true;}
            if(!CFInputQA::Press(Hero,EKeys::E) || Pet->WorkSite!=Bench.Get())return End(false,TEXT("assign-work"));Phase=5;At=World->GetTimeSeconds();return true;
        }
        if(Phase==5)
        {
            if(World->GetTimeSeconds()>=NextDiagnostic)
            {
                NextDiagnostic=World->GetTimeSeconds()+10;
                UE_LOG(LogTemp,Display,TEXT("CF_STARTER_WORK_STATE pet=%s bench=%s activity=%d stamina=%.1f progress=%d status=%s"),*P.ToString(),*Bench->GetActorLocation().ToString(),static_cast<int32>(Pet->Activity),Pet->Stamina,Hero->GetObjectives().WorkMaterials,*Pet->GetGatheringStatus());
                for(TActorIterator<AResourceNode> It(World);It;++It)if(It->Definition && It->Definition->Specialty==Bench->RequiredSpecialty && It->GetDistanceTo(Bench.Get())<=800)
                    UE_LOG(LogTemp,Display,TEXT("CF_STARTER_WORK_NODE id=%s position=%s remaining=%d distance=%.1f reach=%d"),*It->PersistentId.ToString(),*It->GetActorLocation().ToString(),It->Remaining,Pet->GetDistanceTo(*It),It->CanReach(Pet.Get(),220));
            }
            if(Hero->GetObjectives().WorkMaterials<3)
            {if(Pet->Activity==ECreatureActivity::Work && Hero->GetObjectives().WorkMaterials>0)Photo(TEXT("Working"),Pet->GetActorLocation());if(World->GetTimeSeconds()-At>60)return End(false,TEXT("production-timeout"));return true;}
            bool SourcePaid=false;for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,1))SourcePaid=It->Remaining==SourceRemaining-3;
            if(!SourcePaid || Pet->Experience!=3 || Hero->Inventory->Count(EFrontierItem::Material)!=21 || !CFInputQA::Press(Hero,EKeys::F) || !CFInputQA::Press(Hero,EKeys::N) || Hero->GetObjectives().Claimed!=3 || Hero->Inventory->Count(EFrontierItem::Material)!=33)return End(false,TEXT("work-reward"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RESOURCE PASS sourcePaid=3 experience=3"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_PRODUCTION PASS materialPaid=0 produced=3 rewards=24 material=33 claimed=3"));Phase=6;At=World->GetTimeSeconds();return true;
        }
        if(Phase==6)
        {
            if(Pet->GetDistanceTo(Hero)>250 || Pet->Activity!=ECreatureActivity::Follow){if(World->GetTimeSeconds()-At>20)return End(false,TEXT("regroup"));return true;}
            if(Photo(TEXT("ReadyForFarm"),P))return true;
            const auto HP=Hero->Health,PHP=Pet->Health;
            if(HeroTravel<(Read?700:1200) || !Hero->SaveProgress(TEXT("StarterJourneyDone")) || !Hero->LoadProgress(TEXT("StarterJourneyDone")))return End(false,TEXT("save"));
            Pet.Reset();for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->PersistentId==PetId)Pet=*It;
            if(!Pet.IsValid() || !Hero->GetActorLocation().Equals(H,.01) || !Pet->GetActorLocation().Equals(P,.01) || Hero->Health!=HP || Pet->Health!=PHP || Hero->GetObjectives().Claimed!=3 || Hero->Inventory->Count(EFrontierItem::Material)!=33 || Hero->Inventory->Count(EFrontierItem::Food)!=0 || Pet->BondPoints!=3)return End(false,TEXT("restore"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_LIFE PASS version=38 positionRetained=1 claimed=3 material=33 food=0 ecology=3"));return End(true,TEXT("complete"));
        }
        return true;
    }
};
}
void RunStarterJourneyScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto S=MakeShared<FStarterJourney>();S->World=World;S->Hero=Hero;S->Read=Read;S->Joined=Read;S->Deadline=FPlatformTime::Seconds()+180;
    if(Read && !Hero->LoadProgress(TEXT("StarterJourneyInput"))){S->End(false,TEXT("input"));return;}
    int32 Resources=0;for(TActorIterator<AResourceNode> It(World);It;++It)if(It->Definition && It->PersistentId.IsValid()){++Resources;if(It->PersistentId==FGuid(0xCF170000,0,0,1))S->SourceRemaining=It->Remaining;}
    if(Resources!=11 || S->SourceRemaining!=24){S->End(false,TEXT("initial-resources"));return;}
    double Distance=DBL_MAX;int32 Owned=0;for(TActorIterator<ACreatureCharacter> It(World);It;++It)
    {
        if(It->Tamer==Hero->GetController())++Owned;
        if(It->Definition && It->Definition->PeacefulFoodCost==3 && !It->bAlpha && (Read?It->Tamer==Hero->GetController():!It->Tamer) && Hero->GetDistanceTo(*It)<Distance){S->Pet=*It;Distance=Hero->GetDistanceTo(*It);}
    }
    for(TActorIterator<AWorkStation> It(World);It;++It)if(!It->bPlayerBuilt && It->RequiredSpecialty==EWorkSpecialty::Gathering)S->Bench=*It;
    if(!S->Pet.IsValid() || !S->Bench.IsValid() || Owned!=(Read?1:0) || Hero->GetObjectives().Claimed!=(Read?2:0) || Hero->Inventory->Count(EFrontierItem::Material)!=(Read?18:6) || Hero->Inventory->Count(EFrontierItem::CaptureDevice)!=(Read?7:5) || Hero->Inventory->Count(EFrontierItem::Food)!=(Read?0:3) || Hero->Inventory->Count(EFrontierItem::Medicine)!=1){S->End(false,TEXT("initial-state"));return;}
    S->PetId=S->Pet->PersistentId;S->LastH=Hero->GetActorLocation();S->LastP=S->Pet->GetActorLocation();S->VelocityH=Hero->GetVelocity();S->VelocityP=S->Pet->GetVelocity();S->LastTime=S->At=World->GetTimeSeconds();S->Input=NewObject<UHomeMovementProbe>(Hero);S->Input->RegisterComponent();
    if(Read)
    {
        const auto* Snapshot=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("StarterJourneyInput"),0));const auto* Partner=Snapshot?Snapshot->Creatures.FindByPredicate([&](const FCreatureSaveRecord& R){return R.Id==S->PetId;}):nullptr;
        if(!Partner || Snapshot->FormatVersion!=38 || !Hero->GetActorLocation().Equals(Snapshot->PlayerTransform.GetLocation(),.01) || !S->Pet->GetActorLocation().Equals(Partner->Transform.GetLocation(),.01)){S->End(false,TEXT("input-position"));return;}
    }
    S->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFStarterJourneyPreview"));if(S->Preview){S->Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("StarterJourneyPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*S->Root,true);UE_LOG(LogTemp,Display,TEXT("CF_STARTER_PREVIEW_ROOT %s"),*S->Root);}
    UE_LOG(LogTemp,Display,TEXT("CF_STARTER_SOURCE PASS read=%d defaultSupplies=%d pet=%s"),Read,!Read,*S->PetId.ToString());
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){return S->Tick();}));
}

namespace
{
// 延用正常开局行走／朝向／拍照工具；从真实前三项完成档继续，不重新发放补给。
struct FStarterFarm : FStarterJourney
{
    TWeakObjectPtr<AWorkStation> Garden;TWeakObjectPtr<AResourceNode> Fiber;
    FGuid GardenId;FString Nickname;bool FoodMade=false,Built=false,Gathered=false,SeedsMade=false,Awarded=false,Held=false;
    int32 FiberBefore=18;const FVector Plot=FVector(-800,-700,-15);
    bool End(bool Pass,const TCHAR* Reason)
    {
        if(Input.IsValid())Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::LeftShift);if(Held)CFInputQA::Release(Hero,EKeys::M);
        const auto Feedback=Hero->Feedback;if(!Pass)Hero->SaveProgress(TEXT("StarterFarmBlocked"));
        UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FARM %s reason=%s phase=%d heroTravel=%.1f petTravel=%.1f wildTravel=%.1f hero=%s pet=%s feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Reason,Phase,HeroTravel,PetTravel,WildTravel,*Hero->GetActorLocation().ToString(),Pet.IsValid()?*Pet->GetActorLocation().ToString():TEXT("none"),*Feedback);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    bool PlanFiber()
    {
        auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);if(!Nav)return false;TArray<FVector> Best;double Length=DBL_MAX;
        for(int32 I=0;I<8;++I)
        {
            const double A=I*PI/4;FNavLocation Ground;if(!Nav->ProjectPointToNavigation(Fiber->GetActorLocation()+FVector(FMath::Cos(A)*220,FMath::Sin(A)*220,0),Ground,FVector(60,60,160)))continue;
            const auto* Capsule=Hero->GetCapsuleComponent();const auto Center=Ground.Location+FVector(0,0,Capsule->GetScaledCapsuleHalfHeight());FCollisionQueryParams Q(SCENE_QUERY_STAT(StarterFiberStand),false,Hero);FHitResult Hit;
            if(World->OverlapBlockingTestByChannel(Center,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius()-1,Capsule->GetScaledCapsuleHalfHeight()-1),Q))continue;
            if(World->LineTraceSingleByChannel(Hit,Center,Fiber->GetActorLocation(),ECC_Visibility,Q) && Hit.GetActor()!=Fiber.Get())continue;
            auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Hero->GetActorLocation(),Ground.Location,Hero);
            if(Path && Path->IsValid() && !Path->IsPartial() && Path->PathPoints.Num()>1 && Path->GetPathLength()<Length){Best=Path->PathPoints;Length=Path->GetPathLength();}
        }
        if(Best.IsEmpty())return false;Route=MoveTemp(Best);Point=1;Anchor=Hero->GetActorLocation();ProgressAt=World->GetTimeSeconds();Phase=2;return CFInputQA::Press(Hero,EKeys::LeftShift);
    }
    bool Budget() const
    {
        int32 Plots=0;for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt)++Plots;
        const int32 Food=Garden.IsValid()?Garden->StoredFood:0,Seed=Garden.IsValid()?Garden->Crop->Seeds:0,Planted=Garden.IsValid() && Garden->Crop->bPlanted?1:0;
        return Plots==(Built?1:0) && HomeLayout::Collect(World).IsEmpty() && Pet->PersistentId==PetId && Pet->Nickname==Nickname && Pet->BondPoints==3 &&
            Hero->GetObjectives().WorkMaterials==3 && Hero->GetObjectives().Claimed==(Awarded?4:3) && Hero->Inventory->Count(EFrontierItem::CaptureDevice)==7 && Hero->Inventory->Count(EFrontierItem::Medicine)==(Awarded?3:1) &&
            Hero->Inventory->Count(EFrontierItem::Material)+(FoodMade?2:0)+(Built?18:0)==33 &&
            Hero->Inventory->Count(EFrontierItem::Fiber)+(SeedsMade?1:0)==(Gathered?1:0) &&
            Hero->Inventory->Count(EFrontierItem::Seeds)+Seed+Planted+Hero->GetObjectives().FarmFood==(SeedsMade?3:0) &&
            Hero->Inventory->Count(EFrontierItem::Food)+Food+(SeedsMade?1:0)==(FoodMade?1:0)+Hero->GetObjectives().FarmFood &&
            Fiber->Remaining==FiberBefore-(Gathered?1:0);
    }
    bool Tick()
    {
        if(!Pet.IsValid() || !Fiber.IsValid() || FPlatformTime::Seconds()>Deadline)return End(false,TEXT("lifetime"));
        const auto H=Hero->GetActorLocation(),P=Pet->GetActorLocation();const double Delta=World->GetTimeSeconds()-LastTime;
        if(!Motion(Hero,LastH,VelocityH,Delta) || !Motion(Pet.Get(),LastP,VelocityP,Delta) || Hero->Health<=0 || Pet->Health<=0 || !Pet->IsActorTickEnabled())return End(false,TEXT("motion-health"));
        HeroTravel+=FVector::Dist(H,LastH);PetTravel+=FVector::Dist(P,LastP);LastH=H;LastP=P;VelocityH=Hero->GetVelocity();VelocityP=Pet->GetVelocity();LastTime=World->GetTimeSeconds();
        int32 Ecology=0,Owned=0;for(TActorIterator<AWildEncounter> It(World);It;++It){++Ecology;if(!It->IsActorTickEnabled())return End(false,TEXT("ecology"));}
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)
        {
            if(It->Tamer==Hero->GetController())++Owned;
            if(It->Disposition!=ECreatureDisposition::Tamed && It->Health>0){if(!It->IsActorTickEnabled() || !It->GetActorEnableCollision() || It->GetCharacterMovement()->MovementMode==MOVE_None)return End(false,TEXT("wild-disabled"));if(const auto* Old=WildPositions.Find(It->PersistentId))WildTravel+=FVector::Dist(*Old,It->GetActorLocation());WildPositions.Add(It->PersistentId,It->GetActorLocation());}
        }
        // 采集输入的正常更新发生在本帧验证前，第一份纤维到包时同步记账，不修改库存。
        if(Held && Hero->Inventory->Count(EFrontierItem::Fiber)==1)Gathered=true;
        if(Ecology!=3 || Owned!=1 || !Budget())return End(false,TEXT("budget"));if(FPlatformTime::Seconds()<ShotUntil)return true;
        if(Phase==-1)
        {
            if(World->GetTimeSeconds()-At<.2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;
            if(Read){if(!Plan(Bench->GetActorLocation()+FVector(210,-180,40),4))return End(false,TEXT("read-return-path"));return true;}
            if(!CFInputQA::Press(Hero,EKeys::Three) || Hero->Inventory->Count(EFrontierItem::Food)!=1 || Hero->Inventory->Count(EFrontierItem::Material)!=31)return End(false,TEXT("food"));FoodMade=true;
            if(!Plan(FVector(-800,-1000,40),0))return End(false,TEXT("build-path"));return true;
        }
        if(Phase==0 || Phase==2 || Phase==4 || Phase==6 || Phase==8)
        {
            const int32 Result=Move();if(Result<0)return End(false,TEXT("walk-blocked"));if(!Result)return true;
            if(Phase==0)
            {
                Face(FVector(-800,-700,-50));if(!CFInputQA::Press(Hero,EKeys::B))return End(false,TEXT("build-open"));
                for(int32 I=0;Hero->Building->Selected!=EWorkSpecialty::Farming && I<8;++I)if(!CFInputQA::Press(Hero,EKeys::V))return End(false,TEXT("build-select"));Phase=1;At=World->GetTimeSeconds();return true;
            }
            if(Phase==2)
            {
                Phase=10;At=World->GetTimeSeconds();return true;
            }
            if(Phase==4){Phase=5;At=World->GetTimeSeconds();return true;}
            if(Phase==6){Phase=7;At=World->GetTimeSeconds();return true;}
            Phase=9;At=World->GetTimeSeconds();return true;
        }
        if(Phase==1)
        {
            Face(FVector(-800,-700,-50));FString Reason;
            const FTransform Expected(FRotator(0,90,0),Plot,FVector(2,1,.7));
            if(!Hero->Building->ValidatePlacement(Expected,Reason)){if(World->GetTimeSeconds()-At>5)return End(false,*Reason);return true;}
            if(Photo(TEXT("Planning"),FVector(-800,-700,-50)))return true;
            if(!CFInputQA::Action(Hero,TEXT("Attack"),EKeys::LeftMouseButton))return End(false,TEXT("build-confirm"));
            for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt && It->RequiredSpecialty==EWorkSpecialty::Farming && It->GetActorLocation().Equals(Plot,.01))Garden=*It;
            if(!Garden.IsValid() || !Garden->GetActorTransform().Equals(Expected,.01) || Hero->Inventory->Count(EFrontierItem::Material)!=13 || !CFInputQA::Press(Hero,EKeys::B) || Hero->Building->IsActive())return End(false,TEXT("built"));Built=true;GardenId=Garden->PersistentId;
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FARM_BUILD PASS input=1 paid=18 material=13"));Photo(TEXT("Built"),Plot+FVector(0,0,30));if(!PlanFiber())return End(false,TEXT("fiber-path"));return true;
        }
        if(Phase==3)
        {
            if(!Gathered){if(!Hero->Gathering->IsGathering())return End(false,TEXT("gather-interrupted"));return true;}
            if(!CFInputQA::Release(Hero,EKeys::M) || Hero->Gathering->IsGathering())return End(false,TEXT("gather-release"));Held=false;
            if(!Hero->SaveProgress(TEXT("StarterFarmMid")))return End(false,TEXT("mid-save"));UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FARM_GATHER PASS fiber=1 nodePaid=1 input=1"));Photo(TEXT("Fiber"),Fiber->GetActorLocation());if(!Plan(Bench->GetActorLocation()+FVector(210,-180,40),4))return End(false,TEXT("return-path"));return true;
        }
        if(Phase==10)
        {
            Face(Fiber->GetActorLocation());
            if(Hero->TraceResource()!=Fiber.Get())
            {
                if(NextDiagnostic==0){FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(StarterFiberAim),false,Hero);const auto Eye=Hero->FirstPersonCamera->GetComponentLocation();World->LineTraceSingleByChannel(Hit,Eye,Eye+Hero->FirstPersonCamera->GetForwardVector()*340,ECC_Visibility,Q);UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FARM_SIGHT_WAIT actor=%s position=%s"),*GetNameSafe(Hit.GetActor()),*Hit.ImpactPoint.ToString());NextDiagnostic=World->GetTimeSeconds();}
                // 仅有界等待原巡游生物让开射线，不忽略遮挡，也不反复按M尝试。
                if(World->GetTimeSeconds()-At>5)return End(false,TEXT("gather-occluded"));return true;
            }
            if(!CFInputQA::Press(Hero,EKeys::M) || !Hero->Gathering->IsGathering())return End(false,TEXT("gather-start"));Held=true;Phase=3;return true;
        }
        if(Phase==5)
        {
            if(!Hero->IsBackpackOpen())
            {
                const auto* Catalog=UCraftingCatalog::Load();const int32 Index=Catalog?Catalog->Recipes.IndexOfByPredicate([](const FFrontierRecipe& R){return R.Id==TEXT("PlantingSeeds");}):INDEX_NONE;
                if(Index<0 || !CFInputQA::Press(Hero,EKeys::C))return End(false,TEXT("seed-menu"));for(int32 I=0;I<Index;++I)if(!CFInputQA::Press(Hero,EKeys::Down))return End(false,TEXT("seed-select"));
                if(Hero->GetBackpackRecipe()!=Index || !Hero->GetBackpackGatheringHint(Index).Contains(TEXT("配料齐备")))return End(false,TEXT("seed-ready"));
            }
            if(Photo(TEXT("Crafting"),Bench->GetActorLocation()))return true;
            if(!CFInputQA::Press(Hero,EKeys::Enter) || Hero->Inventory->Count(EFrontierItem::Seeds)!=3 || Hero->Inventory->Count(EFrontierItem::Food)!=0 || Hero->Inventory->Count(EFrontierItem::Fiber)!=0 || !CFInputQA::Press(Hero,EKeys::C))return End(false,TEXT("seed-craft"));SeedsMade=true;
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FARM_SEEDS PASS foodPaid=1 fiberPaid=1 seeds=3 input=1"));if(!Plan(FVector(-800,-970,40),6))return End(false,TEXT("garden-path"));return true;
        }
        if(Phase==7)
        {
            Face(Plot+FVector(0,0,30));if(Hero->TraceFacility()!=Garden.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("garden-sight"));return true;}
            if(!CFInputQA::Press(Hero,EKeys::F2) || !CFInputQA::Press(Hero,EKeys::F2) || Garden->Crop->Seeds!=2 || Hero->Inventory->Count(EFrontierItem::Seeds)!=1 || !CFInputQA::Press(Hero,EKeys::E) || Pet->WorkSite!=Garden.Get())return End(false,TEXT("garden-assign"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FARM_ASSIGN PASS input=1 seedsStored=2 seedsRetained=1 samePet=1"));Photo(TEXT("Assigned"),Plot+FVector(0,0,30));if(!Plan(FVector(-1050,-1000,40),8))return End(false,TEXT("yield-path"));return true;
        }
        if(Phase==9)
        {
            if(Hero->GetObjectives().FarmFood<2){if(World->GetTimeSeconds()-At>90)return End(false,TEXT("farm-timeout"));return true;}
            if(!Awarded)
            {
                if(Garden->StoredFood!=2 || Garden->Crop->Seeds!=0 || Garden->Crop->bPlanted || Pet->Experience!=5 || !CFInputQA::Press(Hero,EKeys::F) || !CFInputQA::Press(Hero,EKeys::N) || Hero->GetObjectives().Claimed!=4)return End(false,TEXT("farm-reward"));Awarded=true;
                UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FARM_HARVEST PASS farmFood=2 experience=5 claimed=4 medicine=3"));Photo(TEXT("Harvested"),Plot+FVector(0,0,30));return true;
            }
            Face(Plot+FVector(0,0,30));if(Hero->Inventory->Count(EFrontierItem::Food)==0)
            {if(Hero->TraceFacility()!=Garden.Get()){if(World->GetTimeSeconds()-At>95)return End(false,TEXT("food-sight"));return true;}if(!CFInputQA::Press(Hero,EKeys::O) || Hero->Inventory->Count(EFrontierItem::Food)!=2 || Garden->StoredFood!=0)return End(false,TEXT("food-withdraw"));}
            if(Pet->GetDistanceTo(Hero)>250 || Pet->Activity!=ECreatureActivity::Follow){if(World->GetTimeSeconds()-At>110)return End(false,TEXT("regroup"));return true;}
            if(Photo(TEXT("Home"),P))return true;const auto HP=Hero->Health,PHP=Pet->Health;const auto GardenTransform=Garden->GetActorTransform();
            if(HeroTravel<(Read?2000:4500) || WildTravel<50 || !Hero->SaveProgress(TEXT("StarterFarmDone")) || !Hero->LoadProgress(TEXT("StarterFarmDone")))return End(false,TEXT("save"));
            Pet.Reset();Garden.Reset();Fiber.Reset();for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->PersistentId==PetId)Pet=*It;
            for(TActorIterator<AWorkStation> It(World);It;++It)if(It->PersistentId==GardenId)Garden=*It;for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,8))Fiber=*It;
            if(!Pet.IsValid() || !Garden.IsValid() || !Fiber.IsValid() || !Hero->GetActorLocation().Equals(H,.01) || !Pet->GetActorLocation().Equals(P,.01) || Hero->Health!=HP || Pet->Health!=PHP || !Garden->GetActorTransform().Equals(GardenTransform,.01) || !Budget())return End(false,TEXT("restore"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FARM_LIFE PASS version=38 positionRetained=1 material=13 food=2 storedFood=0 seeds=1 claimed=4"));return End(true,TEXT("complete"));
        }
        return true;
    }
};
}
void RunStarterFarmScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto S=MakeShared<FStarterFarm>();S->World=World;S->Hero=Hero;S->Read=Read;S->Deadline=FPlatformTime::Seconds()+240;
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("StarterFarmInput"),0));
    if(!Source || Source->FormatVersion!=38 || Source->HomeModules.Num()!=0 || Source->Facilities.Num()!=(Read?1:0) || !Hero->LoadProgress(TEXT("StarterFarmInput"))){S->End(false,TEXT("source"));return;}
    int32 Owned=0;for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController()){++Owned;S->Pet=*It;}
    for(TActorIterator<AWorkStation> It(World);It;++It){if(!It->bPlayerBuilt && It->RequiredSpecialty==EWorkSpecialty::Gathering)S->Bench=*It;if(It->bPlayerBuilt && It->RequiredSpecialty==EWorkSpecialty::Farming)S->Garden=*It;}
    for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,8))S->Fiber=*It;
    if(Owned!=1 || !S->Pet.IsValid() || !S->Bench.IsValid() || !S->Fiber.IsValid() || S->Pet->Experience!=3 || S->Pet->BondPoints!=3 || Hero->Inventory->Count(EFrontierItem::Material)!=(Read?13:33) || Hero->Inventory->Count(EFrontierItem::Food)!=(Read?1:0) || Hero->Inventory->Count(EFrontierItem::Fiber)!=(Read?1:0) || Hero->Inventory->Count(EFrontierItem::Seeds)!=0 || Hero->GetObjectives().FarmFood!=0 || Hero->GetObjectives().Claimed!=3){S->End(false,TEXT("source-state"));return;}
    S->PetId=S->Pet->PersistentId;S->Nickname=S->Pet->Nickname;
    const auto* Partner=Source->Creatures.FindByPredicate([&](const FCreatureSaveRecord& R){return R.Id==S->PetId;});
    if(!Partner || !Hero->GetActorLocation().Equals(Source->PlayerTransform.GetLocation(),.01) || !S->Pet->GetActorLocation().Equals(Partner->Transform.GetLocation(),.01)){S->End(false,TEXT("source-position"));return;}
    if(Read){if(!S->Garden.IsValid() || !S->Garden->GetActorLocation().Equals(S->Plot,.01) || S->Garden->StoredFood!=0 || S->Garden->Crop->IsOccupied()){S->End(false,TEXT("source-garden"));return;}S->FoodMade=S->Built=S->Gathered=true;S->GardenId=S->Garden->PersistentId;S->FiberBefore=S->Fiber->Remaining+1;}
    else S->FiberBefore=S->Fiber->Remaining;
    S->LastH=Hero->GetActorLocation();S->LastP=S->Pet->GetActorLocation();S->VelocityH=Hero->GetVelocity();S->VelocityP=S->Pet->GetVelocity();S->LastTime=S->At=World->GetTimeSeconds();S->Input=NewObject<UHomeMovementProbe>(Hero);S->Input->RegisterComponent();
    S->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFStarterFarmPreview"));if(S->Preview){S->Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("StarterFarmPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*S->Root,true);UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FARM_PREVIEW_ROOT %s"),*S->Root);}
    UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FARM_SOURCE PASS read=%d positionRetained=1 pet=%s"),Read,*S->PetId.ToString());FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){return S->Tick();}));
}

namespace
{
struct FStarterPrep : FStarterJourney
{
    TWeakObjectPtr<AWorkStation> Garden;TWeakObjectPtr<AResourceNode> Brush;
    FFacilitySaveRecord OriginalGarden;FString Nickname;
    bool End(bool Pass,const TCHAR* Reason)
    {
        if(Input.IsValid())Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::LeftShift);
        const auto Feedback=Hero->Feedback;if(!Pass)Hero->SaveProgress(TEXT("StarterPrepBlocked"));
        UE_LOG(LogTemp,Display,TEXT("CF_STARTER_PREP %s reason=%s phase=%d heroTravel=%.1f petTravel=%.1f wildTravel=%.1f feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Reason,Phase,HeroTravel,PetTravel,WildTravel,*Feedback);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    bool Budget(int32 ExpectedMedicine=3) const
    {
        if(!Garden.IsValid() || !Brush.IsValid())return false;const auto& G=Hero->Equipment->GetState();const int32 Produced=21-Brush->Remaining;
        int32 Farms=0;for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt)++Farms;
        return Produced>=0 && Produced<=7 && Hero->Inventory->Count(EFrontierItem::Material)+(G.bOwnSpear?8:0)+(G.bOwnVest?12:0)==13+Produced &&
            Pet->Experience==5+Produced && Pet->BondPoints==3 && Pet->Nickname==Nickname &&
            Hero->Inventory->Count(EFrontierItem::Food)==2 && Hero->Inventory->Count(EFrontierItem::Seeds)==1 && Hero->Inventory->Count(EFrontierItem::Medicine)==ExpectedMedicine && Hero->Inventory->Count(EFrontierItem::CaptureDevice)==7 && Hero->Inventory->Count(EFrontierItem::Fiber)==0 &&
            Hero->GetObjectives().Claimed==4 && Hero->GetObjectives().FarmFood==2 && Hero->GetObjectives().WorkMaterials==3 && !Hero->GetObjectives().bAlphaResolved &&
            HomeLayout::Collect(World).IsEmpty() && Farms==1 && Garden->PersistentId==OriginalGarden.Id && Garden->RequiredSpecialty==OriginalGarden.Specialty && Garden->GetActorTransform().Equals(OriginalGarden.Transform,.01) &&
            Garden->StoredFood==OriginalGarden.StoredFood && Garden->Crop->Seeds==OriginalGarden.Seeds && Garden->Crop->bPlanted==OriginalGarden.bPlanted && FMath::IsNearlyEqual(Garden->Crop->GrowthWork,OriginalGarden.GrowthWork,.01f);
    }
    bool Tick()
    {
        if(!Pet.IsValid() || !Bench.IsValid() || FPlatformTime::Seconds()>Deadline)return End(false,TEXT("lifetime"));
        const auto H=Hero->GetActorLocation(),P=Pet->GetActorLocation();const double Delta=World->GetTimeSeconds()-LastTime;
        if(!Motion(Hero,LastH,VelocityH,Delta) || !Motion(Pet.Get(),LastP,VelocityP,Delta) || Hero->Health<=0 || Pet->Health<=0 || !Pet->IsActorTickEnabled())return End(false,TEXT("motion-health"));
        HeroTravel+=FVector::Dist(H,LastH);PetTravel+=FVector::Dist(P,LastP);LastH=H;LastP=P;VelocityH=Hero->GetVelocity();VelocityP=Pet->GetVelocity();LastTime=World->GetTimeSeconds();
        int32 Ecology=0,Owned=0;for(TActorIterator<AWildEncounter> It(World);It;++It){++Ecology;if(!It->IsActorTickEnabled())return End(false,TEXT("ecology"));}
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)
        {
            if(It->Tamer==Hero->GetController())++Owned;
            if(It->Disposition!=ECreatureDisposition::Tamed && It->Health>0){if(!It->IsActorTickEnabled() || !It->GetActorEnableCollision() || It->GetCharacterMovement()->MovementMode==MOVE_None)return End(false,TEXT("wild-disabled"));if(const auto* Old=WildPositions.Find(It->PersistentId))WildTravel+=FVector::Dist(*Old,It->GetActorLocation());WildPositions.Add(It->PersistentId,It->GetActorLocation());}
        }
        if(Ecology!=3 || Owned!=1 || !Budget())return End(false,TEXT("budget-home"));if(FPlatformTime::Seconds()<ShotUntil)return true;
        if(Phase==-1)
        {
            if(World->GetTimeSeconds()-At<.2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;
            if(Read){Phase=4;return true;}
            if(!Hero->GetObjectiveHint().Contains(TEXT("按4")))return End(false,TEXT("spear-hint"));if(Photo(TEXT("NeedSpear"),P))return true;
            if(!Plan(Bench->GetActorLocation()+FVector(210,-180,40),0))return End(false,TEXT("base-path"));return true;
        }
        if(Phase==0)
        {
            const int32 Result=Move();if(Result<0)return End(false,TEXT("walk-blocked"));if(!Result)return true;
            if(!CFInputQA::Press(Hero,EKeys::Four) || !Hero->Equipment->GetState().bSpearEquipped || Hero->Inventory->Count(EFrontierItem::Material)!=5 || !Hero->GetObjectiveHint().Contains(TEXT("按5")))return End(false,TEXT("spear-craft"));
            // 用实际不足的5材料尝试护甲，原事务必须拒绝且不扣料。
            if(!CFInputQA::Press(Hero,EKeys::Five) || Hero->Inventory->Count(EFrontierItem::Material)!=5 || Hero->Equipment->GetState().bOwnVest)return End(false,TEXT("vest-reject"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_PREP_SPEAR PASS input=1 cost=8 material=5 vestRejected=1"));Photo(TEXT("Spear"),P);Phase=1;At=World->GetTimeSeconds();return true;
        }
        if(Phase==1)
        {
            Face(Bench->GetActorLocation());if(Hero->TraceFacility()!=Bench.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("bench-sight"));return true;}
            if(!CFInputQA::Press(Hero,EKeys::E) || Pet->WorkSite!=Bench.Get())return End(false,TEXT("assign"));Phase=2;At=World->GetTimeSeconds();return true;
        }
        if(Phase==2)
        {
            if(Hero->Inventory->Count(EFrontierItem::Material)<12)
            {if(Hero->Inventory->Count(EFrontierItem::Material)>5)Photo(TEXT("Collecting"),P);if(World->GetTimeSeconds()-At>60)return End(false,TEXT("production-timeout"));return true;}
            if(Brush->Remaining!=14 || Pet->Experience!=12 || !CFInputQA::Press(Hero,EKeys::F) || !Hero->SaveProgress(TEXT("StarterPrepMid")))return End(false,TEXT("production"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_PREP_RESOURCE PASS produced=7 sourcePaid=7 experience=12"));Phase=4;return true;
        }
        if(Phase==4)
        {
            if(!Hero->GetObjectiveHint().Contains(TEXT("按5")))return End(false,TEXT("vest-hint"));if(Photo(TEXT("VestReady"),P))return true;
            if(!CFInputQA::Press(Hero,EKeys::Five) || !Hero->Equipment->GetState().bVestEquipped || Hero->Inventory->Count(EFrontierItem::Material)!=0 || !Hero->GetObjectiveHint().Contains(TEXT("危险首领")))return End(false,TEXT("vest-craft"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_PREP_VEST PASS input=1 cost=12 material=0 advisoryOnly=1"));Phase=5;return true;
        }
        if(Phase==5)
        {
            if(!CFInputQA::Press(Hero,EKeys::Six) || Hero->Equipment->GetState().bSpearEquipped || !Hero->GetObjectiveHint().Contains(TEXT("按6装备")))return End(false,TEXT("spear-toggle"));Phase=6;return true;
        }
        if(Phase==6)
        {
            if(Photo(TEXT("SpearOff"),P))return true;
            if(!CFInputQA::Press(Hero,EKeys::Six) || !CFInputQA::Press(Hero,EKeys::Seven) || Hero->Equipment->GetState().bVestEquipped || !Hero->GetObjectiveHint().Contains(TEXT("按7装备")))return End(false,TEXT("vest-toggle"));Phase=7;return true;
        }
        if(Phase==7)
        {
            if(Photo(TEXT("VestOff"),P))return true;
            if(!CFInputQA::Press(Hero,EKeys::Seven) || !Hero->Equipment->GetState().bSpearEquipped || !Hero->Equipment->GetState().bVestEquipped || !Hero->GetObjectiveHint().Contains(TEXT("直线")))return End(false,TEXT("equipped"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_PREP_HINTS PASS states=5 toggles=2"));Phase=8;At=World->GetTimeSeconds();return true;
        }
        if(Phase==8)
        {
            if(Pet->GetDistanceTo(Hero)>250 || Pet->Activity!=ECreatureActivity::Follow){if(World->GetTimeSeconds()-At>20)return End(false,TEXT("regroup"));return true;}
            if(Photo(TEXT("Prepared"),P))return true;const auto HP=Hero->Health,PHP=Pet->Health;
            if((!Read && HeroTravel<700) || Hero->GetPlayerAttackDamage()!=35 || !FMath::IsNearlyEqual(Hero->Equipment->GetDamageReduction(),.2f) || !Hero->SaveProgress(TEXT("StarterPrepDone")) || !Hero->LoadProgress(TEXT("StarterPrepDone")))return End(false,TEXT("save"));
            Pet.Reset();Garden.Reset();Brush.Reset();for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->PersistentId==PetId)Pet=*It;
            for(TActorIterator<AWorkStation> It(World);It;++It)if(It->PersistentId==OriginalGarden.Id)Garden=*It;for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,1))Brush=*It;
            const auto& Gear=Hero->Equipment->GetState();
            if(!Pet.IsValid() || !Garden.IsValid() || !Brush.IsValid() || !Hero->GetActorLocation().Equals(H,.01) || !Pet->GetActorLocation().Equals(P,.01) || Hero->Health!=HP || Pet->Health!=PHP || !Gear.bSpearEquipped || !Gear.bVestEquipped || !Budget())return End(false,TEXT("restore"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_PREP_LIFE PASS version=38 positionRetained=1 material=0 food=2 seeds=1 medicine=3 gear=2 claimed=4"));return End(true,TEXT("complete"));
        }
        return true;
    }
};
}
void RunStarterPrepScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto S=MakeShared<FStarterPrep>();S->World=World;S->Hero=Hero;S->Read=Read;S->Deadline=FPlatformTime::Seconds()+150;
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("StarterPrepInput"),0));
    if(!Source || Source->FormatVersion!=38 || Source->HomeModules.Num()!=0 || Source->Facilities.Num()!=1 || !Hero->LoadProgress(TEXT("StarterPrepInput"))){S->End(false,TEXT("source"));return;}
    S->OriginalGarden=Source->Facilities[0];int32 Owned=0;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController()){S->Pet=*It;++Owned;}
    for(TActorIterator<AWorkStation> It(World);It;++It){if(It->PersistentId==S->OriginalGarden.Id)S->Garden=*It;if(!It->bPlayerBuilt && It->RequiredSpecialty==EWorkSpecialty::Gathering)S->Bench=*It;}
    for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,1))S->Brush=*It;
    const auto& Gear=Hero->Equipment->GetState();
    if(Owned!=1 || !S->Pet.IsValid() || !S->Garden.IsValid() || !S->Bench.IsValid() || !S->Brush.IsValid() || S->Brush->Remaining!=(Read?14:21) || S->Pet->Experience!=(Read?12:5) || Hero->Inventory->Count(EFrontierItem::Material)!=(Read?12:13) || Gear.bOwnSpear!=Read || Gear.bSpearEquipped!=Read || Gear.bOwnVest){S->End(false,TEXT("source-state"));return;}
    S->PetId=S->Pet->PersistentId;S->Nickname=S->Pet->Nickname;const auto* Partner=Source->Creatures.FindByPredicate([&](const FCreatureSaveRecord& R){return R.Id==S->PetId;});
    if(!Partner || !Hero->GetActorLocation().Equals(Source->PlayerTransform.GetLocation(),.01) || !S->Pet->GetActorLocation().Equals(Partner->Transform.GetLocation(),.01) || !S->Budget()){S->End(false,TEXT("source-position-budget"));return;}
    S->LastH=Hero->GetActorLocation();S->LastP=S->Pet->GetActorLocation();S->VelocityH=Hero->GetVelocity();S->VelocityP=S->Pet->GetVelocity();S->LastTime=S->At=World->GetTimeSeconds();S->Input=NewObject<UHomeMovementProbe>(Hero);S->Input->RegisterComponent();
    S->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFStarterPrepPreview"));if(S->Preview){S->Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("StarterPrepPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*S->Root,true);UE_LOG(LogTemp,Display,TEXT("CF_STARTER_PREP_PREVIEW_ROOT %s"),*S->Root);}
    UE_LOG(LogTemp,Display,TEXT("CF_STARTER_PREP_SOURCE PASS read=%d positionRetained=1 pet=%s"),Read,*S->PetId.ToString());FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){return S->Tick();}));
}

namespace
{
// 从真实装备完成档接敌再撤离；不补血、不移动生物、不修改首领和伙伴能力。
struct FStarterScout : FStarterPrep
{
    TWeakObjectPtr<AWildEncounter> Region;TWeakObjectPtr<ACreatureCharacter> Boss;
    float BossHealth=0,InitialHeroHealth=0,InitialPetHealth=0;bool Hit=false,Warning=false;
    bool Rescue=false;int32 MedicinePaid=0;double SafeSince=-1;
    TMap<FGuid,FVector> PlacedHomes;
    bool CheckPlacedHomes() const
    {
        int32 Found=0;for(TActorIterator<ACreatureCharacter> It(World);It;++It)
            if(const auto* Home=PlacedHomes.Find(It->PersistentId))
            {if(!It->bHasHabitat || It->Tamer || !It->EncounterId.IsNone() || !It->HabitatHome.Equals(*Home,.01))return false;++Found;}
        return Found==PlacedHomes.Num();
    }
    bool Budget() const {return FStarterPrep::Budget(3-MedicinePaid);}
    bool End(bool Pass,const TCHAR* Reason)
    {
        if(Input.IsValid())Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::LeftShift);
        const auto Feedback=Hero->Feedback;if(!Pass)Hero->SaveProgress(Rescue?TEXT("StarterRescueBlocked"):TEXT("StarterScoutBlocked"));
        if(!Pass)for(TActorIterator<ACreatureCharacter> It(World);It;++It)
            if(It->Disposition!=ECreatureDisposition::Tamed && It->Health>0)
                UE_LOG(LogTemp,Display,TEXT("CF_STARTER_SCOUT_THREAT id=%s species=%s encounter=%s habitat=%d home=%s leash=%.1f activity=%d position=%s health=%.1f"),*It->PersistentId.ToString(),*It->GetSpeciesName(),*It->EncounterId.ToString(),It->bHasHabitat,*It->HabitatHome.ToString(),It->LeashRadius,static_cast<int32>(It->Activity),*It->GetActorLocation().ToString(),It->Health);
        UE_LOG(LogTemp,Display,TEXT("CF_STARTER_SCOUT %s reason=%s phase=%d heroTravel=%.1f petTravel=%.1f wildTravel=%.1f heroHP=%.1f petHP=%.1f bossHP=%.1f warning=%d hit=%d hero=%s pet=%s feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Reason,Phase,HeroTravel,PetTravel,WildTravel,Hero->Health,Pet.IsValid()?Pet->Health:-1,Boss.IsValid()?Boss->Health:-1,Warning,Hit,*Hero->GetActorLocation().ToString(),Pet.IsValid()?*Pet->GetActorLocation().ToString():TEXT("none"),*Feedback);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    bool Tick()
    {
        if(!Pet.IsValid() || !Boss.IsValid() || !Region.IsValid() || FPlatformTime::Seconds()>Deadline)return End(false,TEXT("lifetime"));
        const auto H=Hero->GetActorLocation(),P=Pet->GetActorLocation();const double Delta=World->GetTimeSeconds()-LastTime;
        if(!Motion(Hero,LastH,VelocityH,Delta) || !Motion(Pet.Get(),LastP,VelocityP,Delta) || Hero->Health<=0 || (Pet->Health<=0 && !(Rescue && MedicinePaid==0)) || !Pet->IsActorTickEnabled())return End(false,TEXT("motion-health"));
        HeroTravel+=FVector::Dist(H,LastH);PetTravel+=FVector::Dist(P,LastP);LastH=H;LastP=P;VelocityH=Hero->GetVelocity();VelocityP=Pet->GetVelocity();LastTime=World->GetTimeSeconds();
        int32 Ecology=0,Owned=0;for(TActorIterator<AWildEncounter> It(World);It;++It){++Ecology;if(!It->IsActorTickEnabled())return End(false,TEXT("ecology"));}
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)
        {
            if(It->Tamer==Hero->GetController())++Owned;
            if(It->Disposition!=ECreatureDisposition::Tamed && It->Health>0){if(!It->IsActorTickEnabled() || !It->GetActorEnableCollision() || It->GetCharacterMovement()->MovementMode==MOVE_None)return End(false,TEXT("wild-disabled"));if(const auto* Old=WildPositions.Find(It->PersistentId))WildTravel+=FVector::Dist(*Old,It->GetActorLocation());WildPositions.Add(It->PersistentId,It->GetActorLocation());}
        }
        if(Ecology!=3 || Owned!=1 || !Budget() || Boss->Health<=0 || !Boss->bAlpha)return End(false,TEXT("budget-home"));
        Warning|=Boss->AlphaWindup>0;Hit|=Boss->Health<BossHealth;
        if(World->GetTimeSeconds()>=NextDiagnostic)
        {
            NextDiagnostic=World->GetTimeSeconds()+2;
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_SCOUT_STATE phase=%d heroHP=%.1f petHP=%.1f stamina=%.1f nutrition=%.1f bossHP=%.1f petActivity=%d target=%s skillWindup=%.2f skillCooldown=%.2f bossWindup=%.2f hero=%s pet=%s boss=%s"),Phase,Hero->Health,Pet->Health,Pet->Stamina,Pet->Nutrition,Boss->Health,static_cast<int32>(Pet->Activity),Pet->FocusTarget?*Pet->FocusTarget->GetCreatureName():TEXT("none"),Pet->SpecialWindupRemaining,Pet->SpecialCooldownRemaining,Boss->AlphaWindup,*H.ToString(),*P.ToString(),*Boss->GetActorLocation().ToString());
        }
        if(FPlatformTime::Seconds()<ShotUntil)return true;
        if(Phase==-1)
        {
            if(Rescue && !Read){Phase=10;At=World->GetTimeSeconds();return true;}
            if(World->GetTimeSeconds()-At<.2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;
            if(Read){Phase=4;return true;}
            if(Photo(TEXT("Departure"),Region->GetActorLocation()))return true;
            const auto Approach=Region->GetActorLocation()+(Hero->GetActorLocation()-Region->GetActorLocation()).GetSafeNormal2D()*500;
            if(!Plan(Approach,0))return End(false,TEXT("approach-path"));return true;
        }
        if(Phase==0 || Phase==5)
        {
            if(Rescue && Phase==5 && Pet->Health<=40 && MedicinePaid<3)
            {
                Face(P);
                if(Hero->TraceCreature()==Pet.Get())
                {
                    const float Before=Pet->Health;
                    if(!CFInputQA::Press(Hero,EKeys::P) || !FMath::IsNearlyEqual(Pet->Health,FMath::Min(Pet->GetMaxHealth(),Before+40)))return End(false,TEXT("withdraw-heal"));
                    ++MedicinePaid;
                    UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RESCUE_SUPPORT PASS input=1 medicinePaid=%d before=%.1f after=%.1f"),MedicinePaid,Before,Pet->Health);
                }
            }
            const int32 Result=Move();if(Result<0)return End(false,TEXT("walk-blocked"));if(!Result)return true;
            Phase=Phase==0?1:6;At=World->GetTimeSeconds();return true;
        }
        if(Phase==1)
        {
            Face(Boss->GetActorLocation());
            if(Hero->TraceCreature()!=Boss.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("boss-sight"));return true;}
            if(Photo(TEXT("Contact"),Boss->GetActorLocation()))return true;
            if(!CFInputQA::Press(Hero,EKeys::Q) || Pet->FocusTarget!=Boss.Get() || !CFInputQA::Press(Hero,EKeys::G))return End(false,TEXT("combat-input"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_SCOUT_COMMAND PASS focus=1 special=1 samePet=1"));Phase=2;At=World->GetTimeSeconds();return true;
        }
        if(Phase==2)
        {
            if(!Hit){if(World->GetTimeSeconds()-At>15)return End(false,TEXT("combat-no-hit"));return true;}
            if(Photo(TEXT("Engaged"),Boss->GetActorLocation()))return true;
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_SCOUT_CONTACT PASS damage=%.1f warning=%d heroHP=%.1f petHP=%.1f"),BossHealth-Boss->Health,Warning,Hero->Health,Pet->Health);
            if(!CFInputQA::Press(Hero,EKeys::F8) || !Pet->bAvoidCombat || !CFInputQA::Press(Hero,EKeys::F) || !Hero->SaveProgress(TEXT("StarterScoutMid")))return End(false,TEXT("withdraw-input"));
            Phase=4;return true;
        }
        if(Phase==4)
        {
            if(!Pet->bAvoidCombat || !Plan(Bench->GetActorLocation()+FVector(210,-180,40),5))return End(false,TEXT("return-path"));
            // 救援后使用原普通移动速度撤离，不按住慢行键，也不改角色速度。
            if(Rescue && !CFInputQA::Release(Hero,EKeys::LeftShift))return End(false,TEXT("release-slow-walk"));return true;
        }
        if(Phase==10)
        {
            Face(P);if(Hero->TraceCreature()!=Pet.Get()){if(World->GetTimeSeconds()-At>2)return End(false,TEXT("rescue-sight"));return true;}
            if(Pet->Health!=0 || !CFInputQA::Press(Hero,EKeys::P) || Pet->Health!=FMath::Min(40.f,Pet->GetMaxHealth()) || Hero->Inventory->Count(EFrontierItem::Medicine)!=2)return End(false,TEXT("rescue-medicine"));
            MedicinePaid=1;
            if(!CFInputQA::Press(Hero,EKeys::F8) || !Pet->bAvoidCombat || !CFInputQA::Press(Hero,EKeys::F) || !Hero->SaveProgress(TEXT("StarterRescueMid")))return End(false,TEXT("rescue-follow"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RESCUE_HEAL PASS input=1 medicinePaid=1 restored=40 samePet=1 avoid=1"));Phase=4;return true;
        }
        if(Phase==6)
        {
            if(Pet->GetDistanceTo(Hero)>250 || Pet->Activity!=ECreatureActivity::Follow || Boss->Activity==ECreatureActivity::Combat){if(World->GetTimeSeconds()-At>25)return End(false,TEXT("regroup"));return true;}
            if(Rescue)
            {
                bool Threat=false;for(TActorIterator<ACreatureCharacter> It(World);It;++It)
                    if(It->Disposition!=ECreatureDisposition::Tamed && It->Health>0 && It->Activity!=ECreatureActivity::ReturnHome && (It->GetDistanceTo(Hero)<600 || It->GetDistanceTo(Pet.Get())<600))Threat=true;
                if(Threat){SafeSince=-1;if(World->GetTimeSeconds()-At>25)return End(false,TEXT("home-threat"));return true;}
                if(SafeSince<0)SafeSince=World->GetTimeSeconds();if(World->GetTimeSeconds()-SafeSince<2)return true;
            }
            if(Photo(TEXT("Returned"),P))return true;
            const float HP=Hero->Health,PHP=Pet->Health,BHP=Boss->Health;
            const TCHAR* Done=Rescue?TEXT("StarterRescueDone"):TEXT("StarterScoutDone");
            if((Rescue?(MedicinePaid<1 || MedicinePaid>3):!Hit) || (!Read && HeroTravel<(Rescue?1000:3000)) || !Hero->SaveProgress(Done) || !Hero->LoadProgress(Done))return End(false,TEXT("save"));
            Pet.Reset();Boss.Reset();Garden.Reset();Brush.Reset();
            for(TActorIterator<ACreatureCharacter> It(World);It;++It){if(It->PersistentId==PetId)Pet=*It;if(It->EncounterId==TEXT("CrystalRise") && It->bAlpha && It->Health>0)Boss=*It;}
            for(TActorIterator<AWorkStation> It(World);It;++It)if(It->PersistentId==OriginalGarden.Id)Garden=*It;
            for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,1))Brush=*It;
            if(!Pet.IsValid() || !Boss.IsValid() || !Hero->GetActorLocation().Equals(H,.01) || !Pet->GetActorLocation().Equals(P,.01) || Hero->Health!=HP || Pet->Health!=PHP || Boss->Health!=BHP || !Pet->bAvoidCombat || !Budget() || !CheckPlacedHomes())return End(false,TEXT("restore"));
            if(Rescue)UE_LOG(LogTemp,Display,TEXT("CF_PLACED_HABITAT_LIFE PASS count=%d positionRetained=1 homePersisted=1 safeSeconds=2"),PlacedHomes.Num());
            if(Rescue)UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RESCUE_BUDGET PASS medicinePaid=%d medicineRemaining=%d petHP=%.1f"),MedicinePaid,Hero->Inventory->Count(EFrontierItem::Medicine),Pet->Health);
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_SCOUT_LIFE PASS version=38 positionRetained=1 homeRetained=1 suppliesRetained=1 claimed=4 bossAlive=1 avoid=1"));return End(true,TEXT("complete"));
        }
        return true;
    }
};
}
void RunStarterScoutScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto S=MakeShared<FStarterScout>();S->World=World;S->Hero=Hero;S->Read=Read;S->Deadline=FPlatformTime::Seconds()+150;
    S->Rescue=FParse::Param(FCommandLine::Get(),TEXT("CFStarterRescueTest")) || FParse::Param(FCommandLine::Get(),TEXT("CFStarterRescueRead"));S->MedicinePaid=S->Rescue && Read?1:0;
    const TCHAR* InputSlot=S->Rescue?TEXT("StarterRescueInput"):TEXT("StarterScoutInput");
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(InputSlot,0));
    if(!Source || Source->FormatVersion!=38 || Source->HomeModules.Num()!=0 || Source->Facilities.Num()!=1 || !Hero->LoadProgress(InputSlot)){S->End(false,TEXT("source"));return;}
    if(S->Rescue)
    {
        const auto* Mode=World->GetAuthGameMode<ACFGameMode>();
        for(const auto& Record:Source->Creatures)
            if(!Record.bOwned && Record.EncounterId.IsNone())
                for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->PersistentId==Record.Id)
                {
                    FVector Origin;
                    if(!Mode || !Mode->ResolvePlacedCreatureHome(It->Definition,Origin) || !It->bHasHabitat || !It->HabitatHome.Equals(Origin,.01) || !It->GetActorTransform().Equals(Record.Transform,.01) || It->Health!=Record.Health || It->Level!=Record.Level || It->Experience!=Record.Experience || It->RoamRadius!=Record.RoamRadius || It->LeashRadius!=Record.LeashRadius){S->End(false,TEXT("placed-origin"));return;}
                    S->PlacedHomes.Add(Record.Id,Origin);
                }
        if(S->PlacedHomes.Num()!=2 || !S->CheckPlacedHomes()){S->End(false,TEXT("placed-count"));return;}
        UE_LOG(LogTemp,Display,TEXT("CF_PLACED_HABITAT_SOURCE PASS count=2 positionRetained=1 healthRetained=1 originalRanges=1"));
    }
    if(S->Rescue && Read)S->MedicinePaid=3-Hero->Inventory->Count(EFrontierItem::Medicine);
    S->OriginalGarden=Source->Facilities[0];int32 Owned=0;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It){if(It->Tamer==Hero->GetController()){S->Pet=*It;++Owned;}if(It->EncounterId==TEXT("CrystalRise") && It->bAlpha && It->Health>0)S->Boss=*It;}
    for(TActorIterator<AWildEncounter> It(World);It;++It)if(It->EncounterId==TEXT("CrystalRise") && It->bBoss)S->Region=*It;
    for(TActorIterator<AWorkStation> It(World);It;++It){if(It->PersistentId==S->OriginalGarden.Id)S->Garden=*It;if(!It->bPlayerBuilt && It->RequiredSpecialty==EWorkSpecialty::Gathering)S->Bench=*It;}
    for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,1))S->Brush=*It;
    const auto& Gear=Hero->Equipment->GetState();
    if(Owned!=1 || !S->Pet.IsValid() || !S->Boss.IsValid() || !S->Region.IsValid() || !S->Bench.IsValid() || !Gear.bSpearEquipped || !Gear.bVestEquipped || S->Pet->bAvoidCombat!=Read){S->End(false,TEXT("source-state"));return;}
    S->PetId=S->Pet->PersistentId;S->Nickname=S->Pet->Nickname;S->BossHealth=S->Boss->GetMaxHealth();S->InitialHeroHealth=Hero->Health;S->InitialPetHealth=S->Pet->Health;S->Hit=Read && S->Boss->Health<S->BossHealth;
    if(S->Rescue && ((!Read && S->Pet->Health!=0) || (Read && S->Pet->Health<=0) || S->Boss->Health!=S->BossHealth)){S->End(false,TEXT("rescue-source"));return;}
    const auto* Partner=Source->Creatures.FindByPredicate([&](const FCreatureSaveRecord& R){return R.Id==S->PetId;});
    if(!Partner || !Hero->GetActorLocation().Equals(Source->PlayerTransform.GetLocation(),.01) || !S->Pet->GetActorLocation().Equals(Partner->Transform.GetLocation(),.01) || !S->Budget()){S->End(false,TEXT("source-position-budget"));return;}
    S->LastH=Hero->GetActorLocation();S->LastP=S->Pet->GetActorLocation();S->VelocityH=Hero->GetVelocity();S->VelocityP=S->Pet->GetVelocity();S->LastTime=S->At=World->GetTimeSeconds();S->Input=NewObject<UHomeMovementProbe>(Hero);S->Input->RegisterComponent();
    S->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFStarterScoutPreview")) || FParse::Param(FCommandLine::Get(),TEXT("CFStarterRescuePreview"));if(S->Preview){S->Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/(S->Rescue?TEXT("StarterRescuePreview"):TEXT("StarterScoutPreview"))/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*S->Root,true);UE_LOG(LogTemp,Display,TEXT("CF_STARTER_SCOUT_PREVIEW_ROOT %s"),*S->Root);}
    UE_LOG(LogTemp,Display,TEXT("CF_STARTER_SCOUT_SOURCE PASS read=%d positionRetained=1 pet=%s bossHP=%.1f heroHP=%.1f petHP=%.1f stamina=%.1f nutrition=%.1f"),Read,*S->PetId.ToString(),S->Boss->Health,Hero->Health,S->Pet->Health,S->Pet->Stamina,S->Pet->Nutrition);FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){return S->Tick();}));
}

namespace
{
// 从真正药品耗尽的返家档恢复；伙伴在设施休息，玩家实际采集原料制作药品。
struct FStarterRecovery : FStarterPrep
{
    int32 Crafted=0,Used=0;bool Held=false;float InitialHealth=0;
    bool Rearm=false;int32 HarvestStart=14,HarvestGoal=10,MedicineAtStart=0;
    int32 RewardMaterials=0,RewardCaptures=0,RewardMedicine=0,ExpectedClaimed=4,ExpectedExperience=12,ExpectedLevel=1;bool ExpectAlphaResolved=false;
    FGuid RecoveryResourceId=FGuid(0xCF170000,0,0,1);
    bool End(bool Pass,const TCHAR* Reason)
    {
        if(Held)CFInputQA::Release(Hero,EKeys::M);if(Input.IsValid())Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::LeftShift);
        const auto Feedback=Hero->Feedback;if(!Pass)Hero->SaveProgress(TEXT("StarterRecoveryBlocked"));
        UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RECOVERY %s reason=%s phase=%d heroTravel=%.1f petTravel=%.1f wildTravel=%.1f heroHP=%.1f petHP=%.1f crafted=%d used=%d feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Reason,Phase,HeroTravel,PetTravel,WildTravel,Hero->Health,Pet.IsValid()?Pet->Health:-1,Crafted,Used,*Feedback);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    bool Budget() const
    {
        if(!Pet.IsValid() || !Brush.IsValid() || !Garden.IsValid())return false;const int32 Harvested=HarvestStart-Brush->Remaining;
        int32 Farms=0;for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt)++Farms;
        const auto& Gear=Hero->Equipment->GetState();
        return Harvested>=0 && Harvested<=HarvestGoal && Hero->Inventory->Count(EFrontierItem::Material)+5*Crafted==Harvested+RewardMaterials && Hero->Inventory->Count(EFrontierItem::Medicine)==MedicineAtStart+Crafted-Used+RewardMedicine &&
            Hero->Inventory->Count(EFrontierItem::Food)==2 && Hero->Inventory->Count(EFrontierItem::Seeds)==1 && Hero->Inventory->Count(EFrontierItem::CaptureDevice)==7+RewardCaptures && Hero->Inventory->Count(EFrontierItem::Fiber)==0 &&
            Pet->Experience==ExpectedExperience && Pet->Level==ExpectedLevel && Pet->BondPoints==3 && Pet->Nickname==Nickname && Pet->bAvoidCombat && Gear.bSpearEquipped && Gear.bVestEquipped && Hero->GetObjectives().Claimed==ExpectedClaimed && Hero->GetObjectives().bAlphaResolved==ExpectAlphaResolved &&
            Farms==1 && HomeLayout::Collect(World).IsEmpty() && Garden->PersistentId==OriginalGarden.Id && Garden->GetActorTransform().Equals(OriginalGarden.Transform,.01) && Garden->StoredFood==OriginalGarden.StoredFood && Garden->Crop->Seeds==OriginalGarden.Seeds && Garden->Crop->bPlanted==OriginalGarden.bPlanted && FMath::IsNearlyEqual(Garden->Crop->GrowthWork,OriginalGarden.GrowthWork,.01f);
    }
    bool Tick()
    {
        if(!Pet.IsValid() || !Bench.IsValid() || FPlatformTime::Seconds()>Deadline)return End(false,TEXT("lifetime"));
        const auto H=Hero->GetActorLocation(),P=Pet->GetActorLocation();const double Delta=World->GetTimeSeconds()-LastTime;
        if(!Motion(Hero,LastH,VelocityH,Delta) || !Motion(Pet.Get(),LastP,VelocityP,Delta) || Hero->Health<=0 || Pet->Health<=0 || !Pet->IsActorTickEnabled())return End(false,TEXT("motion-health"));
        HeroTravel+=FVector::Dist(H,LastH);PetTravel+=FVector::Dist(P,LastP);LastH=H;LastP=P;VelocityH=Hero->GetVelocity();VelocityP=Pet->GetVelocity();LastTime=World->GetTimeSeconds();
        int32 Ecology=0,Owned=0;for(TActorIterator<AWildEncounter> It(World);It;++It){++Ecology;if(!It->IsActorTickEnabled())return End(false,TEXT("ecology"));}
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)
        {
            if(It->Tamer==Hero->GetController())++Owned;
            if(It->Disposition!=ECreatureDisposition::Tamed && It->Health>0){if(!It->IsActorTickEnabled() || !It->GetActorEnableCollision() || It->GetCharacterMovement()->MovementMode==MOVE_None)return End(false,TEXT("wild-disabled"));if(const auto* Old=WildPositions.Find(It->PersistentId))WildTravel+=FVector::Dist(*Old,It->GetActorLocation());WildPositions.Add(It->PersistentId,It->GetActorLocation());}
        }
        if(Ecology!=3 || Owned!=1 || !Budget())return End(false,TEXT("budget-home"));if(FPlatformTime::Seconds()<ShotUntil)return true;
        if(Phase==-1)
        {
            if(World->GetTimeSeconds()-At<.2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;
            if(!Read)
            {
                if(!CFInputQA::Press(Hero,EKeys::T) || Pet->Activity!=ECreatureActivity::Rest || Pet->WorkSite!=Bench.Get() || !Hero->SaveProgress(TEXT("StarterRecoveryMid")))return End(false,TEXT("rest-input"));
                UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RECOVERY_REST PASS input=1 samePet=1 facility=1"));
            }
            Phase=8;return true;
        }
        if(Phase==8)
        {
            if(Photo(TEXT("Resting"),P))return true;
            if(!Plan(Brush->GetActorLocation()+(H-Brush->GetActorLocation()).GetSafeNormal2D()*180,0))return End(false,TEXT("gather-path"));return true;
        }
        if(Phase==0 || Phase==4)
        {
            const int32 Result=Move();if(Result<0)return End(false,TEXT("walk-blocked"));if(!Result)return true;Phase=Phase==0?1:5;At=World->GetTimeSeconds();return true;
        }
        if(Phase==1)
        {
            Face(Brush->GetActorLocation());if(Hero->TraceResource()!=Brush.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("gather-sight"));return true;}
            if(!CFInputQA::Press(Hero,EKeys::M) || !Hero->Gathering->IsGathering())return End(false,TEXT("gather-input"));Held=true;Phase=2;At=World->GetTimeSeconds();return true;
        }
        if(Phase==2)
        {
            if(Hero->Inventory->Count(EFrontierItem::Material)<HarvestGoal){if(!Hero->Gathering->IsGathering() || World->GetTimeSeconds()-At>30)return End(false,TEXT("gather-interrupted"));return true;}
            if(!CFInputQA::Release(Hero,EKeys::M) || Brush->Remaining!=HarvestStart-HarvestGoal)return End(false,TEXT("gather-count"));Held=false;
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RECOVERY_GATHER PASS input=1 material=%d sourcePaid=%d experience=12"),HarvestGoal,HarvestGoal);Photo(TEXT("Gathered"),Brush->GetActorLocation());Phase=3;return true;
        }
        if(Phase==3){if(!Plan(Bench->GetActorLocation()+FVector(210,-180,40),4))return End(false,TEXT("return-path"));return true;}
        if(Phase==5)
        {
            for(int32 Index=0;Index<HarvestGoal/5;++Index)
            {if(!CFInputQA::Press(Hero,EKeys::Two) || Hero->Inventory->Count(EFrontierItem::Medicine)!=MedicineAtStart+Index+1 || Hero->Inventory->Count(EFrontierItem::Material)!=HarvestGoal-5*(Index+1))return End(false,TEXT("craft-budget"));++Crafted;}
            if(!Hero->GetObjectiveHint().Contains(TEXT("H治疗自己")))return End(false,TEXT("medicine-hint"));Phase=9;return true;
        }
        if(Phase==9)
        {
            if(Photo(TEXT("MedicineReady"),P))return true;
            if(!CFInputQA::Press(Hero,EKeys::H) || Hero->Health!=100 || Hero->Inventory->Count(EFrontierItem::Medicine)!=1)return End(false,TEXT("heal-self"));++Used;
            if(!CFInputQA::Press(Hero,EKeys::H) || Hero->Inventory->Count(EFrontierItem::Medicine)!=1)return End(false,TEXT("full-health-reject"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RECOVERY_MEDICINE PASS crafted=%d materialPaid=%d selfUsed=1 retained=1 fullHealthReject=1"),Crafted,HarvestGoal);Photo(TEXT("Resupplied"),P);Phase=6;At=World->GetTimeSeconds();return true;
        }
        if(Phase==6)
        {
            if(Pet->Activity!=ECreatureActivity::Rest || Pet->WorkSite!=Bench.Get())return End(false,TEXT("rest-lost"));
            if(Pet->Health<Pet->GetMaxHealth() || Pet->Stamina<99){if(World->GetTimeSeconds()-At>30)return End(false,TEXT("rest-timeout"));return true;}
            if(!CFInputQA::Press(Hero,EKeys::F))return End(false,TEXT("follow-input"));Phase=7;At=World->GetTimeSeconds();return true;
        }
        if(Phase==7)
        {
            if(Pet->Activity!=ECreatureActivity::Follow || Pet->GetDistanceTo(Hero)>250){if(World->GetTimeSeconds()-At>20)return End(false,TEXT("regroup"));return true;}
            if(!Hero->GetObjectiveHint().Contains(TEXT("直线")))return End(false,TEXT("recovered-hint"));
            if(Photo(TEXT("Recovered"),P))return true;const float HP=Pet->Health,SP=Pet->Stamina;
            if(HeroTravel<300 || !Hero->SaveProgress(TEXT("StarterRecoveryDone")) || !Hero->LoadProgress(TEXT("StarterRecoveryDone")))return End(false,TEXT("save"));
            Pet.Reset();Garden.Reset();Brush.Reset();for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->PersistentId==PetId)Pet=*It;
            for(TActorIterator<AWorkStation> It(World);It;++It)if(It->PersistentId==OriginalGarden.Id)Garden=*It;for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==RecoveryResourceId)Brush=*It;
            if(!Pet.IsValid() || !Hero->GetActorLocation().Equals(H,.01) || !Pet->GetActorLocation().Equals(P,.01) || Hero->Health!=100 || Pet->Health!=HP || Pet->Stamina!=SP || !Budget())return End(false,TEXT("restore"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RECOVERY_HINTS PASS states=%d"),Rearm?2:3);
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RECOVERY_LIFE PASS version=38 positionRetained=1 heroHP=100 petFull=1 medicine=1 material=0 claimed=4"));return End(true,TEXT("complete"));
        }
        return true;
    }
};
}
void RunStarterRecoveryScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto S=MakeShared<FStarterRecovery>();S->World=World;S->Hero=Hero;S->Read=Read;S->Deadline=FPlatformTime::Seconds()+150;
    S->Rearm=FParse::Param(FCommandLine::Get(),TEXT("CFStarterRecoveryRearm"));
    // 战斗返家档保留原备用药，采原有石料补回本次治疗消耗，不重置灌木历史。
    if(S->Rearm){S->RecoveryResourceId=FGuid(0xCF170000,0,0,2);S->HarvestGoal=5;S->MedicineAtStart=1;}
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("StarterRecoveryInput"),0));
    if(!Source || Source->FormatVersion!=38 || Source->HomeModules.Num()!=0 || Source->Facilities.Num()!=1 || !Hero->LoadProgress(TEXT("StarterRecoveryInput"))){S->End(false,TEXT("source"));return;}
    S->OriginalGarden=Source->Facilities[0];int32 Owned=0;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController()){S->Pet=*It;++Owned;}
    for(TActorIterator<AWorkStation> It(World);It;++It){if(It->PersistentId==S->OriginalGarden.Id)S->Garden=*It;if(!It->bPlayerBuilt && It->RequiredSpecialty==EWorkSpecialty::Gathering)S->Bench=*It;}
    for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==S->RecoveryResourceId)S->Brush=*It;
    if(S->Rearm && S->Brush.IsValid())S->HarvestStart=S->Brush->Remaining;
    if(Owned!=1 || !S->Pet.IsValid() || !S->Bench.IsValid() || !S->Brush.IsValid() || (!S->Rearm && S->Brush->Remaining!=14) || S->Brush->Remaining<S->HarvestGoal || !S->Brush->Definition || S->Brush->Definition->OutputItem!=EFrontierItem::Material || Hero->Health>=100 || S->Pet->Health<=0 || (!S->Rearm && S->Pet->Health>=S->Pet->GetMaxHealth()) || (Read && (S->Pet->Activity!=ECreatureActivity::Rest || S->Pet->WorkSite!=S->Bench.Get()))){S->End(false,TEXT("source-state"));return;}
    S->PetId=S->Pet->PersistentId;S->Nickname=S->Pet->Nickname;S->InitialHealth=Hero->Health;const auto* Partner=Source->Creatures.FindByPredicate([&](const FCreatureSaveRecord& R){return R.Id==S->PetId;});
    const auto* Catalog=UCraftingCatalog::Load();const auto* Recipe=Catalog?Catalog->Find(TEXT("Medicine")):nullptr;
    if(!Partner || !Hero->GetActorLocation().Equals(Source->PlayerTransform.GetLocation(),.01) || !S->Pet->GetActorLocation().Equals(Partner->Transform.GetLocation(),.01) || !Recipe || Recipe->Inputs.Num()!=1 || Recipe->Inputs.FindRef(EFrontierItem::Material)!=5 || Recipe->OutputAmount!=1 || !S->Budget()){S->End(false,TEXT("source-position-budget"));return;}
    if(!Hero->GetObjectiveHint().Contains(S->Rearm?TEXT("H治疗自己"):TEXT("缺药"))){S->End(false,TEXT("source-recovery-hint"));return;}
    S->LastH=Hero->GetActorLocation();S->LastP=S->Pet->GetActorLocation();S->VelocityH=Hero->GetVelocity();S->VelocityP=S->Pet->GetVelocity();S->LastTime=S->At=World->GetTimeSeconds();S->Input=NewObject<UHomeMovementProbe>(Hero);S->Input->RegisterComponent();
    S->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFStarterRecoveryPreview"));if(S->Preview){S->Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("StarterRecoveryPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*S->Root,true);UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RECOVERY_PREVIEW_ROOT %s"),*S->Root);}
    UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RECOVERY_SOURCE PASS read=%d positionRetained=1 pet=%s"),Read,*S->PetId.ToString());FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){return S->Tick();}));
}

namespace
{
// 沿真实导航绕行旧烬尾兽出生点；所有敌人仍正常警戒、追击、攻击和归巢。
struct FStarterFlank : FStarterRecovery
{
    bool Melee=false,MeleeSent=false,MeleeConfirmed=false;
    bool Defeat=false,Victory=false;double NextStrike=0;int32 Strikes=1;
    bool Dodge=false,JumpSent=false,JumpReleased=false,DodgeResolved=false;float JumpHP=0;
    TWeakObjectPtr<ACreatureCharacter> Boss;TWeakObjectPtr<AWildEncounter> Region;
    int32 Leg=0;bool Hit=false,SkillStarted=false;float DamageSeen=0;double SafeAt=-1;
    FVector Goal(bool Returning,int32 Index) const
    {
        if(Returning)return Index==0?FVector(750,1650,40):Index==1?FVector(-400,1650,40):Bench->GetActorLocation()+FVector(210,-180,40);
        return Index==0?FVector(-400,1650,40):Index==1?FVector(750,1650,40):Region->GetActorLocation()+FVector(-450,300,90);
    }
    bool Walk(bool Returning,int32 Index)
    {return Plan(Goal(Returning,Index),Returning?4:0) && CFInputQA::Release(Hero,EKeys::LeftShift);}
    bool End(bool Pass,const TCHAR* Reason)
    {
        if(Input.IsValid())Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::LeftShift);
        const auto Feedback=Hero->Feedback;if(!Pass)Hero->SaveProgress(TEXT("StarterFlankBlocked"));
        UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FLANK %s reason=%s phase=%d leg=%d heroTravel=%.1f petTravel=%.1f wildTravel=%.1f heroHP=%.1f petHP=%.1f bossHP=%.1f damage=%.1f skillStarted=%d hero=%s pet=%s feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Reason,Phase,Leg,HeroTravel,PetTravel,WildTravel,Hero->Health,Pet.IsValid()?Pet->Health:-1,Boss.IsValid()?Boss->Health:-1,DamageSeen,SkillStarted,*Hero->GetActorLocation().ToString(),Pet.IsValid()?*Pet->GetActorLocation().ToString():TEXT("none"),*Feedback);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    bool Tick()
    {
        if(!Pet.IsValid() || !Boss.IsValid() || !Region.IsValid() || !Bench.IsValid() || FPlatformTime::Seconds()>Deadline)return End(false,TEXT("lifetime"));
        const auto H=Hero->GetActorLocation(),P=Pet->GetActorLocation();const double Delta=World->GetTimeSeconds()-LastTime;
        if(!Motion(Hero,LastH,VelocityH,Delta) || !Motion(Pet.Get(),LastP,VelocityP,Delta) || Hero->Health<=0 || Pet->Health<=0 || !Pet->IsActorTickEnabled())return End(false,TEXT("motion-health"));
        HeroTravel+=FVector::Dist(H,LastH);PetTravel+=FVector::Dist(P,LastP);LastH=H;LastP=P;VelocityH=Hero->GetVelocity();VelocityP=Pet->GetVelocity();LastTime=World->GetTimeSeconds();
        int32 Ecology=0,Owned=0;for(TActorIterator<AWildEncounter> It(World);It;++It){++Ecology;if(!It->IsActorTickEnabled())return End(false,TEXT("ecology"));}
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)
        {
            if(It->Tamer==Hero->GetController())++Owned;
            if(It->Disposition!=ECreatureDisposition::Tamed && It->Health>0)
            {if(!It->IsActorTickEnabled() || !It->GetActorEnableCollision() || It->GetCharacterMovement()->MovementMode==MOVE_None)return End(false,TEXT("wild-disabled"));if(const auto* Old=WildPositions.Find(It->PersistentId))WildTravel+=FVector::Dist(*Old,It->GetActorLocation());WildPositions.Add(It->PersistentId,It->GetActorLocation());}
        }
        if(Defeat && Boss->Health<=0 && !Victory)
        {
            // 只接受原首领击败的两种经验分配；不写入生命、经验或目标状态。
            if(!Hero->GetObjectives().bAlphaResolved || Pet->Level!=2 || (Pet->Experience!=37 && Pet->Experience!=162))return End(false,TEXT("victory-experience"));
            ExpectedLevel=2;ExpectedExperience=Pet->Experience;ExpectAlphaResolved=true;
            int32 Drops=0,Materials=0,Medicine=0;for(TActorIterator<AWorldLoot> It(World);It;++It){++Drops;Materials+=It->Materials;Medicine+=It->Medicine;}
            if(Drops>1 || Materials+Hero->Inventory->Count(EFrontierItem::Material)!=8 || Medicine+Hero->Inventory->Count(EFrontierItem::Medicine)!=2)return End(false,TEXT("victory-loot"));
            // 站在原220厘米拾取范围内等普通物资包更新，不能直接调用收取或改库存。
            if(Drops){if(World->GetTimeSeconds()-At>15)return End(false,TEXT("victory-loot-timeout"));return true;}
            if(!CFInputQA::Press(Hero,EKeys::N) || Hero->GetObjectives().Claimed!=5 || Hero->Inventory->Count(EFrontierItem::Material)!=28 || Hero->Inventory->Count(EFrontierItem::CaptureDevice)!=10)return End(false,TEXT("victory-reward"));
            RewardMaterials=28;RewardMedicine=1;RewardCaptures=3;ExpectedClaimed=5;Victory=true;
            if(!CFInputQA::Press(Hero,EKeys::N) || Hero->Inventory->Count(EFrontierItem::Material)!=28 || Hero->Inventory->Count(EFrontierItem::CaptureDevice)!=10)return End(false,TEXT("victory-duplicate"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_VICTORY PASS bossDown=1 claimed=5 materialReward=20 captureReward=3 duplicateReject=1 loot=1 experience=%d strikes=%d"),ExpectedExperience,Strikes);
        }
        if(Ecology!=3 || Owned!=1 || !Budget() || (Boss->Health<=0 && !Defeat) || !Boss->bAlpha)return End(false,TEXT("budget-home"));
        if(Pet->SpecialWindupRemaining>0)SkillStarted=true;
        if(Phase==2 && Boss->Health<Boss->GetMaxHealth()){Hit=true;DamageSeen=FMath::Max(DamageSeen,Boss->GetMaxHealth()-Boss->Health);}
        if(Dodge && !DodgeResolved)
        {
            const float Distance=FVector::Dist2D(H,Boss->GetActorLocation());
            if(!JumpSent && Boss->AlphaWindup>0 && Boss->AlphaWindup<=.45f && Distance<=380 && Hero->GetCharacterMovement()->IsMovingOnGround())
            {
                if(!CFInputQA::Action(Hero,TEXT("Jump"),EKeys::SpaceBar))return End(false,TEXT("dodge-input"));
                JumpSent=true;JumpHP=Hero->Health;
                UE_LOG(LogTemp,Display,TEXT("CF_STARTER_DODGE_INPUT PASS remaining=%.3f distance=%.2f health=%.2f"),Boss->AlphaWindup,Distance,JumpHP);
            }
            else if(JumpSent && !JumpReleased)
            {if(!CFInputQA::Action(Hero,TEXT("Jump"),EKeys::SpaceBar,IE_Released))return End(false,TEXT("dodge-release"));JumpReleased=true;}
            if(JumpSent && Boss->AlphaWindup<=0)
            {
                const float Height=H.Z-Boss->GetActorLocation().Z;
                if(Distance>380 || Height<=65 || Hero->Health!=JumpHP)return End(false,TEXT("dodge-resolution"));
                DodgeResolved=true;
                UE_LOG(LogTemp,Display,TEXT("CF_STARTER_DODGE PASS input=1 inRange=1 above=1 healthRetained=1"));
            }
        }
        if(World->GetTimeSeconds()>=NextDiagnostic){NextDiagnostic=World->GetTimeSeconds()+3;UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FLANK_STATE phase=%d leg=%d hero=%s pet=%s boss=%s heroHP=%.1f petHP=%.1f bossHP=%.1f activity=%d stamina=%.1f windup=%.2f"),Phase,Leg,*H.ToString(),*P.ToString(),*Boss->GetActorLocation().ToString(),Hero->Health,Pet->Health,Boss->Health,static_cast<int32>(Pet->Activity),Pet->Stamina,Pet->SpecialWindupRemaining);}
        if(Phase==9){const int32 Result=Move();if(Result<0)return End(false,TEXT("player-melee-walk"));if(Result>0)Phase=2;return true;}
        if(FPlatformTime::Seconds()<ShotUntil)return true;
        if(Phase==-1)
        {
            if(World->GetTimeSeconds()-At<.2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;
            if(Read){Leg=0;Phase=3;return true;}
            if(Photo(TEXT("Departure"),P))return true;
            if(!Walk(false,0))return End(false,TEXT("outward-path"));return true;
        }
        if(Phase==0 || Phase==4)
        {
            const bool Returning=Phase==4;const int32 Result=Move();if(Result<0)return End(false,TEXT("walk-blocked"));if(!Result)return true;
            if(Leg<2){++Leg;if(!Walk(Returning,Leg))return End(false,TEXT("leg-path"));return true;}
            Phase=Returning?5:1;At=World->GetTimeSeconds();return true;
        }
        if(Phase==1)
        {
            Face(Boss->GetActorLocation());if(Hero->TraceCreature()!=Boss.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("boss-sight"));return true;}
            if(Photo(TEXT("Contact"),Boss->GetActorLocation()))return true;
            // 避战不阻止原显式集火／技能；不更改伙伴的自动支援姿态。
            if(!CFInputQA::Press(Hero,EKeys::Q) || Pet->FocusTarget!=Boss.Get() || !CFInputQA::Press(Hero,EKeys::G))return End(false,TEXT("combat-input"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FLANK_COMMAND PASS focus=1 specialBinding=1 samePet=1 avoid=1"));Phase=2;At=World->GetTimeSeconds();return true;
        }
        if(Phase==2)
        {
            // 此处只在北侧接敌近战专项中驱动原移动和左键绑定，不直接结算伤害。
            if(Melee && !MeleeConfirmed)
            {
                if(World->GetTimeSeconds()-At>15)return End(false,TEXT("player-melee-timeout"));
                if(!MeleeSent)
                {
                    if(Hero->GetDistanceTo(Boss.Get())>280)
                    {
                        const FVector Away=(Hero->GetActorLocation()-Boss->GetActorLocation()).GetSafeNormal2D();
                        if(!Plan(Boss->GetActorLocation()+Away*240,9))return End(false,TEXT("player-melee-path"));
                        CFInputQA::Release(Hero,EKeys::LeftShift);return true;
                    }
                    Face(Boss->GetActorLocation());
                    if(!CFInputQA::Action(Hero,TEXT("Attack"),EKeys::LeftMouseButton) || !Hero->IsMeleePending())return End(false,TEXT("player-melee-input"));
                    MeleeSent=true;return true;
                }
                if(Hero->IsMeleePending())return true;
                if(!Hero->Feedback.Contains(TEXT("近战命中")))return End(false,TEXT("player-melee-missed"));
                MeleeConfirmed=true;
                NextStrike=World->GetTimeSeconds()+.6;
                UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FLANK_MELEE PASS input=1 pending=1 boss=%s pet=%s"),*Boss->PersistentId.ToString(),*PetId.ToString());
            }
            // 玩家先命中不能替代伙伴成果；有界等待原伙伴继续原集火／技能。
            if(Melee && (!SkillStarted || DamageSeen<=35))
            {if(World->GetTimeSeconds()-At>15)return End(false,TEXT("joint-combat-no-partner-hit"));return true;}
            if(Dodge && !DodgeResolved){if(World->GetTimeSeconds()-At>15)return End(false,TEXT("dodge-not-observed"));return true;}
            if(Defeat && !Victory)
            {
                if(World->GetTimeSeconds()-At>15)return End(false,TEXT("victory-timeout"));
                Face(Boss->GetActorLocation());
                if(!Hero->IsMeleePending() && World->GetTimeSeconds()>=NextStrike)
                {if(!CFInputQA::Action(Hero,TEXT("Attack"),EKeys::LeftMouseButton) || !Hero->IsMeleePending())return End(false,TEXT("victory-strike"));++Strikes;NextStrike=World->GetTimeSeconds()+.6;}
                return true;
            }
            if(!Hit){if(World->GetTimeSeconds()-At>15)return End(false,TEXT("combat-no-hit"));return true;}
            if(Photo(TEXT("Engaged"),Boss->GetActorLocation()))return true;
            // 技能可以在双方尚未普攻时命中；等待真实攻击产生冷却，不伪造正值检查点。
            bool HasMeleeCooldown=false;for(TActorIterator<ACreatureCharacter> It(World);It;++It)HasMeleeCooldown|=It->GetAttackCooldownRemaining()>0;
            if(!Defeat && !HasMeleeCooldown){if(World->GetTimeSeconds()-At>15)return End(false,TEXT("combat-no-melee-cooldown"));return true;}
            if(!CFInputQA::Press(Hero,EKeys::F) || Pet->FocusTarget || !Pet->bAvoidCombat || !Hero->SaveProgress(TEXT("StarterFlankMid")))return End(false,TEXT("withdraw-input"));
            const auto* Mid=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("StarterFlankMid"),0));int32 Positive=0,Checked=0;
            if(!Mid)return End(false,TEXT("cooldown-mid"));
            for(const auto& Record:Mid->Creatures)for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->PersistentId==Record.Id)
            {if(Record.CombatCooldownVersion!=1 || Record.MeleeCooldown!=It->GetAttackCooldownRemaining())return End(false,TEXT("cooldown-mid-value"));++Checked;if(Record.MeleeCooldown>0)++Positive;}
            if(Checked!=Mid->Creatures.Num() || (!Defeat && Positive==0))return End(false,TEXT("cooldown-mid-positive"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FLANK_COOLDOWN_MID PASS revision=1 positive=%d exact=1"),Positive);
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FLANK_CONTACT PASS damage=%.1f skillStarted=%d medicineUsed=0"),DamageSeen,SkillStarted);Leg=0;Phase=3;return true;
        }
        if(Phase==3){if(!Walk(true,0))return End(false,TEXT("return-path"));return true;}
        if(Phase==5)
        {
            bool Threat=false;for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Disposition!=ECreatureDisposition::Tamed && It->Health>0 && It->Activity!=ECreatureActivity::ReturnHome && (It->GetDistanceTo(Hero)<600 || It->GetDistanceTo(Pet.Get())<600))Threat=true;
            if(Threat || Pet->GetDistanceTo(Hero)>250 || Pet->Activity!=ECreatureActivity::Follow){SafeAt=-1;if(World->GetTimeSeconds()-At>25)return End(false,TEXT("unsafe-home"));return true;}
            if(SafeAt<0)SafeAt=World->GetTimeSeconds();if(World->GetTimeSeconds()-SafeAt<2)return true;
            if(Photo(TEXT("Returned"),P))return true;const float HP=Hero->Health,PHP=Pet->Health,BHP=Boss->Health;
            if(!Hit || (!Read && (HeroTravel<5000 || !SkillStarted)) || !Hero->SaveProgress(TEXT("StarterFlankDone")) || !Hero->LoadProgress(TEXT("StarterFlankDone")))return End(false,TEXT("save"));
            Pet.Reset();Garden.Reset();Brush.Reset();Boss.Reset();for(TActorIterator<ACreatureCharacter> It(World);It;++It){if(It->PersistentId==PetId)Pet=*It;if(It->EncounterId==TEXT("CrystalRise") && It->bAlpha && (It->Health>0 || Defeat))Boss=*It;}
            for(TActorIterator<AWorkStation> It(World);It;++It)if(It->PersistentId==OriginalGarden.Id)Garden=*It;for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,1))Brush=*It;
            if(!Pet.IsValid() || !Boss.IsValid() || Boss->Health!=BHP || !Hero->GetActorLocation().Equals(H,.01) || !Pet->GetActorLocation().Equals(P,.01) || Hero->Health!=HP || Pet->Health!=PHP || !Budget())return End(false,TEXT("restore"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FLANK_LIFE PASS version=38 positionRetained=1 homeRetained=1 medicine=%d claimed=%d safeSeconds=2"),1+RewardMedicine,ExpectedClaimed);return End(true,TEXT("complete"));
        }
        return true;
    }
};
}
void RunStarterFlankScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    if(Read && FParse::Param(FCommandLine::Get(),TEXT("CFStarterFlankDefeat")))
    {
        // 冷启动验收已完成的真实胜利档，不重新生成敌人或重打战斗。
        auto End=[](bool Pass){UE_LOG(LogTemp,Display,TEXT("CF_STARTER_VICTORY_READ %s version=38 claimed=5 alpha=1 duplicateReject=1 positionRetained=1"),Pass?TEXT("PASS"):TEXT("FAIL"));FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);};
        const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("StarterFlankInput"),0));
        if(!Source || Source->FormatVersion!=38 || !Hero->LoadProgress(TEXT("StarterFlankInput"))){End(false);return;}
        bool Pass=Hero->Health>0 && Hero->GetActorTransform().Equals(Source->PlayerTransform,.01) && Hero->GetObjectives().Claimed==5 && Hero->GetObjectives().bAlphaResolved && Hero->Inventory->Count(EFrontierItem::Material)==28 && Hero->Inventory->Count(EFrontierItem::Medicine)==2 && Hero->Inventory->Count(EFrontierItem::CaptureDevice)==10;
        int32 Matched=0,Owned=0,Bosses=0;
        for(const auto& Record:Source->Creatures)for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->PersistentId==Record.Id)
        {
            ++Matched;Pass &= It->GetActorTransform().Equals(Record.Transform,.01) && It->Health==Record.Health;
            if(It->Tamer==Hero->GetController()){++Owned;Pass &= It->Level==2 && (It->Experience==37 || It->Experience==162) && It->Health>0;}
            if(It->bAlpha && It->EncounterId==TEXT("CrystalRise")){++Bosses;Pass &= It->Health==0;}
        }
        int32 Drops=0;for(TActorIterator<AWorldLoot> It(World);It;++It)++Drops;
        Pass &= Matched==Source->Creatures.Num() && Owned==1 && Bosses==1 && Drops==0;
        const auto Items=Hero->Inventory->GetStacks();
        Pass &= CFInputQA::Press(Hero,EKeys::N) && Hero->Inventory->GetStacks().OrderIndependentCompareEqual(Items) && Hero->SaveProgress(TEXT("StarterFlankDone")) && Hero->LoadProgress(TEXT("StarterFlankDone")) && Hero->GetObjectives().Claimed==5 && Hero->GetObjectives().bAlphaResolved && Hero->Inventory->GetStacks().OrderIndependentCompareEqual(Items);
        End(Pass);return;
    }
    auto S=MakeShared<FStarterFlank>();S->World=World;S->Hero=Hero;S->Read=Read;S->Deadline=FPlatformTime::Seconds()+150;
    // 只继承真实恢复档中既有的材料账：此前制2药、用1药，本段不再次制作。
    S->Crafted=2;S->Used=1;S->Melee=FParse::Param(FCommandLine::Get(),TEXT("CFStarterFlankMelee"));
    S->Dodge=FParse::Param(FCommandLine::Get(),TEXT("CFStarterFlankDodge"));
    S->Defeat=FParse::Param(FCommandLine::Get(),TEXT("CFStarterFlankDefeat"));
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("StarterFlankInput"),0));
    if(!Source || Source->FormatVersion!=38 || Source->Facilities.Num()!=1 || !Source->HomeModules.IsEmpty() || !Hero->LoadProgress(TEXT("StarterFlankInput"))){S->End(false,TEXT("source"));return;}
    int32 LegacyCooldowns=0,CurrentCooldowns=0;
    for(const auto& Record:Source->Creatures)for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->PersistentId==Record.Id)
    {
        const float Expected=Record.CombatCooldownVersion==1?Record.MeleeCooldown:It->AttackInterval;
        if(It->GetAttackCooldownRemaining()!=Expected){S->End(false,TEXT("source-cooldown"));return;}
        if(Record.CombatCooldownVersion==1)++CurrentCooldowns;else ++LegacyCooldowns;
    }
    if(LegacyCooldowns+CurrentCooldowns!=Source->Creatures.Num()){S->End(false,TEXT("source-cooldown-count"));return;}
    UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FLANK_COOLDOWN_SOURCE PASS legacy=%d current=%d exact=1"),LegacyCooldowns,CurrentCooldowns);
    S->OriginalGarden=Source->Facilities[0];int32 Owned=0;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It){if(It->Tamer==Hero->GetController()){S->Pet=*It;++Owned;}if(It->EncounterId==TEXT("CrystalRise") && It->bAlpha && It->Health>0)S->Boss=*It;}
    for(TActorIterator<AWildEncounter> It(World);It;++It)if(It->EncounterId==TEXT("CrystalRise") && It->bBoss)S->Region=*It;
    for(TActorIterator<AWorkStation> It(World);It;++It){if(It->PersistentId==S->OriginalGarden.Id)S->Garden=*It;if(!It->bPlayerBuilt && It->RequiredSpecialty==EWorkSpecialty::Gathering)S->Bench=*It;}
    for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,1))S->Brush=*It;
    if(Owned!=1 || !S->Pet.IsValid() || !S->Boss.IsValid() || !S->Region.IsValid() || !S->Bench.IsValid() || (!Read && (Hero->Health!=100 || S->Pet->Health!=S->Pet->GetMaxHealth()))){S->End(false,TEXT("source-state"));return;}
    S->PetId=S->Pet->PersistentId;S->Nickname=S->Pet->Nickname;S->Hit=Read && S->Boss->Health<S->Boss->GetMaxHealth();S->DamageSeen=S->Hit?S->Boss->GetMaxHealth()-S->Boss->Health:0;
    if(Read && !S->Hit){S->End(false,TEXT("source-no-contact"));return;}
    const auto* Partner=Source->Creatures.FindByPredicate([&](const FCreatureSaveRecord& R){return R.Id==S->PetId;});
    if(!Partner || !Hero->GetActorLocation().Equals(Source->PlayerTransform.GetLocation(),.01) || !S->Pet->GetActorLocation().Equals(Partner->Transform.GetLocation(),.01) || !S->Budget()){S->End(false,TEXT("source-position-budget"));return;}
    S->LastH=Hero->GetActorLocation();S->LastP=S->Pet->GetActorLocation();S->VelocityH=Hero->GetVelocity();S->VelocityP=S->Pet->GetVelocity();S->LastTime=S->At=World->GetTimeSeconds();S->Input=NewObject<UHomeMovementProbe>(Hero);S->Input->RegisterComponent();
    S->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFStarterFlankPreview"));if(S->Preview){S->Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("StarterFlankPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*S->Root,true);UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FLANK_PREVIEW_ROOT %s"),*S->Root);}
    UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FLANK_SOURCE PASS read=%d positionRetained=1 pet=%s"),Read,*S->PetId.ToString());FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){return S->Tick();}));
}

namespace
{
struct FStarterShelter : FStarterJourney
{
    TWeakObjectPtr<AHomeModule> Floor,Box,Site;TWeakObjectPtr<AResourceNode> Stone;
    FHomeModuleRecord OriginalFloor,OriginalBox;FFacilitySaveRecord Garden;
    FString Nickname;int32 Part=0;bool Gathering=false,SavedMid=false;
    FVector Position()const{return Part==0?FVector(0,-200,-30):Part==1?FVector(0,-600,-30):FVector(0,-400,280);}
    FVector Observer()const{return FVector(-350,Part==0?-200:Part==1?-600:-400,40);}
    FVector Aim()const{return Part==0?FVector(0,-220,-30):Part==1?FVector(0,-580,-30):FVector(0,-400,-30);}
    bool End(bool Pass,const TCHAR* Reason)
    {
        if(Input.IsValid())Input->Direction=FVector::ZeroVector;if(Gathering)CFInputQA::Release(Hero,EKeys::M);CFInputQA::Release(Hero,EKeys::LeftShift);
        if(auto* PC=Cast<APlayerController>(Hero->GetController()))PC->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::RightShift,IE_Released,0.f,false,FPlatformTime::Cycles64()));
        if(!Pass)Hero->SaveProgress(TEXT("StarterShelterBlocked"));
        UE_LOG(LogTemp,Display,TEXT("CF_STARTER_SHELTER %s reason=%s phase=%d part=%d heroTravel=%.1f petTravel=%.1f stock=%d cargo=%d feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Reason,Phase,Part,HeroTravel,PetTravel,Box.IsValid()?Box->StorageInventory->Count(EFrontierItem::Material):-1,Pet.IsValid()?Pet->Cargo->Count():-1,*Hero->Feedback);FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    void FindActors()
    {
        Pet.Reset();Floor.Reset();Box.Reset();Stone.Reset();
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->PersistentId==PetId)Pet=*It;
        for(TActorIterator<AHomeModule> It(World);It;++It){if(It->PersistentId==OriginalFloor.Id)Floor=*It;if(It->PersistentId==OriginalBox.Id)Box=*It;}
        for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,2))Stone=*It;
    }
    bool Budget()const
    {
        if(!Pet.IsValid() || !Floor.IsValid() || !Box.IsValid() || !Stone.IsValid())return false;
        const int32 Harvested=25-Stone->Remaining;int32 Total=Hero->Inventory->Count(EFrontierItem::Material)+Pet->Cargo->Count(),Farms=0;
        for(TActorIterator<AHomeModule> It(World);It;++It)if(!It->bPreview && !It->bStaged)Total+=It->PaidMaterials+It->StorageInventory->Count(EFrontierItem::Material);
        for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt){++Farms;if(It->PersistentId!=Garden.Id || !It->GetActorTransform().Equals(Garden.Transform,.01) || It->StoredFood!=Garden.StoredFood || It->Crop->Seeds!=Garden.Seeds || It->Crop->bPlanted!=Garden.bPlanted || !FMath::IsNearlyEqual(It->Crop->GrowthWork,Garden.GrowthWork,.01))return false;}
        return Harvested>=0 && Harvested<=8 && Total==28+Harvested && Farms==1 && Hero->Health==100 && Pet->Health>0 && Pet->Level==2 && Pet->Experience==37 && Pet->BondPoints==3 && Pet->Nickname==Nickname && Hero->GetObjectives().Claimed==5 && Hero->GetObjectives().bAlphaResolved && Hero->Inventory->Count(EFrontierItem::Medicine)==0 && Hero->Inventory->Count(EFrontierItem::CaptureDevice)==10 && Hero->Inventory->Count(EFrontierItem::Food)==2 && Hero->Inventory->Count(EFrontierItem::Seeds)==1 && Floor->GetActorTransform().Equals(OriginalFloor.Transform,.01) && Box->GetActorTransform().Equals(OriginalBox.Transform,.01);
    }
    bool Tick()
    {
        if(!Budget() || FPlatformTime::Seconds()>Deadline)return End(false,TEXT("budget-lifetime"));
        const auto H=Hero->GetActorLocation(),P=Pet->GetActorLocation();const double Delta=World->GetTimeSeconds()-LastTime;
        if(!Motion(Hero,LastH,VelocityH,Delta) || !Motion(Pet.Get(),LastP,VelocityP,Delta) || !Pet->IsActorTickEnabled())return End(false,TEXT("motion"));
        HeroTravel+=FVector::Dist(H,LastH);PetTravel+=FVector::Dist(P,LastP);LastH=H;LastP=P;VelocityH=Hero->GetVelocity();VelocityP=Pet->GetVelocity();LastTime=World->GetTimeSeconds();
        int32 Regions=0;for(TActorIterator<AWildEncounter> It(World);It;++It){++Regions;if(!It->IsActorTickEnabled())return End(false,TEXT("ecology"));}if(Regions!=3)return End(false,TEXT("ecology-count"));
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Health>0 && (!It->IsActorTickEnabled() || !It->GetActorEnableCollision() || It->GetCharacterMovement()->MovementMode==MOVE_None))return End(false,TEXT("disabled"));
        if(!SavedMid && Part==0 && Site.IsValid() && Pet->Cargo->Count()==3)
        {if(!Hero->SaveProgress(TEXT("StarterShelterMid")))return End(false,TEXT("mid-save"));SavedMid=true;UE_LOG(LogTemp,Display,TEXT("CF_STARTER_SHELTER_MID PASS carried=3 paid=14 total=36"));}
        if(FPlatformTime::Seconds()<ShotUntil)return true;
        if(Phase==-1)
        {
            if(UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;
            if(!Plan(Read?FVector(-280,-350,40):FVector(-470,-60,40),Read?10:0))return End(false,TEXT("initial-path"));return true;
        }
        if(Phase==0 || Phase==3 || Phase==5 || Phase==10 || Phase==11 || Phase==12)
        {const int32 Result=Move();if(Result<0)return End(false,TEXT("walk"));if(!Result)return true;Phase=Phase==0?1:Phase==3?4:Phase==5?6:Phase==10?14:Phase==11?8:13;At=World->GetTimeSeconds();return true;}
        if(Phase==1)
        {Face(Stone->GetActorLocation());if(Hero->TraceResource()!=Stone.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("gather-sight"));return true;}if(!CFInputQA::Press(Hero,EKeys::M) || !Hero->Gathering->IsGathering())return End(false,TEXT("gather-input"));Gathering=true;Phase=2;return true;}
        if(Phase==2)
        {
            if(Hero->Inventory->Count(EFrontierItem::Material)<8){if(!Hero->Gathering->IsGathering())return End(false,TEXT("gather-stop"));return true;}
            if(!CFInputQA::Release(Hero,EKeys::M) || Stone->Remaining!=17)return End(false,TEXT("gather-cost"));Gathering=false;Photo(TEXT("Gathered"),Stone->GetActorLocation());
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_SHELTER_GATHER PASS input=1 sourcePaid=8 total=36"));if(!Plan(FVector(-280,-350,40),3))return End(false,TEXT("return-path"));return true;
        }
        if(Phase==4 || Phase==14)
        {
            Face(Box->GetActorLocation()+FVector(0,0,35));
            if(Phase==4){if(!CFInputQA::Press(Hero,EKeys::E) || Hero->Storage->GetCrate()!=Box.Get() || !CFInputQA::Press(Hero,EKeys::Right) || Box->StorageInventory->Count(EFrontierItem::Material)!=22 || !CFInputQA::Press(Hero,EKeys::E))return End(false,TEXT("store-input"));Photo(TEXT("Stocked"),Box->GetActorLocation());}
            if(!CFInputQA::Press(Hero,EKeys::V) || Hero->GetDeliveryCrate()!=Box.Get())return End(false,TEXT("source-select"));
            if(!Plan(Read && Phase==14?FVector(-450,-400,40):Observer(),Read && Phase==14?11:5))return End(false,TEXT("observer-path"));return true;
        }
        if(Phase==6)
        {
            Face(Aim());if(!CFInputQA::Press(Hero,EKeys::B) || !CFInputQA::Press(Hero,EKeys::Eight))return End(false,TEXT("plan-open"));
            const auto Kind=Part<2?EHomeModuleKind::Wall:EHomeModuleKind::Roof;for(int32 I=0;Hero->HomeBuilding->Selected!=Kind && I<10;++I)if(!CFInputQA::Press(Hero,EKeys::V))return End(false,TEXT("plan-select"));
            CastChecked<APlayerController>(Hero->GetController())->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::RightShift,IE_Pressed,1.f,false,FPlatformTime::Cycles64()));Phase=7;At=World->GetTimeSeconds();return true;
        }
        if(Phase==7)
        {
            Face(Aim());Hero->HomeBuilding->UpdatePreview();const auto* Ghost=Hero->HomeBuilding->GetPreview();
            if(!Ghost || Ghost->IsHidden() || !Ghost->GetActorLocation().Equals(Position(),.01) || !Hero->HomeBuilding->GetHint().Contains(TEXT("可以建造"))){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("plan-preview"));return true;}
            if(!CFInputQA::Action(Hero,TEXT("Attack"),EKeys::LeftMouseButton))return End(false,TEXT("plan-input"));
            CastChecked<APlayerController>(Hero->GetController())->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::RightShift,IE_Released,0.f,false,FPlatformTime::Cycles64()));if(!CFInputQA::Press(Hero,EKeys::B))return End(false,TEXT("plan-close"));
            Site.Reset();for(TActorIterator<AHomeModule> It(World);It;++It)if(!It->bPreview && It->bUnderConstruction && It->GetActorLocation().Equals(Position(),.01))Site=*It;
            if(!Site.IsValid() || Site->FloorId!=Floor->PersistentId || Site->PaidMaterials!=0)return End(false,TEXT("site"));Phase=9;At=World->GetTimeSeconds();return true;
        }
        if(Phase==9)
        {
            Face(Site->ConstructionAnchor()+(Part<2?FVector(-160,0,50):FVector(0,0,12)));
            if(AHomeModule::TraceConstruction(Hero)!=Site.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("site-sight"));return true;}
            if(!CFInputQA::Press(Hero,EKeys::E) || Pet->Construction->GetSite()!=Site.Get())return End(false,TEXT("assign-input"));
            if(!Plan(FVector(-450,-400,40),11))return End(false,TEXT("yield-path"));return true;
        }
        if(Phase==8)
        {
            if(!Site.IsValid())return End(false,TEXT("work-site"));if(Site->bUnderConstruction)return true;
            if(Site->PaidMaterials!=(Part<2?6:10) || Pet->Cargo->Count()!=0 || Pet->Construction->GetSite())return End(false,TEXT("completion"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_SHELTER_PART PASS part=%d paid=%d"),Part,Site->PaidMaterials);Photo(Part==0?TEXT("Wall1"):Part==1?TEXT("Wall2"):TEXT("Roof"),Site->GetActorLocation());++Part;
            if(Part<3){if(!Plan(Observer(),5))return End(false,TEXT("next-path"));return true;}
            if(!SavedMid || !CFInputQA::Press(Hero,EKeys::F) || !Plan(FVector(140,-400,60),12))return End(false,TEXT("enter-path"));return true;
        }
        if(Phase==13)
        {
            const bool Inside=FMath::Abs(H.X)<180 && FMath::Abs(H.Y+400)<180 && FMath::Abs(P.X)<180 && FMath::Abs(P.Y+400)<180 && H.Z>50 && P.Z>50;
            if(!Inside || Pet->Activity!=ECreatureActivity::Follow){if(World->GetTimeSeconds()-At>20)return End(false,TEXT("enter-pair"));return true;}
            if(Photo(TEXT("Inside"),P))return true;
            // 新来源不仅核对模块记录，还检查两墙和屋顶实际阻挡射线，拆支撑不能改变库存。
            int32 Walls=0,Roofs=0;for(TActorIterator<AHomeModule> It(World);It;++It)if(!It->bPreview && !It->bStaged){Walls+=It->GetKind()==EHomeModuleKind::Wall;Roofs+=It->GetKind()==EHomeModuleKind::Roof;}
            FCollisionQueryParams Q(SCENE_QUERY_STAT(StarterShelterGeometry),false,Hero);Q.AddIgnoredActor(Pet.Get());
            auto Blocked=[&](FVector Start,FVector Finish,EHomeModuleKind Kind){FHitResult Hit;if(!World->LineTraceSingleByChannel(Hit,Start,Finish,ECC_Visibility,Q))return false;const auto* Module=Cast<AHomeModule>(Hit.GetActor());return Module && !Module->bUnderConstruction && Module->GetKind()==Kind;};
            if(Walls!=2 || Roofs!=1 || !Blocked(FVector(0,-400,120),FVector(0,-100,120),EHomeModuleKind::Wall) || !Blocked(FVector(0,-400,120),FVector(0,-700,120),EHomeModuleKind::Wall) || !Blocked(FVector(0,-400,180),FVector(0,-400,400),EHomeModuleKind::Roof))return End(false,TEXT("geometry"));
            const auto Before=Hero->Inventory->GetStacks();FString Reason;auto Layout=HomeLayout::Collect(World);if(Layout.Num()!=5 || !HomeLayout::Validate(Layout,Reason) || Hero->HomeBuilding->Dismantle(Floor.Get()) || !Hero->Inventory->GetStacks().OrderIndependentCompareEqual(Before))return End(false,TEXT("support"));
            const auto HBefore=Hero->GetActorLocation(),PBefore=Pet->GetActorLocation();if(!Hero->SaveProgress(TEXT("StarterShelterDone")) || !Hero->LoadProgress(TEXT("StarterShelterDone")))return End(false,TEXT("save"));FindActors();
            if(!Budget() || !Hero->GetActorLocation().Equals(HBefore,.01) || !Pet->GetActorLocation().Equals(PBefore,.01) || Box->StorageInventory->Count(EFrontierItem::Material)!=0 || HomeLayout::Collect(World).Num()!=5)return End(false,TEXT("restore"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_SHELTER_LIFE PASS version=38 modules=5 paid=36 stored=0 entered=1 identity=1 collision=3 supportReject=1"));return End(true,TEXT("complete"));
        }
        return true;
    }
};
}
void RunStarterShelterScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto S=MakeShared<FStarterShelter>();S->World=World;S->Hero=Hero;S->Read=Read;S->Deadline=FPlatformTime::Seconds()+150;
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("StarterShelterInput"),0));
    if(!Source || Source->FormatVersion!=38 || Source->Facilities.Num()!=1 || Source->HomeModules.Num()!=(Read?3:2) || !Hero->LoadProgress(TEXT("StarterShelterInput"))){S->End(false,TEXT("source"));return;}
    S->Garden=Source->Facilities[0];for(const auto& R:Source->HomeModules){if(R.Definition.Get() && R.Definition.Get()->Kind==EHomeModuleKind::Floor)S->OriginalFloor=R;if(R.Definition.Get() && R.Definition.Get()->Kind==EHomeModuleKind::Storage)S->OriginalBox=R;}
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController()){S->PetId=It->PersistentId;S->Nickname=It->Nickname;}S->FindActors();
    if(!S->Budget() || !Hero->GetActorTransform().Equals(Source->PlayerTransform,.01)){S->End(false,TEXT("source-budget"));return;}
    if(Read){for(TActorIterator<AHomeModule> It(World);It;++It)if(It->bUnderConstruction)S->Site=*It;if(!S->Site.IsValid() || S->Pet->Cargo->Count()!=3 || S->Pet->Construction->GetSite()!=S->Site.Get()){S->End(false,TEXT("source-work"));return;}S->SavedMid=true;}
    S->LastH=Hero->GetActorLocation();S->LastP=S->Pet->GetActorLocation();S->VelocityH=Hero->GetVelocity();S->VelocityP=S->Pet->GetVelocity();S->LastTime=World->GetTimeSeconds();S->Input=NewObject<UHomeMovementProbe>(Hero);S->Input->RegisterComponent();
    S->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFStarterShelterPreview"));if(S->Preview){S->Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("StarterShelterPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*S->Root,true);UE_LOG(LogTemp,Display,TEXT("CF_STARTER_SHELTER_PREVIEW_ROOT %s"),*S->Root);}
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){return S->Tick();}));
}

namespace
{
// 继续真实棚屋存档，复用导航探针及原输入，不设置角色位置、资源数量或施工进度。
struct FStarterRoom : FStarterJourney
{
    TWeakObjectPtr<AHomeModule> Floor,Box,Site,Door;TWeakObjectPtr<AResourceNode> Supply;
    TArray<FHomeModuleRecord> Original;FFacilitySaveRecord Garden;FString Nickname;
    int32 Part=0;bool Gathering=false,Mid=false;
    FVector Position()const{return FVector(Part==0?200:-200,-400,-30);}
    FVector Observer()const{return FVector(Part==0?440:-520,-400,40);}
    void Find()
    {
        Pet.Reset();Floor.Reset();Box.Reset();Door.Reset();Supply.Reset();
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->PersistentId==PetId)Pet=*It;
        for(TActorIterator<AHomeModule> It(World);It;++It)if(!It->bPreview && !It->bStaged)
        {if(It->GetKind()==EHomeModuleKind::Floor)Floor=*It;if(It->GetKind()==EHomeModuleKind::Storage)Box=*It;if(It->GetKind()==EHomeModuleKind::Door)Door=*It;}
        for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,4))Supply=*It;
    }
    bool End(bool Pass,const TCHAR* Reason)
    {
        if(Input.IsValid())Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::LeftShift);CFInputQA::Release(Hero,EKeys::M);
        if(auto* PC=Cast<APlayerController>(Hero->GetController()))PC->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::RightShift,IE_Released,0.f,false,FPlatformTime::Cycles64()));
        if(!Pass)Hero->SaveProgress(TEXT("StarterRoomBlocked"));
        const auto Here=Hero->GetActorLocation();const auto Next=Route.IsValidIndex(Point)?Route[Point]:Here;FHitResult Obstacle;FCollisionQueryParams Q(SCENE_QUERY_STAT(RoomStopped),false,Hero);
        World->LineTraceSingleByChannel(Obstacle,Here,Next+FVector(0,0,50),ECC_Visibility,Q);
        UE_LOG(LogTemp,Display,TEXT("CF_STARTER_ROOM %s reason=%s phase=%d part=%d heroTravel=%.1f petTravel=%.1f hero=%s pet=%s next=%s obstacle=%s feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Reason,Phase,Part,HeroTravel,PetTravel,*Here.ToString(),Pet.IsValid()?*Pet->GetActorLocation().ToString():TEXT("none"),*Next.ToString(),*GetNameSafe(Obstacle.GetActor()),*Hero->Feedback);FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    bool Budget()const
    {
        if(!Pet.IsValid() || !Floor.IsValid() || !Box.IsValid() || !Supply.IsValid())return false;
        const int32 Taken=24-Supply->Remaining;int32 Total=Hero->Inventory->Count(EFrontierItem::Material)+Pet->Cargo->Count(),Farms=0;
        const auto Layout=HomeLayout::Collect(World);
        for(const auto& R:Layout)Total+=R.PaidMaterials+R.StoredItems.FindRef(EFrontierItem::Material);
        for(const auto& R:Original){const auto* Current=Layout.FindByPredicate([&](const FHomeModuleRecord& V){return V.Id==R.Id;});if(!Current || !Current->Transform.Equals(R.Transform,.01) || Current->Definition!=R.Definition || Current->PaidMaterials!=R.PaidMaterials)return false;}
        for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt){++Farms;if(It->PersistentId!=Garden.Id || !It->GetActorTransform().Equals(Garden.Transform,.01) || It->StoredFood!=Garden.StoredFood || It->Crop->Seeds!=Garden.Seeds || It->Crop->bPlanted!=Garden.bPlanted || !FMath::IsNearlyEqual(It->Crop->GrowthWork,Garden.GrowthWork,.01))return false;}
        return Taken>=0 && Taken<=22 && Total==36+Taken && Farms==1 && Hero->Health==100 && Pet->Health>0 && Pet->Level==2 && Pet->Experience==37 && Pet->BondPoints==3 && Pet->Nickname==Nickname && Hero->GetObjectives().Claimed==5 && Hero->GetObjectives().bAlphaResolved && Hero->Inventory->Count(EFrontierItem::Medicine)==0 && Hero->Inventory->Count(EFrontierItem::CaptureDevice)==10 && Hero->Inventory->Count(EFrontierItem::Food)==2 && Hero->Inventory->Count(EFrontierItem::Seeds)==1;
    }
    bool Tick()
    {
        if(!Budget() || FPlatformTime::Seconds()>Deadline)return End(false,TEXT("budget-time"));
        if(World->GetTimeSeconds()>=NextDiagnostic)
        {NextDiagnostic=World->GetTimeSeconds()+5;UE_LOG(LogTemp,Display,TEXT("CF_STARTER_ROOM_STATE phase=%d part=%d stock=%d cargo=%d supplied=%d work=%.2f stamina=%.1f pet=%s feedback=%s"),Phase,Part,Box->StorageInventory->Count(EFrontierItem::Material),Pet->Cargo->Count(),Site.IsValid()?Site->StorageInventory->Count(EFrontierItem::Material):0,Site.IsValid()?Site->ConstructionWork:0,Pet->Stamina,*Pet->GetActorLocation().ToString(),*Hero->Feedback);}
        const auto H=Hero->GetActorLocation(),P=Pet->GetActorLocation();const double Delta=World->GetTimeSeconds()-LastTime;
        if(!Motion(Hero,LastH,VelocityH,Delta) || !Motion(Pet.Get(),LastP,VelocityP,Delta))return End(false,TEXT("motion"));
        HeroTravel+=FVector::Dist(H,LastH);PetTravel+=FVector::Dist(P,LastP);LastH=H;LastP=P;VelocityH=Hero->GetVelocity();VelocityP=Pet->GetVelocity();LastTime=World->GetTimeSeconds();
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Health>0 && (!It->IsActorTickEnabled() || !It->GetActorEnableCollision() || It->GetCharacterMovement()->MovementMode==MOVE_None))return End(false,TEXT("ecology"));
        for(TActorIterator<AWildEncounter> It(World);It;++It)if(!It->IsActorTickEnabled())return End(false,TEXT("region"));
        if(!Mid && Site.IsValid() && Part==0 && Pet->Cargo->Count()==3)
        {if(!Hero->SaveProgress(TEXT("StarterRoomMid")))return End(false,TEXT("mid"));Mid=true;UE_LOG(LogTemp,Display,TEXT("CF_STARTER_ROOM_MID PASS carried=3 total=58"));}
        if(FPlatformTime::Seconds()<ShotUntil)return true;
        if(Phase==-1)
        {if(World->GetTimeSeconds()-At<2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;if(!Plan(Read?FVector(-280,-350,40):FVector(-1220,900,40),Read?3:0))return End(false,TEXT("initial-path"));return true;}
        if(Phase==0 || Phase==3 || Phase==5 || Phase==10 || Phase==12 || Phase==15 || Phase==17)
        {const int32 R=Move();if(R<0)return End(false,TEXT("walk"));if(!R)return true;Phase=Phase==0?1:Phase==3?4:Phase==5?6:Phase==10?9:Phase==12?13:Phase==15?16:18;At=World->GetTimeSeconds();return true;}
        if(Phase==1)
        {Face(Supply->GetActorLocation());if(Hero->TraceResource()!=Supply.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("gather-sight"));return true;}if(!CFInputQA::Press(Hero,EKeys::M))return End(false,TEXT("gather-input"));Gathering=true;Phase=2;return true;}
        if(Phase==2)
        {if(Hero->Inventory->Count(EFrontierItem::Material)<22){if(!Hero->Gathering->IsGathering())return End(false,TEXT("gather-stopped"));return true;}if(!CFInputQA::Release(Hero,EKeys::M) || Supply->Remaining!=2)return End(false,TEXT("gather-cost"));Gathering=false;Photo(TEXT("Gathered"),Supply->GetActorLocation());UE_LOG(LogTemp,Display,TEXT("CF_STARTER_ROOM_GATHER PASS harvested=22 remaining=2 total=58"));if(!Plan(FVector(-280,-350,40),3))return End(false,TEXT("return-path"));return true;}
        if(Phase==4)
        {
            Face(Box->GetActorLocation()+FVector(0,0,35));
            if(!Read){if(!CFInputQA::Press(Hero,EKeys::E) || Hero->Storage->GetCrate()!=Box.Get() || !CFInputQA::Press(Hero,EKeys::Right) || Box->StorageInventory->Count(EFrontierItem::Material)!=22 || !CFInputQA::Press(Hero,EKeys::E))return End(false,TEXT("stock"));Photo(TEXT("Stocked"),Box->GetActorLocation());}
            if(!CFInputQA::Press(Hero,EKeys::V) || Hero->GetDeliveryCrate()!=Box.Get() || !Plan(Read?FVector(520,-400,40):Observer(),Read?10:5))return End(false,TEXT("source-observer"));return true;
        }
        if(Phase==6)
        {
            Face(FVector(Part==0?180:-180,-400,-30));if(!CFInputQA::Press(Hero,EKeys::B) || !CFInputQA::Press(Hero,EKeys::Eight))return End(false,TEXT("build-open"));
            const auto Kind=Part==0?EHomeModuleKind::Window:EHomeModuleKind::Door;
            for(int32 I=0;Hero->HomeBuilding->Selected!=Kind && I<10;++I)if(!CFInputQA::Press(Hero,EKeys::V))return End(false,TEXT("select"));
            const auto* RotationPreview=Hero->HomeBuilding->GetPreview();if(!RotationPreview)return End(false,TEXT("rotation-preview"));
            if(!FMath::IsNearlyEqual(FMath::Abs(RotationPreview->GetActorRotation().Yaw),90.,.01) && !CFInputQA::Press(Hero,EKeys::Nine))return End(false,TEXT("rotate"));
            CastChecked<APlayerController>(Hero->GetController())->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::RightShift,IE_Pressed,1.f,false,FPlatformTime::Cycles64()));Phase=7;At=World->GetTimeSeconds();return true;
        }
        if(Phase==7)
        {
            Face(FVector(Part==0?180:-180,-400,-30));Hero->HomeBuilding->UpdatePreview();const auto* Ghost=Hero->HomeBuilding->GetPreview();
            if(!Ghost || Ghost->IsHidden() || !Ghost->GetActorLocation().Equals(Position(),.01) || !Hero->HomeBuilding->GetHint().Contains(TEXT("可以建造"))){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("preview"));return true;}
            if(!CFInputQA::Action(Hero,TEXT("Attack"),EKeys::LeftMouseButton))return End(false,TEXT("confirm"));
            CastChecked<APlayerController>(Hero->GetController())->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::RightShift,IE_Released,0.f,false,FPlatformTime::Cycles64()));CFInputQA::Press(Hero,EKeys::B);
            Site.Reset();for(TActorIterator<AHomeModule> It(World);It;++It)if(!It->bPreview && It->bUnderConstruction && It->GetActorLocation().Equals(Position(),.01))Site=*It;
            if(!Site.IsValid() || Site->PaidMaterials!=0 || Site->FloorId!=Floor->PersistentId)return End(false,TEXT("site"));Phase=8;At=World->GetTimeSeconds();return true;
        }
        if(Phase==8)
        {Face(Site->GetActorTransform().TransformPosition(FVector(-160,0,50)));if(AHomeModule::TraceConstruction(Hero)!=Site.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("site-sight"));return true;}if(!CFInputQA::Press(Hero,EKeys::E) || Pet->Construction->GetSite()!=Site.Get() || !Plan(FVector(Part==0?520:-520,-400,40),10))return End(false,TEXT("assign-yield"));return true;}
        if(Phase==9)
        {
            if(!Site.IsValid())return End(false,TEXT("work-site"));if(Site->bUnderConstruction)return true;
            if(Site->PaidMaterials!=(Part==0?10:12) || Pet->Cargo->Count()!=0 || Pet->Construction->GetSite())return End(false,TEXT("completion"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_ROOM_PART PASS part=%d paid=%d"),Part,Site->PaidMaterials);Photo(Part==0?TEXT("Window"):TEXT("DoorClosed"),Site->GetActorLocation()+FVector(0,0,100));++Part;
            if(Part==1){if(!Plan(Observer(),5))return End(false,TEXT("door-path"));return true;}
            Door=Site;if(!Mid || !CFInputQA::Press(Hero,EKeys::F) || !Plan(FVector(-470,-400,40),12))return End(false,TEXT("approach-door"));return true;
        }
        if(Phase==13)
        {
            Face(FVector(-200,-555,100));if(!CFInputQA::Press(Hero,EKeys::E) || Door->DoorState!=2)return End(false,TEXT("door-open"));Photo(TEXT("DoorOpen"),Door->GetActorLocation()+FVector(-110,-110,120));Phase=14;At=World->GetTimeSeconds();return true;
        }
        if(Phase==14)
        {if(UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World) || World->GetTimeSeconds()-At<1)return true;if(!Plan(FVector(120,-400,60),15))return End(false,TEXT("enter-path"));return true;}
        if(Phase==16)
        {
            if(P.X< -140 || P.X>160 || FMath::Abs(P.Y+400)>160 || P.Z<50 || Pet->Activity!=ECreatureActivity::Follow){if(World->GetTimeSeconds()-At>20)return End(false,TEXT("pair-enter"));return true;}
            // 先让伙伴完全离开门扇活动区，再由玩家实际走回可操作门框的位置。
            if(!Plan(FVector(50,-400,60),17))return End(false,TEXT("inside-door-path"));return true;
        }
        if(Phase==18)
        {
            Face(FVector(-200,-555,100));if(Door->DoorState==2 && (!CFInputQA::Press(Hero,EKeys::E) || Door->DoorState!=1))return End(false,TEXT("close-inside"));
            if(Photo(TEXT("Inside"),P))return true;
            FString Reason;const auto Layout=HomeLayout::Collect(World);if(Layout.Num()!=7 || !HomeLayout::Validate(Layout,Reason) || Hero->HomeBuilding->Dismantle(Floor.Get()))return End(false,TEXT("layout-support"));
            FCollisionQueryParams Q(SCENE_QUERY_STAT(StarterRoomGeometry),false,Hero);Q.AddIgnoredActor(Pet.Get());FHitResult Hit;
            if(!World->LineTraceSingleByChannel(Hit,FVector(0,-400,140),FVector(-400,-400,140),ECC_Visibility,Q) || Hit.GetActor()!=Door.Get())return End(false,TEXT("closed-collision"));
            const auto HB=H,PB=P;if(!Hero->SaveProgress(TEXT("StarterRoomDone")) || !Hero->LoadProgress(TEXT("StarterRoomDone")))return End(false,TEXT("save"));Find();
            if(!Budget() || !Door.IsValid() || Door->DoorState!=1 || !Hero->GetActorLocation().Equals(HB,.01) || !Pet->GetActorLocation().Equals(PB,.01))return End(false,TEXT("restore"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_ROOM_LIFE PASS modules=7 paid=58 entered=1 closed=1 identity=1"));return End(true,TEXT("complete"));
        }
        return true;
    }
};
}
void RunStarterRoomScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto S=MakeShared<FStarterRoom>();S->World=World;S->Hero=Hero;S->Read=Read;S->At=World->GetTimeSeconds();S->Deadline=FPlatformTime::Seconds()+210;
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("StarterRoomInput"),0));
    if(!Source || Source->FormatVersion!=38 || Source->Facilities.Num()!=1 || Source->HomeModules.Num()!=(Read?6:5) || !Hero->LoadProgress(TEXT("StarterRoomInput"))){S->End(false,TEXT("source"));return;}
    S->Garden=Source->Facilities[0];for(const auto& R:Source->HomeModules)if(!R.bUnderConstruction)S->Original.Add(R);
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController()){S->PetId=It->PersistentId;S->Nickname=It->Nickname;}S->Find();
    if(!S->Budget() || !Hero->GetActorTransform().Equals(Source->PlayerTransform,.01)){S->End(false,TEXT("source-budget"));return;}
    if(Read){for(TActorIterator<AHomeModule> It(World);It;++It)if(It->bUnderConstruction)S->Site=*It;if(!S->Site.IsValid() || S->Site->GetKind()!=EHomeModuleKind::Window || S->Pet->Cargo->Count()!=3 || S->Pet->Construction->GetSite()!=S->Site.Get()){S->End(false,TEXT("source-work"));return;}S->Mid=true;}
    S->LastH=Hero->GetActorLocation();S->LastP=S->Pet->GetActorLocation();S->VelocityH=Hero->GetVelocity();S->VelocityP=S->Pet->GetVelocity();S->LastTime=World->GetTimeSeconds();S->Input=NewObject<UHomeMovementProbe>(Hero);S->Input->RegisterComponent();
    S->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFStarterRoomPreview"));if(S->Preview){S->Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("StarterRoomPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*S->Root,true);UE_LOG(LogTemp,Display,TEXT("CF_STARTER_ROOM_PREVIEW_ROOT %s"),*S->Root);}
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){return S->Tick();}));
}

namespace
{
struct FStarterHomeLife : FStarterRoom
{
    TWeakObjectPtr<AWorkStation> RestSite;bool Fed=false,RestMid=false;float LastStamina=0;
    bool End(bool Pass,const TCHAR* Reason)
    {
        if(Input.IsValid())Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::LeftShift);
        const FString Feedback=Hero->Feedback;if(!Pass)Hero->SaveProgress(TEXT("StarterHomeLifeBlocked"));
        UE_LOG(LogTemp,Display,TEXT("CF_STARTER_HOME_LIFE %s reason=%s phase=%d heroTravel=%.1f petTravel=%.1f stamina=%.2f nutrition=%.2f food=%d feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Reason,Phase,HeroTravel,PetTravel,Pet.IsValid()?Pet->Stamina:-1,Pet.IsValid()?Pet->Nutrition:-1,Hero->Inventory->Count(EFrontierItem::Food),*Feedback);FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    bool Budget()const
    {
        if(!Pet.IsValid() || !Door.IsValid() || !Supply.IsValid() || Supply->Remaining!=2 || Hero->Health!=100 || Pet->Health<=0 || Pet->Level!=2 || Pet->Experience!=37 || Pet->BondPoints!=(Fed?6:3) || Pet->Nickname!=Nickname || Hero->Inventory->Count(EFrontierItem::Food)!=(Fed?1:2) || Hero->Inventory->Count(EFrontierItem::Material)!=0 || Pet->Cargo->Count()!=0 || !Hero->GetObjectives().bAlphaResolved || Hero->GetObjectives().Claimed!=5)return false;
        const auto Layout=HomeLayout::Collect(World);if(Layout.Num()!=7)return false;int32 Paid=0;
        for(const auto& R:Original){const auto* Current=Layout.FindByPredicate([&](const FHomeModuleRecord& V){return V.Id==R.Id;});if(!Current || !Current->Transform.Equals(R.Transform,.01) || Current->Definition!=R.Definition || Current->PaidMaterials!=R.PaidMaterials || !Current->StoredItems.OrderIndependentCompareEqual(R.StoredItems))return false;Paid+=Current->PaidMaterials;}
        int32 Farms=0;for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt){++Farms;if(It->PersistentId!=Garden.Id || !It->GetActorTransform().Equals(Garden.Transform,.01) || It->StoredFood!=Garden.StoredFood || It->Crop->Seeds!=Garden.Seeds || It->Crop->bPlanted!=Garden.bPlanted || !FMath::IsNearlyEqual(It->Crop->GrowthWork,Garden.GrowthWork,.01))return false;}
        return Paid==58 && Farms==1 && Hero->Inventory->Count(EFrontierItem::Medicine)==0 && Hero->Inventory->Count(EFrontierItem::CaptureDevice)==10 && Hero->Inventory->Count(EFrontierItem::Seeds)==1;
    }
    bool VisibleRest()const
    {
        if(!RestSite.IsValid() || Pet->GetDistanceTo(RestSite.Get())>220)return false;
        FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(HomeLifeRestSight),false,Pet.Get());
        return !World->LineTraceSingleByChannel(Hit,Pet->GetActorLocation(),RestSite->GetActorLocation()+FVector(0,0,30),ECC_Visibility,Q) || Hit.GetActor()==RestSite.Get();
    }
    bool Tick()
    {
        if(!Budget() || FPlatformTime::Seconds()>Deadline)return End(false,TEXT("budget-time"));
        const auto H=Hero->GetActorLocation(),P=Pet->GetActorLocation();const double Delta=World->GetTimeSeconds()-LastTime;
        if(!Motion(Hero,LastH,VelocityH,Delta) || !Motion(Pet.Get(),LastP,VelocityP,Delta))return End(false,TEXT("motion"));
        HeroTravel+=FVector::Dist(H,LastH);PetTravel+=FVector::Dist(P,LastP);LastH=H;LastP=P;VelocityH=Hero->GetVelocity();VelocityP=Pet->GetVelocity();LastTime=World->GetTimeSeconds();
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Health>0 && (!It->IsActorTickEnabled() || !It->GetActorEnableCollision() || It->GetCharacterMovement()->MovementMode==MOVE_None))return End(false,TEXT("ecology"));
        if(RestSite.IsValid() && Pet->Activity==ECreatureActivity::Rest && Pet->Stamina>LastStamina+.05f && !VisibleRest())return End(false,TEXT("rest-through-obstacle"));LastStamina=Pet->Stamina;
        if(!RestMid && RestSite.IsValid() && Pet->Activity==ECreatureActivity::Rest && Pet->Stamina>=45 && Pet->Stamina<90 && VisibleRest())
        {if(!Hero->SaveProgress(TEXT("StarterHomeLifeMid")))return End(false,TEXT("mid-save"));RestMid=true;Photo(TEXT("Resting"),P);UE_LOG(LogTemp,Display,TEXT("CF_STARTER_HOME_LIFE_MID PASS resting=1 visible=1 stamina=%.2f"),Pet->Stamina);}
        if(FPlatformTime::Seconds()<ShotUntil)return true;
        if(Phase==-1){if(World->GetTimeSeconds()-At<2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;Phase=Read?3:0;At=World->GetTimeSeconds();return true;}
        if(Phase==0)
        {
            if(Photo(TEXT("Tired"),P))return true;Face(P);
            if(Hero->TraceCreature()!=Pet.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("feed-sight"));return true;}
            if(!Hero->GetObjectiveHint().Contains(TEXT("J喂食")))return End(false,TEXT("feed-hint"));
            const float SP=Pet->Stamina,Food=Pet->Nutrition;
            if(!CFInputQA::Press(Hero,EKeys::J) || Hero->Inventory->Count(EFrontierItem::Food)!=1 || Pet->BondPoints!=6 || !FMath::IsNearlyEqual(Pet->Stamina,FMath::Min(100.f,SP+15),.01f) || !FMath::IsNearlyEqual(Pet->Nutrition,FMath::Min(100.f,Food+35),.01f))return End(false,TEXT("feed"));
            Fed=true;LastStamina=Pet->Stamina;if(!Hero->GetObjectiveHint().Contains(TEXT("T休息")))return End(false,TEXT("rest-hint"));Photo(TEXT("Fed"),P);UE_LOG(LogTemp,Display,TEXT("CF_STARTER_HOME_LIFE_FEED PASS foodPaid=1 nutritionAdded=35 staminaAdded=15 bond=6"));Phase=1;return true;
        }
        if(Phase==1)
        {
            Face(FVector(-200,-555,100));if(!CFInputQA::Press(Hero,EKeys::E) || Door->DoorState!=2 || !CFInputQA::Press(Hero,EKeys::T) || Pet->Activity!=ECreatureActivity::Rest || !IsValid(Pet->WorkSite))return End(false,TEXT("open-rest"));
            RestSite=Pet->WorkSite;UE_LOG(LogTemp,Display,TEXT("CF_STARTER_HOME_LIFE_REST_TARGET position=%s"),*RestSite->GetActorLocation().ToString());Phase=10;At=World->GetTimeSeconds();return true;
        }
        if(Phase==10)
        {if(World->GetTimeSeconds()-At<2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;if(!Plan(RestSite->GetActorLocation()+FVector(-240,-180,40),2))return End(false,TEXT("rest-observer-path"));return true;}
        if(Phase==2 || Phase==4 || Phase==6 || Phase==8)
        {const int32 R=Move();if(R<0)return End(false,TEXT("walk"));if(!R)return true;Phase=Phase==2?3:Phase==4?5:Phase==6?7:9;At=World->GetTimeSeconds();return true;}
        if(Phase==3)
        {
            if(Pet->Activity!=ECreatureActivity::Rest || Pet->WorkSite!=RestSite.Get())return End(false,TEXT("rest-task"));if(Pet->Stamina<99 || !VisibleRest())return true;
            if(!RestMid)return End(false,TEXT("rest-no-mid"));if(Photo(TEXT("Recovered"),P))return true;
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_HOME_LIFE_REST PASS stamina=%.2f visible=1"),Pet->Stamina);
            if(!CFInputQA::Press(Hero,EKeys::F) || !Plan(FVector(-470,-400,40),4))return End(false,TEXT("return-door"));return true;
        }
        if(Phase==5){if(Door->DoorState!=2 || !Plan(FVector(120,-400,60),6))return End(false,TEXT("enter-path"));return true;}
        if(Phase==7)
        {if(P.X< -140 || P.X>160 || FMath::Abs(P.Y+400)>160 || P.Z<50 || Pet->Activity!=ECreatureActivity::Follow){if(World->GetTimeSeconds()-At>20)return End(false,TEXT("pair-enter"));return true;}if(!Plan(FVector(50,-400,60),8))return End(false,TEXT("close-path"));return true;}
        if(Phase==9)
        {
            Face(FVector(-200,-555,100));if(Door->DoorState==2 && (!CFInputQA::Press(Hero,EKeys::E) || Door->DoorState!=1))return End(false,TEXT("close"));if(Photo(TEXT("Home"),P))return true;
            if(!Hero->GetObjectiveHint().Contains(TEXT("农圃")) || Pet->Stamina<99)return End(false,TEXT("provisions-hint"));
            const float SP=Pet->Stamina,N=Pet->Nutrition;const auto HB=H,PB=P;
            if(!Hero->SaveProgress(TEXT("StarterHomeLifeDone")) || !Hero->LoadProgress(TEXT("StarterHomeLifeDone")))return End(false,TEXT("save"));Find();
            if(!Budget() || Door->DoorState!=1 || !Hero->GetActorLocation().Equals(HB,.01) || !Pet->GetActorLocation().Equals(PB,.01) || !FMath::IsNearlyEqual(Pet->Stamina,SP,.01) || !FMath::IsNearlyEqual(Pet->Nutrition,N,.01))return End(false,TEXT("restore"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_HOME_LIFE_LIFE PASS modules=7 paid=58 food=1 bond=6 rested=1 returned=1 closed=1 identity=1"));return End(true,TEXT("complete"));
        }
        return true;
    }
};
}
void RunStarterHomeLifeScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto S=MakeShared<FStarterHomeLife>();S->World=World;S->Hero=Hero;S->Read=Read;S->Fed=Read;S->RestMid=Read;S->At=World->GetTimeSeconds();S->Deadline=FPlatformTime::Seconds()+150;
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("StarterHomeLifeInput"),0));
    if(!Source || Source->FormatVersion!=38 || Source->Facilities.Num()!=1 || Source->HomeModules.Num()!=7 || !Hero->LoadProgress(TEXT("StarterHomeLifeInput"))){S->End(false,TEXT("source"));return;}
    S->Original=Source->HomeModules;S->Garden=Source->Facilities[0];for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController()){S->PetId=It->PersistentId;S->Nickname=It->Nickname;}S->Find();
    if(!S->Budget() || !Hero->GetActorTransform().Equals(Source->PlayerTransform,.01) || (!Read && (S->Pet->Stamina>=30 || S->Pet->Nutrition>80 || S->Door->DoorState!=1))){S->End(false,TEXT("source-budget"));return;}
    if(Read){S->RestSite=S->Pet->WorkSite;if(!S->RestSite.IsValid() || S->Pet->Activity!=ECreatureActivity::Rest){S->End(false,TEXT("source-rest"));return;}}
    S->LastH=Hero->GetActorLocation();S->LastP=S->Pet->GetActorLocation();S->VelocityH=Hero->GetVelocity();S->VelocityP=S->Pet->GetVelocity();S->LastTime=World->GetTimeSeconds();S->LastStamina=S->Pet->Stamina;S->Input=NewObject<UHomeMovementProbe>(Hero);S->Input->RegisterComponent();
    S->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFStarterHomeLifePreview"));if(S->Preview){S->Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("StarterHomeLifePreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*S->Root,true);UE_LOG(LogTemp,Display,TEXT("CF_STARTER_HOME_LIFE_PREVIEW_ROOT %s"),*S->Root);}
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){return S->Tick();}));
}

namespace
{
struct FStarterHomeFarm : FStarterRoom
{
    TWeakObjectPtr<AWorkStation> Farm;TWeakObjectPtr<AResourceNode> Fiber;
    bool Deposited=false,Withdrew=false,FarmMid=false,SeedSupply=false,Gathered=false,Crafted=false;
    int32 FiberBefore=0;
    FString Slot(const TCHAR* Suffix)const{return FString(SeedSupply?TEXT("StarterHomeSeed"):TEXT("StarterHomeFarm"))+Suffix;}
    int32 BaseXP()const{return SeedSupply?38:37;}
    int32 BaseFood()const{return SeedSupply && !Crafted?2:1;}
    int32 BagSeeds()const{return SeedSupply?(Crafted?(Deposited?2:3):0):(Deposited?0:1);}
    void BindFarm(){Farm.Reset();for(TActorIterator<AWorkStation> It(World);It;++It)if(It->PersistentId==Garden.Id)Farm=*It;if(SeedSupply){Fiber.Reset();for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,8))Fiber=*It;}}
    bool End(bool Pass,const TCHAR* Reason)
    {
        if(Input.IsValid())Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::LeftShift);CFInputQA::Release(Hero,EKeys::M);
        const auto Feedback=Hero->Feedback;if(!Pass)Hero->SaveProgress(Slot(TEXT("Blocked")));
        UE_LOG(LogTemp,Display,TEXT("CF_STARTER_HOME_FARM %s reason=%s phase=%d heroTravel=%.1f petTravel=%.1f farm=%s food=%d stock=%d seeds=%d growth=%.2f feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Reason,Phase,HeroTravel,PetTravel,Farm.IsValid()?*Farm->GetActorLocation().ToString():TEXT("none"),Hero->Inventory->Count(EFrontierItem::Food),Farm.IsValid()?Farm->StoredFood:-1,Farm.IsValid()?Farm->Crop->Seeds:-1,Farm.IsValid()?Farm->Crop->GrowthWork:-1,*Feedback);FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    bool Budget()const
    {
        if(!Pet.IsValid() || !Farm.IsValid() || !Door.IsValid() || !Supply.IsValid() || Supply->Remaining!=2)return false;
        const int32 Harvests=Pet->Experience-BaseXP();
        if(SeedSupply && (!Fiber.IsValid() || FiberBefore<1 || Fiber->Remaining<FiberBefore-1 || Fiber->Remaining>FiberBefore || Hero->Inventory->Count(EFrontierItem::Fiber)+(Crafted?1:0)!=FiberBefore-Fiber->Remaining))return false;
        if(Harvests<0 || Harvests>1 || Hero->Health!=100 || Pet->Health<=0 || Pet->Level!=2 || Pet->BondPoints!=6 || Pet->Nickname!=Nickname || Hero->Inventory->Count(EFrontierItem::Material)!=0 || Pet->Cargo->Count()!=0 || Hero->Inventory->Count(EFrontierItem::Seeds)!=BagSeeds() || Hero->Inventory->Count(EFrontierItem::Food)!=(BaseFood()+(Withdrew?1:0)) || Hero->Inventory->Count(EFrontierItem::Food)+Farm->StoredFood!=BaseFood()+Harvests || Hero->Inventory->Count(EFrontierItem::Seeds)+Farm->Crop->Seeds+(Farm->Crop->bPlanted?1:0)+Harvests!=(SeedSupply?(Crafted?3:0):1))return false;
        const auto Layout=HomeLayout::Collect(World);if(Layout.Num()!=7)return false;int32 Paid=0;
        for(const auto& R:Original){const auto* Current=Layout.FindByPredicate([&](const FHomeModuleRecord& V){return V.Id==R.Id;});if(!Current || !Current->Transform.Equals(R.Transform,.01) || Current->Definition!=R.Definition || Current->PaidMaterials!=R.PaidMaterials || !Current->StoredItems.OrderIndependentCompareEqual(R.StoredItems))return false;Paid+=Current->PaidMaterials;}
        int32 Farms=0;for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt)++Farms;
        return Paid==58 && Farms==1 && Farm->GetActorTransform().Equals(Garden.Transform,.01) && Farm->RequiredSpecialty==EWorkSpecialty::Farming && Hero->GetObjectives().Claimed==5 && Hero->GetObjectives().bAlphaResolved && Hero->Inventory->Count(EFrontierItem::Medicine)==0 && Hero->Inventory->Count(EFrontierItem::CaptureDevice)==10;
    }
    bool Tick()
    {
        if(!Budget() || FPlatformTime::Seconds()>Deadline)return End(false,TEXT("budget-time"));
        const auto H=Hero->GetActorLocation(),P=Pet->GetActorLocation();const double Delta=World->GetTimeSeconds()-LastTime;
        if(!Motion(Hero,LastH,VelocityH,Delta) || !Motion(Pet.Get(),LastP,VelocityP,Delta))return End(false,TEXT("motion"));
        HeroTravel+=FVector::Dist(H,LastH);PetTravel+=FVector::Dist(P,LastP);LastH=H;LastP=P;VelocityH=Hero->GetVelocity();VelocityP=Pet->GetVelocity();LastTime=World->GetTimeSeconds();
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Health>0 && (!It->IsActorTickEnabled() || !It->GetActorEnableCollision() || It->GetCharacterMovement()->MovementMode==MOVE_None))return End(false,TEXT("ecology"));
        if(!FarmMid && Farm->Crop->bPlanted && Farm->Crop->GrowthWork>=1 && Farm->Crop->GrowthWork<5 && Pet->WorkSite==Farm.Get())
        {if(!SeedSupply && !Hero->SaveProgress(Slot(TEXT("Mid"))))return End(false,TEXT("mid-save"));FarmMid=true;Photo(TEXT("Growing"),Farm->GetActorLocation());UE_LOG(LogTemp,Display,TEXT("CF_STARTER_HOME_FARM_MID PASS planted=1 food=1 xp=%d growth=%.2f"),BaseXP(),Farm->Crop->GrowthWork);}
        if(FPlatformTime::Seconds()<ShotUntil)return true;
        if(Phase==-1){if(World->GetTimeSeconds()-At<2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;Phase=Read?(SeedSupply?24:3):0;return true;}
        if(Phase==0)
        {if(Photo(TEXT("Ready"),P))return true;if(!Hero->GetObjectiveHint().Contains(SeedSupply?TEXT("准备种子"):TEXT("农圃")))return End(false,TEXT("farm-hint"));Face(FVector(-200,-400,245));if(!CFInputQA::Press(Hero,EKeys::E) || Door->DoorState!=2)return End(false,TEXT("open"));Phase=10;At=World->GetTimeSeconds();return true;}
        if(Phase==10)
        {if(World->GetTimeSeconds()-At<2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;if(!Plan(SeedSupply && !Crafted?FVector(-1320,1400,40):Farm->GetActorLocation()+FVector(-190,-140,40),SeedSupply && !Crafted?20:1))return End(false,TEXT("farm-path"));return true;}
        if(Phase==20)
        {const int32 R=Move();if(R<0)return End(false,TEXT("fiber-walk"));if(!R)return true;Phase=21;At=World->GetTimeSeconds();return true;}
        if(Phase==21)
        {Face(Fiber->GetActorLocation());if(Hero->TraceResource()!=Fiber.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("fiber-sight"));return true;}if(!CFInputQA::Press(Hero,EKeys::M) || !Hero->Gathering->IsGathering())return End(false,TEXT("fiber-input"));Phase=22;return true;}
        if(Phase==22)
        {if(Hero->Inventory->Count(EFrontierItem::Fiber)<1)return true;if(!CFInputQA::Release(Hero,EKeys::M) || Fiber->Remaining!=FiberBefore-1)return End(false,TEXT("fiber-cost"));Gathered=true;if(!Hero->SaveProgress(Slot(TEXT("Mid"))))return End(false,TEXT("fiber-save"));Photo(TEXT("Fiber"),Fiber->GetActorLocation());UE_LOG(LogTemp,Display,TEXT("CF_STARTER_HOME_SEED_MID PASS fiber=1 remaining=%d food=2 seeds=0"),Fiber->Remaining);Phase=24;return true;}
        if(Phase==24){if(!Plan(FVector(50,-400,60),25))return End(false,TEXT("craft-return-path"));return true;}
        if(Phase==25)
        {const int32 R=Move();if(R<0)return End(false,TEXT("craft-walk"));if(!R)return true;Phase=26;return true;}
        if(Phase==26)
        {
            const auto* Catalog=UCraftingCatalog::Load();const auto* Recipe=Catalog?Catalog->Find(TEXT("PlantingSeeds")):nullptr;
            if(!Recipe || Recipe->Inputs.FindRef(EFrontierItem::Food)!=1 || Recipe->Inputs.FindRef(EFrontierItem::Fiber)!=1 || Recipe->Inputs.Num()!=2 || Recipe->Output!=EFrontierItem::Seeds || Recipe->OutputAmount!=3)return End(false,TEXT("seed-recipe"));
            if(!Hero->IsBackpackOpen())
            {if(!CFInputQA::Press(Hero,EKeys::C) || !Hero->IsBackpackOpen())return End(false,TEXT("craft-open"));const int32 Index=Catalog->Recipes.IndexOfByPredicate([](const FFrontierRecipe& R){return R.Id==TEXT("PlantingSeeds");});if(Index<0)return End(false,TEXT("craft-index"));for(int32 I=0;I<Index;++I)if(!CFInputQA::Press(Hero,EKeys::Down))return End(false,TEXT("craft-select"));if(Photo(TEXT("Recipe"),P))return true;}
            if(!CFInputQA::Press(Hero,EKeys::Enter) || Hero->Inventory->Count(EFrontierItem::Seeds)!=3 || Hero->Inventory->Count(EFrontierItem::Food)!=1 || Hero->Inventory->Count(EFrontierItem::Fiber)!=0 || !CFInputQA::Press(Hero,EKeys::C) || Hero->IsBackpackOpen())return End(false,TEXT("craft"));
            Crafted=true;UE_LOG(LogTemp,Display,TEXT("CF_STARTER_HOME_SEED_CRAFT PASS fiberPaid=1 foodPaid=1 seeds=3 menuInput=1"));Photo(TEXT("Crafted"),P);if(!Plan(Farm->GetActorLocation()+FVector(-190,-140,40),1))return End(false,TEXT("reseed-path"));return true;
        }
        if(Phase==1 || Phase==4 || Phase==6 || Phase==8)
        {const int32 R=Move();if(R<0)return End(false,TEXT("walk"));if(!R)return true;Phase=Phase==1?2:Phase==4?5:Phase==6?7:9;At=World->GetTimeSeconds();return true;}
        if(Phase==2)
        {
            Face(Farm->GetActorLocation()+FVector(0,0,30));if(Hero->TraceFacility()!=Farm.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("farm-sight"));return true;}
            if(!CFInputQA::Press(Hero,EKeys::F2) || Hero->Inventory->Count(EFrontierItem::Seeds)!=(SeedSupply?2:0) || Farm->Crop->Seeds!=1)return End(false,TEXT("deposit-seed"));Deposited=true;Photo(TEXT("Seeded"),Farm->GetActorLocation());
            if(!CFInputQA::Press(Hero,EKeys::E) || Pet->WorkSite!=Farm.Get() || Pet->Activity!=ECreatureActivity::Work)return End(false,TEXT("assign"));Phase=3;return true;
        }
        if(Phase==3)
        {
            if(Pet->WorkSite!=Farm.Get())return End(false,TEXT("lost-task"));if(Farm->StoredFood<1)return true;
            if(!FarmMid || Pet->Experience!=BaseXP()+1 || Farm->Crop->Seeds!=0 || Farm->Crop->bPlanted || !Hero->GetObjectiveHint().Contains(TEXT("按O取粮")))return End(false,TEXT("harvest"));
            Face(Farm->GetActorLocation()+FVector(0,0,30));if(!CFInputQA::Press(Hero,EKeys::O) || Farm->StoredFood!=0 || Hero->Inventory->Count(EFrontierItem::Food)!=2)return End(false,TEXT("withdraw"));Withdrew=true;Photo(TEXT("Harvested"),Farm->GetActorLocation());
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_HOME_FARM_HARVEST PASS seedPaid=1 produced=1 withdrawn=1 food=2 xp=%d"),BaseXP()+1);if(!CFInputQA::Press(Hero,EKeys::F) || !Plan(FVector(-470,-400,40),4))return End(false,TEXT("return"));return true;
        }
        if(Phase==5){if(Door->DoorState!=2 || !Plan(FVector(120,-400,60),6))return End(false,TEXT("enter"));return true;}
        if(Phase==7){if(P.X< -140 || P.X>160 || FMath::Abs(P.Y+400)>160 || P.Z<50 || Pet->Activity!=ECreatureActivity::Follow){if(World->GetTimeSeconds()-At>20)return End(false,TEXT("pair-enter"));return true;}if(!Plan(FVector(50,-400,60),8))return End(false,TEXT("close-path"));return true;}
        if(Phase==9)
        {
            Face(FVector(-200,-400,245));if(Door->DoorState==2)
            {if(AHomeModule::TraceDoor(Hero)!=Door.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("close-sight"));return true;}if(!CFInputQA::Press(Hero,EKeys::E) || Door->DoorState!=1)return End(false,TEXT("close"));}
            if(Photo(TEXT("Home"),P))return true;
            if(!Hero->GetObjectiveHint().Contains(SeedSupply?TEXT("水晶"):TEXT("准备种子")))return End(false,TEXT("ready-hint"));const auto HB=H,PB=P;
            if(!Hero->SaveProgress(Slot(TEXT("Done"))) || !Hero->LoadProgress(Slot(TEXT("Done"))))return End(false,TEXT("save"));Find();BindFarm();
            if(!Budget() || Door->DoorState!=1 || !Hero->GetActorLocation().Equals(HB,.01) || !Pet->GetActorLocation().Equals(PB,.01))return End(false,TEXT("restore"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_HOME_FARM_LIFE PASS modules=7 paid=58 food=2 seeds=%d xp=%d returned=1 closed=1 identity=1"),SeedSupply?2:0,BaseXP()+1);return End(true,TEXT("complete"));
        }
        return true;
    }
};
}
void RunStarterHomeFarmScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto S=MakeShared<FStarterHomeFarm>();S->World=World;S->Hero=Hero;S->Read=Read;S->SeedSupply=FParse::Param(FCommandLine::Get(),TEXT("CFStarterHomeSeedTest")) || FParse::Param(FCommandLine::Get(),TEXT("CFStarterHomeSeedRead"));S->Deposited=Read && !S->SeedSupply;S->FarmMid=S->Deposited;S->Gathered=Read && S->SeedSupply;S->At=World->GetTimeSeconds();S->Deadline=FPlatformTime::Seconds()+180;
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(S->Slot(TEXT("Input")),0));
    if(!Source || Source->FormatVersion!=38 || Source->Facilities.Num()!=1 || Source->HomeModules.Num()!=7 || !Hero->LoadProgress(S->Slot(TEXT("Input")))){S->End(false,TEXT("source"));return;}
    S->Original=Source->HomeModules;S->Garden=Source->Facilities[0];for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController()){S->PetId=It->PersistentId;S->Nickname=It->Nickname;}S->Find();S->BindFarm();
    if(S->SeedSupply)for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,8))S->Fiber=*It;
    if(S->SeedSupply && S->Fiber.IsValid())S->FiberBefore=S->Fiber->Remaining+(Read?1:0);
    if(S->SeedSupply)UE_LOG(LogTemp,Display,TEXT("CF_STARTER_HOME_SEED_SOURCE fiberNode=%d fiber=%d seeds=%d xp=%d bond=%d sourcePosition=%d gardenSeeds=%d planted=%d door=%d budget=%d"),S->Fiber.IsValid()?S->Fiber->Remaining:-1,Hero->Inventory->Count(EFrontierItem::Fiber),Hero->Inventory->Count(EFrontierItem::Seeds),S->Pet.IsValid()?S->Pet->Experience:-1,S->Pet.IsValid()?S->Pet->BondPoints:-1,Hero->GetActorTransform().Equals(Source->PlayerTransform,.01),S->Garden.Seeds,S->Garden.bPlanted,S->Door.IsValid()?S->Door->DoorState:-1,S->Budget());
    if(!S->Budget() || S->Pet->Experience!=S->BaseXP() || !Hero->GetActorTransform().Equals(Source->PlayerTransform,.01) || (!Read && (S->Garden.Seeds!=0 || S->Garden.bPlanted || S->Garden.StoredFood!=0 || S->Door->DoorState!=1))){S->End(false,TEXT("source-budget"));return;}
    if(Read && !S->SeedSupply && (!S->Farm->Crop->bPlanted || S->Farm->Crop->GrowthWork<=0 || S->Pet->WorkSite!=S->Farm.Get() || S->Pet->Activity!=ECreatureActivity::Work)){S->End(false,TEXT("source-growing"));return;}
    if(Read && S->SeedSupply && (Hero->Inventory->Count(EFrontierItem::Fiber)!=1 || S->Fiber->Remaining!=S->FiberBefore-1 || S->Door->DoorState!=2)){S->End(false,TEXT("source-fiber"));return;}
    S->LastH=Hero->GetActorLocation();S->LastP=S->Pet->GetActorLocation();S->VelocityH=Hero->GetVelocity();S->VelocityP=S->Pet->GetVelocity();S->LastTime=World->GetTimeSeconds();S->Input=NewObject<UHomeMovementProbe>(Hero);S->Input->RegisterComponent();
    S->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFStarterHomeFarmPreview"));if(S->Preview){S->Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("StarterHomeFarmPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*S->Root,true);UE_LOG(LogTemp,Display,TEXT("CF_STARTER_HOME_FARM_PREVIEW_ROOT %s"),*S->Root);}
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){return S->Tick();}));
}

namespace
{
struct FStarterStudy : FStarterRoom
{
    TWeakObjectPtr<AResourceNode> Stone,Crystal;TMap<FGuid,int32> OtherResources;int32 StoneBefore=0,CrystalBefore=0;
    bool Studied=false,SampleMid=false;double SafeAt=-1;
    void Minerals(){Stone.Reset();Crystal.Reset();for(TActorIterator<AResourceNode> It(World);It;++It){if(It->PersistentId==FGuid(0xCF170000,0,0,2))Stone=*It;if(It->PersistentId==FGuid(0xCF170000,0,0,7))Crystal=*It;}}
    bool Travel(FVector Goal,int32 Next,bool Normal=false){if(!Plan(Goal,Next))return false;return !Normal || CFInputQA::Release(Hero,EKeys::LeftShift);}
    bool End(bool Pass,const TCHAR* Reason)
    {
        if(Input.IsValid())Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::M);CFInputQA::Release(Hero,EKeys::LeftShift);
        const auto Feedback=Hero->Feedback;if(!Pass)Hero->SaveProgress(TEXT("StarterStudyBlocked"));
        UE_LOG(LogTemp,Display,TEXT("CF_STARTER_STUDY %s reason=%s phase=%d heroTravel=%.1f petTravel=%.1f heroHP=%.1f petHP=%.1f material=%d crystal=%d feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Reason,Phase,HeroTravel,PetTravel,Hero->Health,Pet.IsValid()?Pet->Health:-1,Hero->Inventory->Count(EFrontierItem::Material),Hero->Inventory->Count(EFrontierItem::Crystal),*Feedback);FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    bool Budget()const
    {
        if(!Stone.IsValid() || !Crystal.IsValid() || !Pet.IsValid() || !Door.IsValid() || Hero->Health<=0 || Pet->Health<=0 || Pet->Level!=2 || Pet->Experience!=39 || Pet->BondPoints!=6 || Pet->Nickname!=Nickname)return false;
        const int32 Rock=StoneBefore-Stone->Remaining,Sample=CrystalBefore-Crystal->Remaining;
        if(Rock<0 || Rock>2 || Sample<0 || Sample>2 || Hero->Inventory->Count(EFrontierItem::Material)+(Studied?2:0)!=Rock || Hero->Inventory->Count(EFrontierItem::Crystal)+(Studied?1:0)!=Sample || Hero->Research->Has(EFrontierResearch::CrystalStudy)!=Studied || Hero->Research->Has(EFrontierResearch::CrystalSample)!=(Sample>0) || Hero->Inventory->Count(EFrontierItem::Food)!=2 || Hero->Inventory->Count(EFrontierItem::Seeds)!=2 || Pet->Cargo->Count()!=0 || Hero->Inventory->Count(EFrontierItem::Medicine)!=0 || Hero->Inventory->Count(EFrontierItem::CaptureDevice)!=10 || Hero->Inventory->Count(EFrontierItem::Scrap)!=0 || Hero->Inventory->Count(EFrontierItem::ResonanceDevice)!=0 || Hero->GetObjectives().Claimed!=5 || !Hero->GetObjectives().bAlphaResolved)return false;
        const auto Layout=HomeLayout::Collect(World);if(Layout.Num()!=7)return false;
        for(const auto& R:Original){const auto* V=Layout.FindByPredicate([&](const FHomeModuleRecord& X){return X.Id==R.Id;});if(!V || !V->Transform.Equals(R.Transform,.01) || V->Definition!=R.Definition || V->PaidMaterials!=R.PaidMaterials || !V->StoredItems.OrderIndependentCompareEqual(R.StoredItems))return false;}
        int32 Farms=0;for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt){++Farms;if(It->PersistentId!=Garden.Id || !It->GetActorTransform().Equals(Garden.Transform,.01) || It->StoredFood!=Garden.StoredFood || It->Crop->Seeds!=Garden.Seeds || It->Crop->bPlanted!=Garden.bPlanted || It->Crop->GrowthWork!=Garden.GrowthWork)return false;}
        for(TActorIterator<AResourceNode> It(World);It;++It)if(const int32* Before=OtherResources.Find(It->PersistentId))if(It->Remaining!=*Before)return false;
        int32 Paid=0;for(const auto& R:Original)Paid+=R.PaidMaterials;return Farms==1 && Paid==58;
    }
    bool Tick()
    {
        if(!Budget() || FPlatformTime::Seconds()>Deadline)return End(false,TEXT("budget-time"));
        const auto H=Hero->GetActorLocation(),P=Pet->GetActorLocation();const double Delta=World->GetTimeSeconds()-LastTime;
        if(!Motion(Hero,LastH,VelocityH,Delta) || !Motion(Pet.Get(),LastP,VelocityP,Delta))return End(false,TEXT("motion"));
        HeroTravel+=FVector::Dist(H,LastH);PetTravel+=FVector::Dist(P,LastP);LastH=H;LastP=P;VelocityH=Hero->GetVelocity();VelocityP=Pet->GetVelocity();LastTime=World->GetTimeSeconds();
        int32 Regions=0;for(TActorIterator<AWildEncounter> It(World);It;++It){++Regions;if(!It->IsActorTickEnabled())return End(false,TEXT("region"));}if(Regions!=3)return End(false,TEXT("regions"));
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Health>0 && (!It->IsActorTickEnabled() || !It->GetActorEnableCollision() || It->GetCharacterMovement()->MovementMode==MOVE_None))return End(false,TEXT("ecology"));
        if(World->GetTimeSeconds()>=NextDiagnostic){NextDiagnostic=World->GetTimeSeconds()+5;UE_LOG(LogTemp,Display,TEXT("CF_STARTER_STUDY_STATE phase=%d hero=%s pet=%s hp=%.1f/%.1f rocks=%d crystals=%d"),Phase,*H.ToString(),*P.ToString(),Hero->Health,Pet->Health,Stone->Remaining,Crystal->Remaining);}
        if(FPlatformTime::Seconds()<ShotUntil)return true;
        if(Phase==-1)
        {if(World->GetTimeSeconds()-At<2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;if(Read){if(!Travel(FVector(50,-400,60),9))return End(false,TEXT("read-return"));return true;}Phase=0;At=World->GetTimeSeconds();return true;}
        if(Phase==0)
        {if(Photo(TEXT("Departure"),P))return true;if(!Hero->GetObjectiveHint().Contains(TEXT("水晶")))return End(false,TEXT("departure-hint"));Face(FVector(-200,-400,245));if(AHomeModule::TraceDoor(Hero)!=Door.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("open-sight"));return true;}if(!CFInputQA::Press(Hero,EKeys::E) || Door->DoorState!=2 || !CFInputQA::Press(Hero,EKeys::F))return End(false,TEXT("open-follow"));Phase=1;At=World->GetTimeSeconds();return true;}
        if(Phase==1){if(World->GetTimeSeconds()-At<2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;if(!Travel(FVector(-470,-60,40),2))return End(false,TEXT("stone-path"));return true;}
        if(Phase==2 || Phase==5 || Phase==6 || Phase==8 || Phase==9 || Phase==11 || Phase==13)
        {const int32 R=Move();if(R<0)return End(false,TEXT("walk"));if(!R)return true;At=World->GetTimeSeconds();
            if(Phase==2)Phase=3;else if(Phase==5){if(!Travel(FVector(1450,1800,40),6,true))return End(false,TEXT("sample-path"));}else if(Phase==6)Phase=7;else if(Phase==8)Phase=14;else if(Phase==9)Phase=10;else if(Phase==11)Phase=12;else Phase=15;return true;}
        if(Phase==3)
        {Face(Stone->GetActorLocation());if(Hero->TraceResource()!=Stone.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("stone-sight"));return true;}if(!CFInputQA::Press(Hero,EKeys::M))return End(false,TEXT("stone-input"));Phase=4;return true;}
        if(Phase==4)
        {if(Hero->Inventory->Count(EFrontierItem::Material)<2){if(!Hero->Gathering->IsGathering())return End(false,TEXT("stone-interrupted"));return true;}if(!CFInputQA::Release(Hero,EKeys::M) || StoneBefore-Stone->Remaining!=2)return End(false,TEXT("stone-cost"));Photo(TEXT("Materials"),Stone->GetActorLocation());if(!Travel(FVector(900,1800,40),5))return End(false,TEXT("north-path"));return true;}
        if(Phase==7)
        {
            Face(Crystal->GetActorLocation());if(Hero->Inventory->Count(EFrontierItem::Crystal)==0){if(Hero->TraceResource()!=Crystal.Get()){if(World->GetTimeSeconds()-At>2)return End(false,TEXT("crystal-sight"));return true;}if(!Hero->Gathering->IsGathering() && !CFInputQA::Press(Hero,EKeys::M))return End(false,TEXT("crystal-input"));}
            if(Hero->Inventory->Count(EFrontierItem::Crystal)<2){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("crystal-gather"));return true;}
            if(!CFInputQA::Release(Hero,EKeys::M) || CrystalBefore-Crystal->Remaining!=2 || !Hero->Research->Has(EFrontierResearch::CrystalSample))return End(false,TEXT("sample-cost"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_STUDY_SAMPLE PASS materialPaid=2 crystalPaid=2 sampled=1"));Photo(TEXT("Sample"),Crystal->GetActorLocation());if(!CFInputQA::Press(Hero,EKeys::F) || !Travel(FVector(200,1800,40),8,true))return End(false,TEXT("withdraw-path"));return true;
        }
        if(Phase==14)
        {
            bool Safe=Pet->GetDistanceTo(Hero)<250;
            for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Disposition==ECreatureDisposition::Hostile && It->Health>0 && (It->GetDistanceTo(Hero)<700 || It->GetDistanceTo(Pet.Get())<700))Safe=false;
            if(!Safe){SafeAt=-1;if(World->GetTimeSeconds()-At>20)return End(false,TEXT("withdraw-safe"));return true;}
            if(SafeAt<0)SafeAt=World->GetTimeSeconds();if(World->GetTimeSeconds()-SafeAt<2)return true;
            if(!Hero->SaveProgress(TEXT("StarterStudyMid")))return End(false,TEXT("mid-save"));SampleMid=true;Photo(TEXT("Retreated"),P);UE_LOG(LogTemp,Display,TEXT("CF_STARTER_STUDY_MID PASS material=2 crystal=2 sample=1 study=0 safe=1"));if(!Travel(FVector(50,-400,60),9))return End(false,TEXT("return-path"));return true;
        }
        if(Phase==10)
        {
            const auto* Catalog=UCraftingCatalog::Load();if(!Catalog)return End(false,TEXT("catalog"));
            if(!Hero->IsBackpackOpen()){if(!CFInputQA::Press(Hero,EKeys::C) || !Hero->IsBackpackOpen())return End(false,TEXT("menu"));const int32 Index=Catalog->Recipes.IndexOfByPredicate([](const FFrontierRecipe& R){return R.Id==TEXT("StudyCrystal");});if(Index<0)return End(false,TEXT("recipe"));for(int32 I=0;I<Index;++I)if(!CFInputQA::Press(Hero,EKeys::Down))return End(false,TEXT("recipe-select"));if(Photo(TEXT("Research"),P))return true;}
            if(!CFInputQA::Press(Hero,EKeys::Enter) || !Hero->Research->Has(EFrontierResearch::CrystalStudy) || Hero->Inventory->Count(EFrontierItem::Material)!=0 || Hero->Inventory->Count(EFrontierItem::Crystal)!=1)return End(false,TEXT("study"));Studied=true;
            const auto Bag=Hero->Inventory->GetStacks();if(!CFInputQA::Press(Hero,EKeys::Enter) || !Hero->Inventory->GetStacks().OrderIndependentCompareEqual(Bag) || !CFInputQA::Press(Hero,EKeys::C) || Hero->IsBackpackOpen())return End(false,TEXT("repeat-study"));
            const auto* Repair=Catalog->Find(TEXT("ScrapResonator"));bool CanCraft=false;const auto Status=Repair?Hero->GetRecipeStatus(*Repair,&CanCraft):FString();if(!Repair || CanCraft || !Status.Contains(TEXT("机械废件")) || Status.Contains(TEXT("尚未解锁")))return End(false,TEXT("unlock"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_STUDY_RESEARCH PASS materialUsed=2 crystalUsed=1 repeated=0 repairUnlocked=1"));Photo(TEXT("Studied"),P);if(!SampleMid || !Travel(FVector(120,-400,60),11))return End(false,TEXT("regroup-path"));return true;
        }
        if(Phase==12){if(P.X< -140 || P.X>160 || FMath::Abs(P.Y+400)>160 || P.Z<50 || Pet->Activity!=ECreatureActivity::Follow){if(World->GetTimeSeconds()-At>20)return End(false,TEXT("pair-enter"));return true;}if(!Travel(FVector(50,-400,60),13))return End(false,TEXT("close-path"));return true;}
        if(Phase==15)
        {
            Face(FVector(-200,-400,245));if(Door->DoorState==2){if(AHomeModule::TraceDoor(Hero)!=Door.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("close-sight"));return true;}if(!CFInputQA::Press(Hero,EKeys::E) || Door->DoorState!=1)return End(false,TEXT("close"));}if(Photo(TEXT("Home"),P))return true;
            const auto HB=H,PB=P;if(!Hero->SaveProgress(TEXT("StarterStudyDone")) || !Hero->LoadProgress(TEXT("StarterStudyDone")))return End(false,TEXT("save"));Find();Minerals();
            if(!Budget() || Door->DoorState!=1 || !Hero->GetActorLocation().Equals(HB,.01) || !Pet->GetActorLocation().Equals(PB,.01))return End(false,TEXT("restore"));UE_LOG(LogTemp,Display,TEXT("CF_STARTER_STUDY_LIFE PASS modules=7 paid=58 material=0 crystal=1 study=1 food=2 seeds=2 returned=1 closed=1 identity=1"));return End(true,TEXT("complete"));
        }
        return true;
    }
};
}
void RunStarterStudyScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto S=MakeShared<FStarterStudy>();S->World=World;S->Hero=Hero;S->Read=Read;S->SampleMid=Read;S->At=World->GetTimeSeconds();S->Deadline=FPlatformTime::Seconds()+180;
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("StarterStudyInput"),0));
    if(!Source || Source->FormatVersion!=38 || Source->Facilities.Num()!=1 || Source->HomeModules.Num()!=7 || Source->Research.bCrystalStudy || !Hero->LoadProgress(TEXT("StarterStudyInput"))){S->End(false,TEXT("source"));return;}
    S->Original=Source->HomeModules;S->Garden=Source->Facilities[0];for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController()){S->PetId=It->PersistentId;S->Nickname=It->Nickname;}S->Find();S->Minerals();
    if(S->Stone.IsValid())S->StoneBefore=S->Stone->Remaining+(Read?2:0);if(S->Crystal.IsValid())S->CrystalBefore=S->Crystal->Remaining+(Read?2:0);
    for(const auto& R:Source->ResourceNodes)if(R.Id!=FGuid(0xCF170000,0,0,2) && R.Id!=FGuid(0xCF170000,0,0,7))S->OtherResources.Add(R.Id,R.Remaining);
    if(!S->Budget() || !Hero->GetActorTransform().Equals(Source->PlayerTransform,.01) || (Read && (Hero->Inventory->Count(EFrontierItem::Crystal)!=2 || Hero->Inventory->Count(EFrontierItem::Material)!=2)) || (!Read && (Source->Research.bCrystalSample || Hero->Inventory->Count(EFrontierItem::Material)!=0 || Hero->Inventory->Count(EFrontierItem::Crystal)!=0))){S->End(false,TEXT("source-budget"));return;}
    for(TActorIterator<AWildEncounter> It(World);It;++It)if(It->bBoss)UE_LOG(LogTemp,Display,TEXT("CF_STARTER_STUDY_SOURCE bossAlive=%d remaining=%.1f stone=%d crystal=%d"),It->GetResident()!=nullptr,It->GetSavedRemaining(),S->StoneBefore,S->CrystalBefore);
    S->LastH=Hero->GetActorLocation();S->LastP=S->Pet->GetActorLocation();S->VelocityH=Hero->GetVelocity();S->VelocityP=S->Pet->GetVelocity();S->LastTime=World->GetTimeSeconds();S->Input=NewObject<UHomeMovementProbe>(Hero);S->Input->RegisterComponent();
    S->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFStarterStudyPreview"));if(S->Preview){S->Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("StarterStudyPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*S->Root,true);UE_LOG(LogTemp,Display,TEXT("CF_STARTER_STUDY_PREVIEW_ROOT %s"),*S->Root);}
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){return S->Tick();}));
}

namespace
{
// 沿用真实研究档、原药品和休息规则，再把洞穴废件实际带回加工。
struct FStarterForge : FStarterRoom
{
    TWeakObjectPtr<AResourceNode> Stone,Cache;TMap<FGuid,int32> OtherResources;
    int32 StoneBefore=0,CacheBefore=0,Crafted=0,Used=0,Leg=0;bool Repaired=false,Returning=false,SafeMid=false,CraftAccessWalk=false;
    double NextDiagnostic=0,SafeAt=-1;
    void Minerals(){Stone.Reset();Cache.Reset();for(TActorIterator<AResourceNode> It(World);It;++It){if(It->PersistentId==FGuid(0xCF170000,0,0,2))Stone=*It;if(It->PersistentId==FGuid(0xCF170000,0,0,11))Cache=*It;}}
    bool Travel(FVector Goal,int32 Next,bool Normal=false){if(!Plan(Goal,Next))return false;return !Normal || CFInputQA::Release(Hero,EKeys::LeftShift);}
    bool End(bool Pass,const TCHAR* Reason)
    {
        if(Input.IsValid())Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::M);CFInputQA::Release(Hero,EKeys::LeftShift);if(!Pass)Hero->SaveProgress(TEXT("StarterForgeBlocked"));
        UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FORGE %s reason=%s phase=%d leg=%d return=%d heroTravel=%.1f petTravel=%.1f hero=%s pet=%s hp=%.1f/%.1f crafted=%d used=%d feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Reason,Phase,Leg,Returning,HeroTravel,PetTravel,*Hero->GetActorLocation().ToString(),Pet.IsValid()?*Pet->GetActorLocation().ToString():TEXT("none"),Hero->Health,Pet.IsValid()?Pet->Health:-1,Crafted,Used,*Hero->Feedback);FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    bool Budget()const
    {
        if(!Stone.IsValid() || !Cache.IsValid() || !Pet.IsValid() || !Door.IsValid() || Hero->Health<=0 || Pet->Health<=0 || Pet->Level!=2 || Pet->Experience!=39 || Pet->BondPoints!=6 || Pet->Nickname!=Nickname)return false;
        const int32 Rocks=StoneBefore-Stone->Remaining,Scrap=CacheBefore-Cache->Remaining;
        if(Rocks<0 || Rocks>10 || Scrap<0 || Scrap>2 || Hero->Inventory->Count(EFrontierItem::Material)+5*Crafted!=Rocks || Hero->Inventory->Count(EFrontierItem::Medicine)!=Crafted-Used || Hero->Inventory->Count(EFrontierItem::Scrap)+(Repaired?2:0)!=Scrap || Hero->Inventory->Count(EFrontierItem::Crystal)!=(Repaired?0:1) || Hero->Inventory->Count(EFrontierItem::ResonanceDevice)!=(Repaired?2:0) || !Hero->Research->Has(EFrontierResearch::CrystalStudy) || Hero->Inventory->Count(EFrontierItem::Food)!=2 || Hero->Inventory->Count(EFrontierItem::Seeds)!=2 || Hero->Inventory->Count(EFrontierItem::CaptureDevice)!=10 || Hero->Inventory->Count(EFrontierItem::Fiber)!=0 || Hero->Inventory->Count(EFrontierItem::Ore)!=0 || Pet->Cargo->Count()!=0 || Hero->GetObjectives().Claimed!=5 || !Hero->GetObjectives().bAlphaResolved)return false;
        const auto Layout=HomeLayout::Collect(World);if(Layout.Num()!=7)return false;int32 Paid=0;
        for(const auto& R:Original){const auto* V=Layout.FindByPredicate([&](const FHomeModuleRecord& X){return X.Id==R.Id;});if(!V || !V->Transform.Equals(R.Transform,.01) || V->Definition!=R.Definition || V->PaidMaterials!=R.PaidMaterials || !V->StoredItems.OrderIndependentCompareEqual(R.StoredItems))return false;Paid+=V->PaidMaterials;}
        int32 Farms=0;for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt){++Farms;if(It->PersistentId!=Garden.Id || !It->GetActorTransform().Equals(Garden.Transform,.01) || It->StoredFood!=Garden.StoredFood || It->Crop->Seeds!=Garden.Seeds || It->Crop->bPlanted!=Garden.bPlanted || It->Crop->GrowthWork!=Garden.GrowthWork)return false;}
        for(TActorIterator<AResourceNode> It(World);It;++It)if(const int32* Before=OtherResources.Find(It->PersistentId))if(It->Remaining!=*Before)return false;
        return Paid==58 && Farms==1;
    }
    bool Tick()
    {
        if(!Budget() || FPlatformTime::Seconds()>Deadline)return End(false,TEXT("budget-time"));const auto H=Hero->GetActorLocation(),P=Pet->GetActorLocation();const double Dt=World->GetTimeSeconds()-LastTime;
        if(!Motion(Hero,LastH,VelocityH,Dt) || !Motion(Pet.Get(),LastP,VelocityP,Dt))return End(false,TEXT("motion"));HeroTravel+=FVector::Dist(H,LastH);PetTravel+=FVector::Dist(P,LastP);LastH=H;LastP=P;VelocityH=Hero->GetVelocity();VelocityP=Pet->GetVelocity();LastTime=World->GetTimeSeconds();
        int32 Regions=0;for(TActorIterator<AWildEncounter> It(World);It;++It){++Regions;if(!It->IsActorTickEnabled())return End(false,TEXT("region"));}if(Regions!=3)return End(false,TEXT("regions"));
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Health>0 && (!It->IsActorTickEnabled() || !It->GetActorEnableCollision() || It->GetCharacterMovement()->MovementMode==MOVE_None))return End(false,TEXT("ecology"));
        if(World->GetTimeSeconds()>=NextDiagnostic){NextDiagnostic=World->GetTimeSeconds()+5;UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FORGE_STATE phase=%d leg=%d return=%d hero=%s pet=%s hp=%.1f/%.1f stone=%d cache=%d"),Phase,Leg,Returning,*H.ToString(),*P.ToString(),Hero->Health,Pet->Health,Stone->Remaining,Cache->Remaining);}
        if(FPlatformTime::Seconds()<ShotUntil)return true;
        if(Phase==-1){if(World->GetTimeSeconds()-At<2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;if(Read){if(!Travel(FVector(50,-400,60),9))return End(false,TEXT("resume-path"));}else{Phase=0;At=World->GetTimeSeconds();}return true;}
        if(Phase==0){if(Photo(TEXT("Departure"),P))return true;Face(FVector(-200,-400,245));if(AHomeModule::TraceDoor(Hero)!=Door.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("door-sight"));return true;}if(!CFInputQA::Press(Hero,EKeys::E) || Door->DoorState!=2 || !CFInputQA::Press(Hero,EKeys::T) || Pet->Activity!=ECreatureActivity::Rest || !Pet->WorkSite)return End(false,TEXT("open-rest"));Bench=Pet->WorkSite;Phase=1;At=World->GetTimeSeconds();return true;}
        if(Phase==1){if(World->GetTimeSeconds()-At<2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;if(!Travel(FVector(-470,-60,40),2))return End(false,TEXT("stone-path"));return true;}
        if(Phase==2 || Phase==4 || Phase==6 || Phase==9 || Phase==11 || Phase==13){const int32 R=Move();if(R<0)return End(false,TEXT("walk"));if(!R)return true;At=World->GetTimeSeconds();Phase=Phase==2?3:Phase==4?5:Phase==6?7:Phase==9?10:Phase==11?12:15;return true;}
        if(Phase==3){Face(Stone->GetActorLocation());if(Hero->Inventory->Count(EFrontierItem::Material)==0){if(Hero->TraceResource()!=Stone.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("stone-sight"));return true;}if(!Hero->Gathering->IsGathering() && !CFInputQA::Press(Hero,EKeys::M))return End(false,TEXT("stone-input"));}if(Hero->Inventory->Count(EFrontierItem::Material)<10){if(!Hero->Gathering->IsGathering() || World->GetTimeSeconds()-At>20)return End(false,TEXT("stone-interrupted"));return true;}if(!CFInputQA::Release(Hero,EKeys::M) || !Travel(Bench->GetActorLocation()+FVector(-240,-180,40),4))return End(false,TEXT("bench-path"));return true;}
        if(Phase==5){if(Crafted==0){for(int32 I=0;I<2;++I){if(!CFInputQA::Press(Hero,EKeys::Two))return End(false,TEXT("medicine-input"));++Crafted;}while(Hero->Health<100 && Used<2){if(!CFInputQA::Press(Hero,EKeys::H))return End(false,TEXT("medicine-use"));++Used;}}if(Hero->Health!=100 || Pet->Activity!=ECreatureActivity::Rest || Pet->WorkSite!=Bench.Get())return End(false,TEXT("recovery-state"));if(Pet->Health<Pet->GetMaxHealth() || Pet->Stamina<99){if(World->GetTimeSeconds()-At>35)return End(false,TEXT("rest-timeout"));return true;}if(Photo(TEXT("Prepared"),P))return true;if(!CFInputQA::Press(Hero,EKeys::F))return End(false,TEXT("follow"));UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FORGE_PREPARED PASS stonePaid=10 crafted=2 used=%d heroHP=%.1f petHP=%.1f"),Used,Hero->Health,Pet->Health);Leg=0;if(!Travel(MakeHollowRoutePoints()[Leg],6))return End(false,TEXT("approach-path"));return true;}
        if(Phase==7){const auto Points=MakeHollowRoutePoints();if(!Returning){if(Leg==2 && Photo(TEXT("Entrance"),P))return true;if(++Leg<Points.Num()){if(!Travel(Points[Leg],6,true))return End(false,TEXT("cave-path"));return true;}Phase=8;At=World->GetTimeSeconds();return true;}if(--Leg>=1){if(!Travel(Points[Leg],6,true))return End(false,TEXT("withdraw-path"));return true;}Phase=14;At=World->GetTimeSeconds();return true;}
        if(Phase==8){Face(Cache->GetActorLocation());if(Hero->Inventory->Count(EFrontierItem::Scrap)==0){if(Hero->TraceResource()!=Cache.Get()){if(World->GetTimeSeconds()-At>3)return End(false,TEXT("cache-sight"));return true;}if(!Hero->Gathering->IsGathering() && !CFInputQA::Press(Hero,EKeys::M))return End(false,TEXT("cache-input"));}if(Hero->Inventory->Count(EFrontierItem::Scrap)<2){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("cache-gather"));return true;}if(!CFInputQA::Release(Hero,EKeys::M) || CacheBefore-Cache->Remaining!=2)return End(false,TEXT("cache-cost"));Photo(TEXT("Scrap"),Cache->GetActorLocation());UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FORGE_SCRAP PASS sourcePaid=2 actualGather=1"));Returning=true;Leg=MakeHollowRoutePoints().Num()-1;if(!Travel(MakeHollowRoutePoints()[Leg],6,true))return End(false,TEXT("withdraw-start"));return true;}
        if(Phase==14){bool Safe=Pet->GetDistanceTo(Hero)<250;for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Disposition==ECreatureDisposition::Hostile && It->Health>0 && (It->GetDistanceTo(Hero)<700 || It->GetDistanceTo(Pet.Get())<700))Safe=false;if(!Safe){SafeAt=-1;if(World->GetTimeSeconds()-At>20)return End(false,TEXT("safe-meet"));return true;}if(SafeAt<0)SafeAt=World->GetTimeSeconds();if(World->GetTimeSeconds()-SafeAt<2)return true;if(!Hero->SaveProgress(TEXT("StarterForgeMid")))return End(false,TEXT("mid-save"));SafeMid=true;Photo(TEXT("Retreated"),P);UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FORGE_MID PASS scrap=2 crystal=1 safe=1 crafted=%d used=%d"),Crafted,Used);if(!Travel(FVector(50,-400,60),9))return End(false,TEXT("home-path"));return true;}
        if(Phase==16){const int32 Result=Move();if(Result<0)return End(false,TEXT("visible-bench-walk"));if(Result){Phase=10;At=World->GetTimeSeconds();}return true;}
        if(Phase==10){const auto* Catalog=UCraftingCatalog::Load();if(!Catalog)return End(false,TEXT("catalog"));if(!Hero->IsBackpackOpen()){if(!CFInputQA::Press(Hero,EKeys::C))return End(false,TEXT("menu"));const int32 Index=Catalog->Recipes.IndexOfByPredicate([](const FFrontierRecipe& R){return R.Id==TEXT("ScrapResonator");});if(Index<0)return End(false,TEXT("recipe"));for(int32 I=0;I<Index;++I)if(!CFInputQA::Press(Hero,EKeys::Down))return End(false,TEXT("select"));if(Photo(TEXT("Recipe"),P))return true;}if(!Hero->IsNearWorkStation()){const auto Before=Hero->Inventory->GetStacks();if(CraftAccessWalk || !CFInputQA::Press(Hero,EKeys::Enter) || !Hero->Inventory->GetStacks().OrderIndependentCompareEqual(Before) || !Hero->Feedback.Contains(TEXT("无遮挡")) || !CFInputQA::Press(Hero,EKeys::C))return End(false,TEXT("blocked-craft"));CraftAccessWalk=true;UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FORGE_ACCESS PASS wallRejected=1 noSpend=1 walkingToBench=1"));if(!Travel(FVector(-450,150,40),16))return End(false,TEXT("visible-bench-path"));return true;}if(!CFInputQA::Press(Hero,EKeys::Enter) || Hero->Inventory->Count(EFrontierItem::ResonanceDevice)!=2 || Hero->Inventory->Count(EFrontierItem::Crystal)!=0 || Hero->Inventory->Count(EFrontierItem::Scrap)!=0)return End(false,TEXT("repair"));Repaired=true;const auto Bag=Hero->Inventory->GetStacks();if(!CFInputQA::Press(Hero,EKeys::Enter) || !Hero->Inventory->GetStacks().OrderIndependentCompareEqual(Bag) || !CFInputQA::Press(Hero,EKeys::C))return End(false,TEXT("repeat-reject"));UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FORGE_REPAIR PASS scrapPaid=2 crystalPaid=1 output=2 repeated=0"));Photo(TEXT("Repaired"),P);if(!SafeMid || !Travel(FVector(120,-400,60),11))return End(false,TEXT("pair-path"));return true;}
        if(Phase==12){if(P.X< -140 || P.X>160 || FMath::Abs(P.Y+400)>160 || P.Z<50 || Pet->Activity!=ECreatureActivity::Follow){if(World->GetTimeSeconds()-At>20)return End(false,TEXT("pair-enter"));return true;}if(!Travel(FVector(50,-400,60),13))return End(false,TEXT("close-path"));return true;}
        if(Phase==15){Face(FVector(-200,-400,245));if(Door->DoorState==2){if(AHomeModule::TraceDoor(Hero)!=Door.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("close-sight"));return true;}if(!CFInputQA::Press(Hero,EKeys::E) || Door->DoorState!=1)return End(false,TEXT("close"));}if(Photo(TEXT("Home"),P))return true;const auto HP=Hero->Health,PP=Pet->Health;const auto HB=H,PB=P;if(!Hero->SaveProgress(TEXT("StarterForgeDone")) || !Hero->LoadProgress(TEXT("StarterForgeDone")))return End(false,TEXT("save"));Find();Minerals();if(!Budget() || Door->DoorState!=1 || Hero->Health!=HP || Pet->Health!=PP || !Hero->GetActorLocation().Equals(HB,.01) || !Pet->GetActorLocation().Equals(PB,.01))return End(false,TEXT("restore"));UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FORGE_LIFE PASS modules=7 paid=58 output=2 crystal=0 scrap=0 food=2 seeds=2 closed=1 identity=1"));return End(true,TEXT("complete"));}
        return true;
    }
};
}
void RunStarterForgeScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto S=MakeShared<FStarterForge>();S->World=World;S->Hero=Hero;S->Read=Read;S->SafeMid=Read;S->At=World->GetTimeSeconds();S->Deadline=FPlatformTime::Seconds()+210;
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("StarterForgeInput"),0));if(!Source || Source->FormatVersion!=38 || Source->Facilities.Num()!=1 || Source->HomeModules.Num()!=7 || !Source->Research.bCrystalStudy || !Hero->LoadProgress(TEXT("StarterForgeInput"))){S->End(false,TEXT("source"));return;}
    S->Original=Source->HomeModules;S->Garden=Source->Facilities[0];for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController()){S->PetId=It->PersistentId;S->Nickname=It->Nickname;}S->Find();S->Minerals();
    if(S->Stone.IsValid())S->StoneBefore=S->Stone->Remaining+(Read?10:0);if(S->Cache.IsValid())S->CacheBefore=S->Cache->Remaining+(Read?2:0);if(Read){S->Crafted=2;S->Used=2-Hero->Inventory->Count(EFrontierItem::Medicine);}
    for(const auto& R:Source->ResourceNodes)if(R.Id!=FGuid(0xCF170000,0,0,2) && R.Id!=FGuid(0xCF170000,0,0,11))S->OtherResources.Add(R.Id,R.Remaining);
    if(!S->Budget() || !Hero->GetActorTransform().Equals(Source->PlayerTransform,.01) || Hero->Inventory->Count(EFrontierItem::Material)!=0 || Hero->Inventory->Count(EFrontierItem::Scrap)!=(Read?2:0)){S->End(false,TEXT("source-budget"));return;}
    S->LastH=Hero->GetActorLocation();S->LastP=S->Pet->GetActorLocation();S->VelocityH=Hero->GetVelocity();S->VelocityP=S->Pet->GetVelocity();S->LastTime=World->GetTimeSeconds();S->Input=NewObject<UHomeMovementProbe>(Hero);S->Input->RegisterComponent();S->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFStarterForgePreview"));if(S->Preview){S->Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("StarterForgePreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*S->Root,true);UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FORGE_PREVIEW_ROOT %s"),*S->Root);}FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){return S->Tick();}));
}

namespace
{
struct FStarterRecruit : FStarterRoom
{
    TWeakObjectPtr<ACreatureCharacter> Recruit;TWeakObjectPtr<AResourceNode> Salvage;
    FGuid RecruitId;TMap<EFrontierItem,int32> Bag;TMap<FGuid,int32> Resources;
    int32 Attempts=0,Produced=0,SalvageBefore=0,Swings=0;bool Caught=false,RestOrdered=false,Recovered=false;
    double NextAction=0,NextDiagnostic=0;FVector LastR,VelocityR;double RecruitTravel=0;
    void FindRecruit(){Recruit.Reset();Salvage.Reset();for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->PersistentId==RecruitId)Recruit=*It;for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,3))Salvage=*It;}
    bool End(bool Pass,const TCHAR* Why){if(Input.IsValid())Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::LeftShift);if(!Pass)Hero->SaveProgress(TEXT("StarterRecruitBlocked"));UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RECRUIT %s reason=%s phase=%d attempts=%d swings=%d produced=%d hp=%.1f/%.1f/%.1f travel=%.1f/%.1f/%.1f feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Why,Phase,Attempts,Swings,Produced,Hero->Health,Pet.IsValid()?Pet->Health:-1,Recruit.IsValid()?Recruit->Health:-1,HeroTravel,PetTravel,RecruitTravel,*Hero->Feedback);FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;}
    bool Budget()const
    {
        if(!Pet.IsValid() || !Recruit.IsValid() || !Salvage.IsValid() || Hero->Health<=0 || Pet->Health<=0 || Recruit->Health<=0 || Attempts<0 || Attempts>2 || Produced<0 || Produced>1 || Pet->Level!=2 || Pet->Experience!=39 || Pet->BondPoints!=6 || Pet->Nickname!=Nickname || Recruit->Level!=1 || Recruit->Experience!=Produced || Recruit->BondPoints!=0 || !Hero->Research->Has(EFrontierResearch::CrystalStudy))return false;
        auto Expected=Bag;Expected.Add(EFrontierItem::ResonanceDevice,2-Attempts);Expected.Add(EFrontierItem::Material,Produced);for(int32 I=0;I<static_cast<int32>(EFrontierItem::Count);++I)if(Hero->Inventory->Count(static_cast<EFrontierItem>(I))!=Expected.FindRef(static_cast<EFrontierItem>(I)))return false;if(Hero->GetCompanionRoster().Num()!=(Caught?2:1) || (Caught && Recruit->Tamer!=Hero->GetController()))return false;
        if(RestOrdered && (Pet->Activity!=ECreatureActivity::Rest || Pet->WorkSite!=Bench.Get()))return false;
        const auto Layout=HomeLayout::Collect(World);if(Layout.Num()!=Original.Num())return false;
        for(const auto& R:Original){const auto* V=Layout.FindByPredicate([&](const FHomeModuleRecord& X){return X.Id==R.Id;});if(!V || V->Definition!=R.Definition || !V->Transform.Equals(R.Transform,.01) || V->PaidMaterials!=R.PaidMaterials || !V->StoredItems.OrderIndependentCompareEqual(R.StoredItems))return false;}
        int32 Farms=0;for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt){++Farms;if(It->PersistentId!=Garden.Id || !It->GetActorTransform().Equals(Garden.Transform,.01) || It->StoredFood!=Garden.StoredFood || It->Crop->Seeds!=Garden.Seeds || It->Crop->bPlanted!=Garden.bPlanted || It->Crop->GrowthWork!=Garden.GrowthWork)return false;}
        for(TActorIterator<AResourceNode> It(World);It;++It)if(const int32* Before=Resources.Find(It->PersistentId))if(It->Remaining!=*Before-(It->PersistentId==FGuid(0xCF170000,0,0,3)?Produced:0))return false;
        return Farms==1 && Pet->Cargo->Count()==0 && Recruit->Cargo->Count()==0;
    }
    bool Tick()
    {
        // 产出由原组件在帧内发生，先读实际守恒差，再验证；不会写资源或库存。
        if(Salvage.IsValid())Produced=SalvageBefore-Salvage->Remaining;
        if(!Budget() || FPlatformTime::Seconds()>Deadline)return End(false,TEXT("budget-time"));const auto H=Hero->GetActorLocation(),P=Pet->GetActorLocation(),R=Recruit->GetActorLocation();const double Dt=World->GetTimeSeconds()-LastTime;
        if(!Motion(Hero,LastH,VelocityH,Dt) || !Motion(Pet.Get(),LastP,VelocityP,Dt) || !Motion(Recruit.Get(),LastR,VelocityR,Dt))return End(false,TEXT("motion"));HeroTravel+=FVector::Dist(H,LastH);PetTravel+=FVector::Dist(P,LastP);RecruitTravel+=FVector::Dist(R,LastR);LastH=H;LastP=P;LastR=R;VelocityH=Hero->GetVelocity();VelocityP=Pet->GetVelocity();VelocityR=Recruit->GetVelocity();LastTime=World->GetTimeSeconds();
        int32 Regions=0;for(TActorIterator<AWildEncounter> It(World);It;++It){++Regions;if(!It->IsActorTickEnabled())return End(false,TEXT("region"));}if(Regions!=3)return End(false,TEXT("regions"));for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Health>0 && (!It->IsActorTickEnabled() || !It->GetActorEnableCollision() || It->GetCharacterMovement()->MovementMode==MOVE_None))return End(false,TEXT("ecology"));
        if(World->GetTimeSeconds()>=NextDiagnostic){NextDiagnostic=World->GetTimeSeconds()+5;UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RECRUIT_STATE phase=%d hero=%s old=%s new=%s hp=%.1f/%.1f/%.1f attempts=%d produced=%d"),Phase,*H.ToString(),*P.ToString(),*R.ToString(),Hero->Health,Pet->Health,Recruit->Health,Attempts,Produced);}
        if(FPlatformTime::Seconds()<ShotUntil)return true;
        if(Phase==-1){if(World->GetTimeSeconds()-At<2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;Phase=Read?6:RestOrdered?1:0;At=World->GetTimeSeconds();return true;}
        if(Phase==0){if(Photo(TEXT("Departure"),P))return true;Face(FVector(-200,-400,245));if(AHomeModule::TraceDoor(Hero)!=Door.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("door-sight"));return true;}if(!CFInputQA::Press(Hero,EKeys::E) || Door->DoorState!=2 || !CFInputQA::Press(Hero,EKeys::T) || Pet->Activity!=ECreatureActivity::Rest || !Pet->WorkSite)return End(false,TEXT("open-rest"));Bench=Pet->WorkSite;RestOrdered=true;Phase=1;At=World->GetTimeSeconds();return true;}
        if(Phase==1){if(World->GetTimeSeconds()-At<2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;if(!Plan(H.X> -350 && H.Y> -250?FVector(0,350,40):FVector(-650,-500,40),20))return End(false,TEXT("approach-path"));return true;}
        if(Phase==20 || Phase==21){const int32 M=Move();if(M<0)return End(false,TEXT("base-bypass-walk"));if(!M)return true;if(Phase==20){if(!Plan(FVector(-900,350,40),21))return End(false,TEXT("base-bypass-path"));}else if(!Plan(R+(H-R).GetSafeNormal2D()*230,2))return End(false,TEXT("approach-path"));return true;}
        if(Phase==2 || Phase==7){if(Phase==2 && Recruit->GetDistanceTo(Hero)<=250){Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::LeftShift);Phase=3;At=World->GetTimeSeconds();return true;}const int32 M=Move();if(M<0)return End(false,TEXT("walk"));if(!M)return true;Phase=Phase==2?3:8;At=World->GetTimeSeconds();return true;}
        if(Phase==3){Face(R);if(Recruit->GetDistanceTo(Hero)>290){if(World->GetTimeSeconds()-At>15)return End(false,TEXT("approach-moving"));if(!Plan(R+(H-R).GetSafeNormal2D()*230,2))return End(false,TEXT("chase-path"));return true;}if(Hero->TraceCreature()!=Recruit.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("combat-sight"));return true;}if(World->GetTimeSeconds()<NextAction)return true;if(Recruit->Health<=35){if(Photo(TEXT("Weakened"),R))return true;if(!CFInputQA::Press(Hero,EKeys::Zero) || !Hero->CaptureComponent->UsesResonance())return End(false,TEXT("device-select"));Phase=4;return true;}if(Swings>=4 || !CFInputQA::Action(Hero,TEXT("Attack"),EKeys::LeftMouseButton))return End(false,TEXT("melee-input"));++Swings;NextAction=World->GetTimeSeconds()+1.0;return true;}
        if(Phase==4){Face(R);if(Hero->TraceCreature()!=Recruit.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("capture-sight"));return true;}if(World->GetTimeSeconds()<NextAction)return true;if(Attempts>=2)return End(false,TEXT("earned-devices-exhausted"));const int32 Before=Hero->Inventory->Count(EFrontierItem::ResonanceDevice);const float Chance=Hero->CaptureComponent->CalculateCaptureChance(Recruit.Get());if(!CFInputQA::Action(Hero,TEXT("Capture"),EKeys::RightMouseButton) || Hero->Inventory->Count(EFrontierItem::ResonanceDevice)!=Before-1)return End(false,TEXT("capture-input"));++Attempts;NextAction=World->GetTimeSeconds()+.9;Caught=Recruit->Tamer==Hero->GetController();UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RECRUIT_THROW attempt=%d chance=%.4f success=%d remaining=%d"),Attempts,Chance,Caught,Hero->Inventory->Count(EFrontierItem::ResonanceDevice));if(!Caught)return true;if(!Hero->SaveProgress(TEXT("StarterRecruitMid")))return End(false,TEXT("capture-save"));Photo(TEXT("Captured"),R);UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RECRUIT_MID PASS caught=1 attempts=%d oldRest=1"),Attempts);Phase=6;At=World->GetTimeSeconds();return true;}
        if(Phase==6){Face(R);if(Hero->TraceCreature()!=Recruit.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("select-sight"));return true;}if(!CFInputQA::Press(Hero,EKeys::L) || !Hero->IsCommandTarget(Recruit.Get()) || Hero->IsCommandTarget(Pet.Get()) || !CFInputQA::Press(Hero,EKeys::F))return End(false,TEXT("single-select"));if(!Plan(FVector(-650,420,40),7))return End(false,TEXT("work-return"));return true;}
        if(Phase==8){Face(Salvage->GetActorLocation());if(!CFInputQA::Press(Hero,EKeys::E) || Recruit->GetFieldResource()!=Salvage.Get() || Pet->Activity!=ECreatureActivity::Rest || Pet->WorkSite!=Bench.Get())return End(false,TEXT("split-order"));Photo(TEXT("Assigned"),R);UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RECRUIT_ASSIGN PASS single=1 craftingSpecialty=1 oldRestPreserved=1"));Phase=9;At=World->GetTimeSeconds();return true;}
        if(Phase==9){if(Produced==0){if(World->GetTimeSeconds()-At>25)return End(false,TEXT("production-time"));return true;}if(Recruit->Experience!=1 || !CFInputQA::Press(Hero,EKeys::F) || Recruit->GetFieldResource() || Pet->Activity!=ECreatureActivity::Rest)return End(false,TEXT("single-stop"));Photo(TEXT("Produced"),R);Phase=10;At=World->GetTimeSeconds();return true;}
        if(Phase==10){if(Pet->Health<Pet->GetMaxHealth()){if(World->GetTimeSeconds()-At>30)return End(false,TEXT("old-rest-time"));return true;}const auto HP=Hero->Health,PP=Pet->Health,RP=Recruit->Health;const auto HeroBefore=H,PetBefore=P,RecruitBefore=R;if(!Hero->SaveProgress(TEXT("StarterRecruitDone")) || !Hero->LoadProgress(TEXT("StarterRecruitDone")))return End(false,TEXT("save"));Find();FindRecruit();Bench=Pet->WorkSite;if(!Budget() || Hero->Health!=HP || Pet->Health!=PP || Recruit->Health!=RP || !Hero->GetActorLocation().Equals(HeroBefore,.01) || !Pet->GetActorLocation().Equals(PetBefore,.01) || !Recruit->GetActorLocation().Equals(RecruitBefore,.01))return End(false,TEXT("restore"));UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RECRUIT_LIFE PASS owned=2 paidProduction=1 newXP=1 oldXP=39 oldRest=1 identities=1"));return End(true,TEXT("complete"));}
        return true;
    }
};
}
void RunStarterRecruitScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto S=MakeShared<FStarterRecruit>();S->World=World;S->Hero=Hero;S->Read=Read;S->Caught=Read;S->RestOrdered=Read;S->At=World->GetTimeSeconds();S->Deadline=FPlatformTime::Seconds()+150;
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("StarterRecruitInput"),0));if(!Source || Source->FormatVersion!=38 || Source->Facilities.Num()!=1 || Source->HomeModules.Num()!=7 || !Source->Research.bCrystalStudy || !Hero->LoadProgress(TEXT("StarterRecruitInput"))){S->End(false,TEXT("source"));return;}
    if(Hero->Health<=0){const auto Before=Hero->Inventory->GetStacks();if(!CFInputQA::Press(Hero,EKeys::R) || Hero->Health!=100 || !Hero->Inventory->GetStacks().OrderIndependentCompareEqual(Before)){S->End(false,TEXT("downed-respawn"));return;}S->Recovered=true;S->RestOrdered=true;UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RECRUIT_RESPAWN PASS originalInput=1 inventoryPreserved=1"));}
    S->Original=Source->HomeModules;S->Garden=Source->Facilities[0];S->Bag=Hero->Inventory->GetStacks();S->Attempts=Read?2-Hero->Inventory->Count(EFrontierItem::ResonanceDevice):0;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It){if(It->Tamer==Hero->GetController() && It->Definition && It->Definition->Form==ECreatureForm::Leaf){S->PetId=It->PersistentId;S->Nickname=It->Nickname;}if(It->EncounterId.IsNone() && It->Definition && It->Definition->Form==ECreatureForm::Ember)S->RecruitId=It->PersistentId;}S->Find();S->FindRecruit();if(Read && S->Pet.IsValid())S->Bench=S->Pet->WorkSite;
    if(S->Pet.IsValid() && S->Door.IsValid() && S->Door->DoorState==2 && S->Pet->Activity==ECreatureActivity::Rest){S->RestOrdered=true;S->Bench=S->Pet->WorkSite;}
    for(const auto& N:Source->ResourceNodes)S->Resources.Add(N.Id,N.Remaining);if(S->Salvage.IsValid())S->SalvageBefore=S->Salvage->Remaining;
    if(!S->Budget() || Hero->Inventory->Count(EFrontierItem::Material)!=0 || (!Read && Hero->Inventory->Count(EFrontierItem::ResonanceDevice)!=2) || (!S->Recovered && !Hero->GetActorTransform().Equals(Source->PlayerTransform,.01))){S->End(false,TEXT("source-budget"));return;}
    UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RECRUIT_SOURCE target=%s level=%d health=%.1f specialty=%d"),*S->RecruitId.ToString(),S->Recruit->Level,S->Recruit->Health,S->Recruit->Definition->WorkSpecialties.Contains(EWorkSpecialty::Crafting));
    S->LastH=Hero->GetActorLocation();S->LastP=S->Pet->GetActorLocation();S->LastR=S->Recruit->GetActorLocation();S->VelocityH=Hero->GetVelocity();S->VelocityP=S->Pet->GetVelocity();S->VelocityR=S->Recruit->GetVelocity();S->LastTime=World->GetTimeSeconds();S->Input=NewObject<UHomeMovementProbe>(Hero);S->Input->RegisterComponent();S->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFStarterRecruitPreview"));if(S->Preview){S->Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("StarterRecruitPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*S->Root,true);UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RECRUIT_PREVIEW_ROOT %s"),*S->Root);}FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){return S->Tick();}));
}

namespace
{
struct FStarterHousehold : FStarterRecruit
{
    TWeakObjectPtr<AWorkStation> Farm,RestSite;bool RestSent=false,Deposited=false,Withdrew=false,ParallelMid=false;float PlayerHealth=0;
    void Bind(){Find();FindRecruit();Farm.Reset();for(TActorIterator<AWorkStation> It(World);It;++It)if(It->PersistentId==Garden.Id)Farm=*It;}
    bool End(bool Pass,const TCHAR* Why){if(Input.IsValid())Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::LeftShift);if(!Pass)Hero->SaveProgress(TEXT("StarterHouseholdBlocked"));UE_LOG(LogTemp,Display,TEXT("CF_STARTER_HOUSEHOLD %s reason=%s phase=%d hp=%.1f/%.1f/%.1f food=%d seeds=%d xp=%d/%d feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Why,Phase,Hero->Health,Pet.IsValid()?Pet->Health:-1,Recruit.IsValid()?Recruit->Health:-1,Hero->Inventory->Count(EFrontierItem::Food),Hero->Inventory->Count(EFrontierItem::Seeds),Pet.IsValid()?Pet->Experience:-1,Recruit.IsValid()?Recruit->Experience:-1,*Hero->Feedback);FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;}
    bool Budget()const
    {
        if(!Pet.IsValid() || !Recruit.IsValid() || !Farm.IsValid() || Hero->Health!=PlayerHealth || Hero->Health<=0 || Pet->Health<=0 || Recruit->Health<=0 || Pet->Level!=2 || Pet->BondPoints!=6 || Pet->Nickname!=Nickname || Recruit->Level!=1 || Recruit->Experience!=1 || Recruit->BondPoints!=0 || Hero->GetCompanionRoster().Num()!=2)return false;
        const int32 Harvest=Pet->Experience-39;if(Harvest<0 || Harvest>1 || (RestSent && (Recruit->Activity!=ECreatureActivity::Rest || Recruit->WorkSite!=RestSite.Get())))return false;
        auto Expected=Bag;Expected.Add(EFrontierItem::Food,2+(Withdrew?1:0));Expected.Add(EFrontierItem::Seeds,2-(Deposited?1:0));for(int32 I=0;I<static_cast<int32>(EFrontierItem::Count);++I)if(Hero->Inventory->Count(static_cast<EFrontierItem>(I))!=Expected.FindRef(static_cast<EFrontierItem>(I)))return false;
        if(Farm->Crop->Seeds+(Farm->Crop->bPlanted?1:0)+Harvest!=(Deposited?1:0) || Farm->StoredFood!=Harvest-(Withdrew?1:0) || Farm->Crop->GrowthWork<0 || Farm->Crop->GrowthWork>6)return false;
        int32 Farms=0;for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt)++Farms;if(Farms!=1 || !Farm->GetActorTransform().Equals(Garden.Transform,.01))return false;
        const auto Layout=HomeLayout::Collect(World);if(Layout.Num()!=Original.Num())return false;for(const auto& R:Original){const auto* V=Layout.FindByPredicate([&](const FHomeModuleRecord& X){return X.Id==R.Id;});if(!V || V->Definition!=R.Definition || !V->Transform.Equals(R.Transform,.01) || V->PaidMaterials!=R.PaidMaterials || V->DoorState!=R.DoorState || !V->StoredItems.OrderIndependentCompareEqual(R.StoredItems))return false;}
        for(TActorIterator<AResourceNode> It(World);It;++It)if(const auto* Before=Resources.Find(It->PersistentId))if(It->Remaining!=*Before)return false;
        return Pet->Cargo->Count()==0 && Recruit->Cargo->Count()==0 && Hero->Research->Has(EFrontierResearch::CrystalStudy) && Hero->GetObjectives().Claimed==5;
    }
    bool Tick()
    {
        if(!Budget() || FPlatformTime::Seconds()>Deadline)return End(false,TEXT("budget-time"));const auto H=Hero->GetActorLocation(),P=Pet->GetActorLocation(),R=Recruit->GetActorLocation();const double Dt=World->GetTimeSeconds()-LastTime;
        if(!Motion(Hero,LastH,VelocityH,Dt) || !Motion(Pet.Get(),LastP,VelocityP,Dt) || !Motion(Recruit.Get(),LastR,VelocityR,Dt))return End(false,TEXT("motion"));HeroTravel+=FVector::Dist(H,LastH);PetTravel+=FVector::Dist(P,LastP);RecruitTravel+=FVector::Dist(R,LastR);LastH=H;LastP=P;LastR=R;VelocityH=Hero->GetVelocity();VelocityP=Pet->GetVelocity();VelocityR=Recruit->GetVelocity();LastTime=World->GetTimeSeconds();
        int32 Regions=0;for(TActorIterator<AWildEncounter> It(World);It;++It){++Regions;if(!It->IsActorTickEnabled())return End(false,TEXT("region"));}if(Regions!=3)return End(false,TEXT("regions"));for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Health>0 && (!It->IsActorTickEnabled() || !It->GetActorEnableCollision() || It->GetCharacterMovement()->MovementMode==MOVE_None))return End(false,TEXT("ecology"));
        if(World->GetTimeSeconds()>=NextDiagnostic){NextDiagnostic=World->GetTimeSeconds()+5;UE_LOG(LogTemp,Display,TEXT("CF_STARTER_HOUSEHOLD_STATE phase=%d hero=%s old=%s new=%s newHP=%.2f planted=%d growth=%.2f stock=%d"),Phase,*H.ToString(),*P.ToString(),*R.ToString(),Recruit->Health,Farm->Crop->bPlanted,Farm->Crop->GrowthWork,Farm->StoredFood);}
        if(Preview && Phase==8 && PetTravel>20 && FParse::Param(FCommandLine::Get(),TEXT("CFFarmYieldTrace")) && !Photos.Contains(TEXT("Yielding")))
        {Photos.Add(TEXT("Yielding"));FScreenshotRequest::RequestScreenshot(Root/TEXT("Yielding.png"),false,false);}
        if(!ParallelMid && RestSent && Deposited && Farm->Crop->bPlanted && Farm->Crop->GrowthWork>0 && Recruit->Health>10 && Recruit->Health<Recruit->GetMaxHealth())
        {if(!Hero->SaveProgress(TEXT("StarterHouseholdMid")))return End(false,TEXT("parallel-save"));ParallelMid=true;Photo(TEXT("Parallel"),Farm->GetActorLocation());UE_LOG(LogTemp,Display,TEXT("CF_STARTER_HOUSEHOLD_MID PASS farmerWorking=1 newResting=1 newHP=%.2f growth=%.2f"),Recruit->Health,Farm->Crop->GrowthWork);}
        if(FPlatformTime::Seconds()<ShotUntil)return true;
        if(Phase==-1){if(World->GetTimeSeconds()-At<2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;Phase=Read?6:0;At=World->GetTimeSeconds();return true;}
        if(Phase==0){if(Photo(TEXT("Injured"),R))return true;Face(R);if(Hero->TraceCreature()!=Recruit.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("rest-select-sight"));return true;}if(!CFInputQA::Press(Hero,EKeys::L) || !Hero->IsCommandTarget(Recruit.Get()) || Hero->IsCommandTarget(Pet.Get()) || !CFInputQA::Press(Hero,EKeys::T) || Recruit->Activity!=ECreatureActivity::Rest || !Recruit->WorkSite)return End(false,TEXT("rest-single"));RestSite=Recruit->WorkSite;RestSent=true;UE_LOG(LogTemp,Display,TEXT("CF_STARTER_HOUSEHOLD_REST PASS single=1 originalHealth=%.1f"),Recruit->Health);if(!Plan(FVector(-500,150,40),1))return End(false,TEXT("old-observer-path"));return true;}
        if(Phase==1 || Phase==3){const int32 M=Move();if(M<0)return End(false,TEXT("walk"));if(!M)return true;Phase=Phase==1?2:4;At=World->GetTimeSeconds();return true;}
        if(Phase==2){Face(P);if(Hero->TraceCreature()!=Pet.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("farmer-select-sight"));return true;}if(!CFInputQA::Press(Hero,EKeys::L) || !Hero->IsCommandTarget(Pet.Get()) || Hero->IsCommandTarget(Recruit.Get()) || !CFInputQA::Press(Hero,EKeys::F) || Recruit->Activity!=ECreatureActivity::Rest)return End(false,TEXT("farmer-follow"));if(!Plan(Farm->GetActorLocation()+FVector(-190,-140,40),3))return End(false,TEXT("farm-path"));return true;}
        if(Phase==4){Face(Farm->GetActorLocation()+FVector(0,0,30));if(Hero->TraceFacility()!=Farm.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("farm-sight"));return true;}if(!CFInputQA::Press(Hero,EKeys::F2) || Hero->Inventory->Count(EFrontierItem::Seeds)!=1 || Farm->Crop->Seeds!=1)return End(false,TEXT("seed-input"));Deposited=true;if(!CFInputQA::Press(Hero,EKeys::E) || Pet->WorkSite!=Farm.Get() || Pet->Activity!=ECreatureActivity::Work || Recruit->Activity!=ECreatureActivity::Rest || Recruit->WorkSite!=RestSite.Get())return End(false,TEXT("split-assign"));Photo(TEXT("Assigned"),Farm->GetActorLocation());Phase=6;At=World->GetTimeSeconds();return true;}
        if(Phase==6){if(Pet->WorkSite!=Farm.Get() || Pet->Activity!=ECreatureActivity::Work)return End(false,TEXT("farm-task-lost"));if(Farm->StoredFood<1){if(World->GetTimeSeconds()-At>35)return End(false,TEXT("crop-time"));return true;}if(!ParallelMid || Pet->Experience!=40)return End(false,TEXT("no-overlap"));Face(Farm->GetActorLocation()+FVector(0,0,30));if(!CFInputQA::Press(Hero,EKeys::O) || Farm->StoredFood!=0 || Hero->Inventory->Count(EFrontierItem::Food)!=3)return End(false,TEXT("withdraw"));Withdrew=true;Photo(TEXT("Harvested"),Farm->GetActorLocation());UE_LOG(LogTemp,Display,TEXT("CF_STARTER_HOUSEHOLD_FARM PASS seedPaid=1 produced=1 food=3 oldXP=40 newXP=1"));Phase=7;At=World->GetTimeSeconds();return true;}
        if(Phase==7){if(Recruit->Health<Recruit->GetMaxHealth() || Recruit->Stamina<99){if(World->GetTimeSeconds()-At>35)return End(false,TEXT("rest-time"));return true;}if(!Plan(FVector(-650,150,40),8))return End(false,TEXT("rest-observer-path"));return true;}
        if(Phase==8){const int32 M=Move();if(M<0)return End(false,TEXT("observer-walk"));if(!M)return true;Phase=9;return true;}
        if(Phase==9){if(Photo(TEXT("Recovered"),R))return true;const auto HP=Hero->Health,PP=Pet->Health,RP=Recruit->Health;const auto HB=H,PB=P,RB=R;if(!Hero->SaveProgress(TEXT("StarterHouseholdDone")) || !Hero->LoadProgress(TEXT("StarterHouseholdDone")))return End(false,TEXT("save"));Bind();RestSite=Recruit->WorkSite;if(!Budget() || Pet->WorkSite!=Farm.Get() || Pet->Activity!=ECreatureActivity::Work || Hero->Health!=HP || Pet->Health!=PP || Recruit->Health!=RP || !Hero->GetActorLocation().Equals(HB,.01) || !Pet->GetActorLocation().Equals(PB,.01) || !Recruit->GetActorLocation().Equals(RB,.01))return End(false,TEXT("restore"));UE_LOG(LogTemp,Display,TEXT("CF_STARTER_HOUSEHOLD_LIFE PASS owned=2 food=3 seeds=1 oldXP=40 newXP=1 newHealth=80 farmTask=1 restTask=1 identity=1"));return End(true,TEXT("complete"));}
        return true;
    }
};
}
void RunStarterHouseholdScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto S=MakeShared<FStarterHousehold>();S->World=World;S->Hero=Hero;S->Read=Read;S->RestSent=Read;S->Deposited=Read;S->ParallelMid=Read;S->At=World->GetTimeSeconds();S->Deadline=FPlatformTime::Seconds()+130;
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("StarterHouseholdInput"),0));if(!Source || Source->FormatVersion!=38 || Source->Facilities.Num()!=1 || Source->HomeModules.Num()!=7 || !Hero->LoadProgress(TEXT("StarterHouseholdInput"))){S->End(false,TEXT("source"));return;}
    S->Original=Source->HomeModules;S->Garden=Source->Facilities[0];S->Bag=Hero->Inventory->GetStacks();S->PlayerHealth=Hero->Health;for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController() && It->Definition){if(It->Definition->Form==ECreatureForm::Leaf){S->PetId=It->PersistentId;S->Nickname=It->Nickname;}if(It->Definition->Form==ECreatureForm::Ember)S->RecruitId=It->PersistentId;}S->Bind();if(Read && S->Recruit.IsValid())S->RestSite=S->Recruit->WorkSite;for(const auto& N:Source->ResourceNodes)S->Resources.Add(N.Id,N.Remaining);
    if(!S->Budget() || (!Read && (S->Recruit->Health!=10 || S->Pet->Experience!=39 || Hero->Inventory->Count(EFrontierItem::Food)!=2 || Hero->Inventory->Count(EFrontierItem::Seeds)!=2)) || !Hero->GetActorTransform().Equals(Source->PlayerTransform,.01)){S->End(false,TEXT("source-budget"));return;}
    S->LastH=Hero->GetActorLocation();S->LastP=S->Pet->GetActorLocation();S->LastR=S->Recruit->GetActorLocation();S->VelocityH=Hero->GetVelocity();S->VelocityP=S->Pet->GetVelocity();S->VelocityR=S->Recruit->GetVelocity();S->LastTime=World->GetTimeSeconds();S->Input=NewObject<UHomeMovementProbe>(Hero);S->Input->RegisterComponent();S->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFStarterHouseholdPreview"));if(S->Preview){S->Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("StarterHouseholdPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*S->Root,true);UE_LOG(LogTemp,Display,TEXT("CF_STARTER_HOUSEHOLD_PREVIEW_ROOT %s"),*S->Root);}FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){return S->Tick();}));
}

namespace
{
struct FStarterRestAim : FStarterHousehold
{
    AWorkStation* Nearest()const{AWorkStation* Closest=nullptr;float Best=800;for(TActorIterator<AWorkStation> It(World);It;++It)if(!It->IsActorBeingDestroyed() && Hero->GetDistanceTo(*It)<Best){Best=Hero->GetDistanceTo(*It);Closest=*It;}return Closest;}
    AWorkStation* Aimed()const{FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(RestAimQA),false,Hero);const auto Eye=Hero->FirstPersonCamera->GetComponentLocation();World->LineTraceSingleByChannel(Hit,Eye,Eye+Hero->FirstPersonCamera->GetForwardVector()*1300,ECC_Visibility,Q);return Cast<AWorkStation>(Hit.GetActor());}
    bool End(bool Pass,const TCHAR* Why){if(Input.IsValid())Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::LeftShift);if(!Pass)Hero->SaveProgress(TEXT("StarterRestAimBlocked"));UE_LOG(LogTemp,Display,TEXT("CF_STARTER_REST_AIM %s reason=%s phase=%d travel=%.1f/%.1f/%.1f hero=%s new=%s feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Why,Phase,HeroTravel,PetTravel,RecruitTravel,*Hero->GetActorLocation().ToString(),Recruit.IsValid()?*Recruit->GetActorLocation().ToString():TEXT("none"),*Hero->Feedback);FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;}
    bool Tick()
    {
        if(!Budget() || Pet->WorkSite!=Farm.Get() || (Phase!=7 && Pet->Activity!=ECreatureActivity::Work) || FPlatformTime::Seconds()>Deadline)return End(false,TEXT("budget-task-time"));const auto H=Hero->GetActorLocation(),P=Pet->GetActorLocation(),R=Recruit->GetActorLocation();const double Dt=World->GetTimeSeconds()-LastTime;
        if(!Motion(Hero,LastH,VelocityH,Dt) || !Motion(Pet.Get(),LastP,VelocityP,Dt) || !Motion(Recruit.Get(),LastR,VelocityR,Dt))return End(false,TEXT("motion"));HeroTravel+=FVector::Dist(H,LastH);PetTravel+=FVector::Dist(P,LastP);RecruitTravel+=FVector::Dist(R,LastR);LastH=H;LastP=P;LastR=R;VelocityH=Hero->GetVelocity();VelocityP=Pet->GetVelocity();VelocityR=Recruit->GetVelocity();LastTime=World->GetTimeSeconds();
        int32 Regions=0;for(TActorIterator<AWildEncounter> It(World);It;++It){++Regions;if(!It->IsActorTickEnabled())return End(false,TEXT("region"));}if(Regions!=3)return End(false,TEXT("regions"));for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Health>0 && (!It->IsActorTickEnabled() || !It->GetActorEnableCollision() || It->GetCharacterMovement()->MovementMode==MOVE_None))return End(false,TEXT("ecology"));
        if(FPlatformTime::Seconds()<ShotUntil)return true;
        if(Phase==-1){if(World->GetTimeSeconds()-At<2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;if(Read){Phase=6;At=World->GetTimeSeconds();return true;}if(!Plan(FVector(-1100,200,40),0))return End(false,TEXT("far-observer-path"));return true;}
        if(Phase==0 || Phase==4){const int32 M=Move();if(M<0)return End(false,TEXT("walk"));if(!M)return true;Phase=Phase==0?1:5;At=World->GetTimeSeconds();return true;}
        if(Phase==1){Face(R);if(Hero->TraceCreature()!=Recruit.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("select-sight"));return true;}if(!CFInputQA::Press(Hero,EKeys::L) || !Hero->IsCommandTarget(Recruit.Get()) || Hero->IsCommandTarget(Pet.Get()))return End(false,TEXT("select"));Face(Farm->GetActorLocation()+FVector(0,0,30));if(Aimed()!=Farm.Get() || Hero->GetDistanceTo(Farm.Get())<=800)return End(false,TEXT("far-fixture"));const auto* Before=Recruit->WorkSite.Get();if(!CFInputQA::Press(Hero,EKeys::T) || Recruit->WorkSite!=Before || Recruit->Activity!=ECreatureActivity::Rest || !Hero->Feedback.Contains(TEXT("超过8米")))return End(false,TEXT("far-reject"));UE_LOG(LogTemp,Display,TEXT("CF_STARTER_REST_AIM_FAR PASS rejected=1 oldTaskPreserved=1"));Photo(TEXT("Rejected"),Farm->GetActorLocation());Phase=2;return true;}
        if(Phase==2){Face(Hero->FirstPersonCamera->GetComponentLocation()+FVector(0,0,1000));const auto* Near=Nearest();if(!Near || Aimed() || !CFInputQA::Press(Hero,EKeys::T) || Recruit->WorkSite!=Near || !Hero->Feedback.Contains(TEXT("就近")))return End(false,TEXT("fallback"));UE_LOG(LogTemp,Display,TEXT("CF_STARTER_REST_AIM_FALLBACK PASS nearest=1"));Photo(TEXT("Nearest"),H+FVector(0,0,1000));Phase=3;return true;}
        if(Phase==3){if(!Plan(FVector(-1100,-100,40),4))return End(false,TEXT("near-observer-path"));return true;}
        if(Phase==5){Face(Farm->GetActorLocation()+FVector(0,0,30));if(Aimed()!=Farm.Get() || Hero->GetDistanceTo(Farm.Get())>800 || Nearest()==Farm.Get())return End(false,TEXT("aim-fixture"));if(!CFInputQA::Press(Hero,EKeys::T) || Recruit->WorkSite!=Farm.Get() || Recruit->Activity!=ECreatureActivity::Rest || !Hero->Feedback.Contains(TEXT("瞄准")))return End(false,TEXT("aimed"));RestSite=Farm;RestSent=true;Photo(TEXT("Aimed"),Farm->GetActorLocation());Phase=8;return true;}
        if(Phase==8){if(!Hero->SaveProgress(TEXT("StarterRestAimMid")))return End(false,TEXT("mid-save"));UE_LOG(LogTemp,Display,TEXT("CF_STARTER_REST_AIM_MID PASS aimed=1 nearerIgnored=1 farmerPreserved=1"));Phase=6;At=World->GetTimeSeconds();return true;}
        if(Phase==6){if(Recruit->GetDistanceTo(Farm.Get())>220){if(World->GetTimeSeconds()-At>25)return End(false,TEXT("arrival-time"));return true;}if(Photo(TEXT("Arrived"),R))return true;Face(P);if(Hero->TraceCreature()!=Pet.Get()){if(World->GetTimeSeconds()-At>30)return End(false,TEXT("farmer-sight"));return true;}if(!CFInputQA::Press(Hero,EKeys::L) || !Hero->IsCommandTarget(Pet.Get()) || Hero->IsCommandTarget(Recruit.Get()))return End(false,TEXT("select-farmer"));Face(H+FVector(0,0,1000));if(!CFInputQA::Press(Hero,EKeys::T) || Pet->WorkSite!=Farm.Get() || Pet->Activity!=ECreatureActivity::Rest || !Pet->bResumeWorkAfterRest || Recruit->WorkSite!=Farm.Get())return End(false,TEXT("resume-preserve"));UE_LOG(LogTemp,Display,TEXT("CF_STARTER_REST_AIM_RESUME PASS originalFarm=1 intent=1 otherRest=1"));Phase=7;At=World->GetTimeSeconds();return true;}
        if(Phase==7){if(Pet->Activity!=ECreatureActivity::Work){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("resume-time"));return true;}const auto HP=Hero->Health,PP=Pet->Health,RP=Recruit->Health;const auto HB=H,PB=P,RB=R;if(!Hero->SaveProgress(TEXT("StarterRestAimDone")) || !Hero->LoadProgress(TEXT("StarterRestAimDone")))return End(false,TEXT("save"));Bind();RestSite=Farm;if(!Budget() || Pet->WorkSite!=Farm.Get() || Recruit->WorkSite!=Farm.Get() || Hero->Health!=HP || Pet->Health!=PP || Recruit->Health!=RP || !Hero->GetActorLocation().Equals(HB,.01) || !Pet->GetActorLocation().Equals(PB,.01) || !Recruit->GetActorLocation().Equals(RB,.01))return End(false,TEXT("restore"));UE_LOG(LogTemp,Display,TEXT("CF_STARTER_REST_AIM_LIFE PASS aimedRest=1 farmWork=1 inventory=1 identities=1"));return End(true,TEXT("complete"));}
        return true;
    }
};
}
void RunStarterRestAimScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto S=MakeShared<FStarterRestAim>();S->World=World;S->Hero=Hero;S->Read=Read;S->Deposited=true;S->Withdrew=true;S->RestSent=Read;S->At=World->GetTimeSeconds();S->Deadline=FPlatformTime::Seconds()+110;
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("StarterRestAimInput"),0));if(!Source || Source->FormatVersion!=38 || Source->Facilities.Num()!=1 || Source->HomeModules.Num()!=7 || !Hero->LoadProgress(TEXT("StarterRestAimInput"))){S->End(false,TEXT("source"));return;}
    S->Original=Source->HomeModules;S->Garden=Source->Facilities[0];S->Bag=Hero->Inventory->GetStacks();S->PlayerHealth=Hero->Health;for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController() && It->Definition){if(It->Definition->Form==ECreatureForm::Leaf){S->PetId=It->PersistentId;S->Nickname=It->Nickname;}if(It->Definition->Form==ECreatureForm::Ember)S->RecruitId=It->PersistentId;}S->Bind();if(Read)S->RestSite=S->Farm;for(const auto& N:Source->ResourceNodes)S->Resources.Add(N.Id,N.Remaining);
    if(!S->Budget() || S->Pet->WorkSite!=S->Farm.Get() || S->Pet->Activity!=ECreatureActivity::Work || !Hero->GetActorTransform().Equals(Source->PlayerTransform,.01)){S->End(false,TEXT("source-budget"));return;}
    S->LastH=Hero->GetActorLocation();S->LastP=S->Pet->GetActorLocation();S->LastR=S->Recruit->GetActorLocation();S->VelocityH=Hero->GetVelocity();S->VelocityP=S->Pet->GetVelocity();S->VelocityR=S->Recruit->GetVelocity();S->LastTime=World->GetTimeSeconds();S->Input=NewObject<UHomeMovementProbe>(Hero);S->Input->RegisterComponent();S->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFStarterRestAimPreview"));if(S->Preview){S->Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("StarterRestAimPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*S->Root,true);UE_LOG(LogTemp,Display,TEXT("CF_STARTER_REST_AIM_PREVIEW_ROOT %s"),*S->Root);}FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){return S->Tick();}));
}

namespace
{
struct FStarterWorkshop : FStarterHousehold
{
    TWeakObjectPtr<AWorkStation> Outside,Indoor;FGuid IndoorId;int32 SalvageStart=0,RotationStep=0;bool Built=false,InsideOrdered=false;
    FTransform ExpectedPlacement=FTransform(FRotator(0,90,0),FVector(100,-400,5),FVector(2,1,.7));
    FTransform Placement()const{return ExpectedPlacement;}
    void BindWorkshop(){Bind();Outside.Reset();Indoor.Reset();for(TActorIterator<AWorkStation> It(World);It;++It){if(It->PersistentId==IndoorId && It->bPlayerBuilt)Indoor=*It;if(!It->bPlayerBuilt && It->RequiredSpecialty==EWorkSpecialty::Crafting)Outside=*It;}}
    bool End(bool Pass,const TCHAR* Why){if(Input.IsValid())Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::LeftShift);const auto Feedback=Hero->Feedback;if(!Pass)Hero->SaveProgress(TEXT("StarterWorkshopBlocked"));UE_LOG(LogTemp,Display,TEXT("CF_STARTER_WORKSHOP %s reason=%s phase=%d built=%d material=%d oldXP=%d newXP=%d hero=%s new=%s feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Why,Phase,Built,Hero->Inventory->Count(EFrontierItem::Material),Pet.IsValid()?Pet->Experience:-1,Recruit.IsValid()?Recruit->Experience:-1,*Hero->GetActorLocation().ToString(),Recruit.IsValid()?*Recruit->GetActorLocation().ToString():TEXT("none"),*Feedback);FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;}
    bool Budget()const
    {
        if(!Pet.IsValid() || !Recruit.IsValid() || !Farm.IsValid() || !Salvage.IsValid() || !Outside.IsValid() || Hero->Health!=PlayerHealth || Hero->Health<=0 || Pet->Health!=110 || Recruit->Health!=80 || Pet->Level!=2 || Pet->Experience!=40 || Pet->BondPoints!=6 || Pet->Nickname!=Nickname || Recruit->Level!=1 || Recruit->BondPoints!=0 || Hero->GetCompanionRoster().Num()!=2 || Pet->Activity!=ECreatureActivity::Work || Pet->WorkSite!=Farm.Get())return false;
        const int32 Taken=SalvageStart-Salvage->Remaining;if(Taken<0 || Taken>14 || Recruit->Experience!=1+Taken)return false;
        auto Expected=Bag;Expected.Add(EFrontierItem::Material,1+Taken-(Built?15:0));for(int32 I=0;I<static_cast<int32>(EFrontierItem::Count);++I)if(Hero->Inventory->Count(static_cast<EFrontierItem>(I))!=Expected.FindRef(static_cast<EFrontierItem>(I)))return false;
        if(Farm->StoredFood!=Garden.StoredFood || Farm->Crop->Seeds!=Garden.Seeds || Farm->Crop->bPlanted!=Garden.bPlanted || Farm->Crop->GrowthWork!=Garden.GrowthWork || !Farm->GetActorTransform().Equals(Garden.Transform,.01))return false;
        int32 Facilities=0;for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt){++Facilities;if(It->PersistentId!=Garden.Id && (!Built || It->PersistentId!=IndoorId))return false;}
        if(Facilities!=(Built?2:1) || (Built && (!Indoor.IsValid() || Indoor->RequiredSpecialty!=EWorkSpecialty::Crafting || !Indoor->GetActorTransform().Equals(Placement(),.01) || !HomeLayout::SupportsFacility(Indoor->GetActorTransform(),HomeLayout::Collect(World)))))return false;
        if(InsideOrdered && (Recruit->WorkSite!=Indoor.Get() || Recruit->Activity!=ECreatureActivity::Rest))return false;
        const auto Layout=HomeLayout::Collect(World);if(Layout.Num()!=Original.Num())return false;for(const auto& R:Original){const auto* V=Layout.FindByPredicate([&](const FHomeModuleRecord& X){return X.Id==R.Id;});if(!V || V->Definition!=R.Definition || !V->Transform.Equals(R.Transform,.01) || V->PaidMaterials!=R.PaidMaterials || !V->StoredItems.OrderIndependentCompareEqual(R.StoredItems))return false;}
        for(TActorIterator<AResourceNode> It(World);It;++It)if(const auto* Before=Resources.Find(It->PersistentId))if(It->PersistentId!=FGuid(0xCF170000,0,0,3) && It->Remaining!=*Before)return false;
        return Hero->Research->Has(EFrontierResearch::CrystalStudy) && Hero->GetObjectives().Claimed==5 && Pet->Cargo->Count()==0 && Recruit->Cargo->Count()==0;
    }
    bool VisibleIndoor()const{FCollisionQueryParams Q(SCENE_QUERY_STAT(WorkshopRestSight),false,Recruit.Get());FHitResult Hit;return !World->LineTraceSingleByChannel(Hit,Recruit->GetActorLocation(),Indoor->GetActorLocation()+FVector(0,0,30),ECC_Visibility,Q) || Hit.GetActor()==Indoor.Get();}
    bool Tick()
    {
        if(!Budget() || FPlatformTime::Seconds()>Deadline)return End(false,TEXT("budget-time"));const auto H=Hero->GetActorLocation(),P=Pet->GetActorLocation(),R=Recruit->GetActorLocation();const double Dt=World->GetTimeSeconds()-LastTime;
        if(!Motion(Hero,LastH,VelocityH,Dt) || !Motion(Pet.Get(),LastP,VelocityP,Dt) || !Motion(Recruit.Get(),LastR,VelocityR,Dt))return End(false,TEXT("motion"));HeroTravel+=FVector::Dist(H,LastH);PetTravel+=FVector::Dist(P,LastP);RecruitTravel+=FVector::Dist(R,LastR);LastH=H;LastP=P;LastR=R;VelocityH=Hero->GetVelocity();VelocityP=Pet->GetVelocity();VelocityR=Recruit->GetVelocity();LastTime=World->GetTimeSeconds();
        int32 Regions=0;for(TActorIterator<AWildEncounter> It(World);It;++It){++Regions;if(!It->IsActorTickEnabled())return End(false,TEXT("region"));}if(Regions!=3)return End(false,TEXT("regions"));for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Health>0 && (!It->IsActorTickEnabled() || !It->GetActorEnableCollision() || It->GetCharacterMovement()->MovementMode==MOVE_None))return End(false,TEXT("ecology"));
        if(World->GetTimeSeconds()>=NextDiagnostic){NextDiagnostic=World->GetTimeSeconds()+5;UE_LOG(LogTemp,Display,TEXT("CF_STARTER_WORKSHOP_STATE phase=%d hero=%s new=%s material=%d salvage=%d newXP=%d rest=%d"),Phase,*H.ToString(),*R.ToString(),Hero->Inventory->Count(EFrontierItem::Material),Salvage->Remaining,Recruit->Experience,static_cast<int32>(Recruit->Activity));}if(FPlatformTime::Seconds()<ShotUntil)return true;
        if(Phase==-1){if(World->GetTimeSeconds()-At<2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;Phase=Read?12:0;At=World->GetTimeSeconds();return true;}
        if(Phase==0){if(Photo(TEXT("Departure"),R))return true;Face(R);if(Hero->TraceCreature()!=Recruit.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("worker-select-sight"));return true;}if(!CFInputQA::Press(Hero,EKeys::L) || !Hero->IsCommandTarget(Recruit.Get()) || Hero->IsCommandTarget(Pet.Get()))return End(false,TEXT("worker-select"));Face(Outside->GetActorLocation()+FVector(0,0,20));if(!CFInputQA::Press(Hero,EKeys::E) || Recruit->WorkSite!=Outside.Get() || Recruit->Activity!=ECreatureActivity::Work)return End(false,TEXT("gather-order"));Phase=10;At=World->GetTimeSeconds();return true;}
        if(Phase==10){if(Hero->Inventory->Count(EFrontierItem::Material)<15){if(World->GetTimeSeconds()-At>60)return End(false,TEXT("gather-time"));return true;}if(!CFInputQA::Press(Hero,EKeys::F))return End(false,TEXT("stop-production"));Face(Outside->GetActorLocation()+FVector(0,0,20));if(!CFInputQA::Press(Hero,EKeys::T) || Recruit->WorkSite!=Outside.Get() || Recruit->Activity!=ECreatureActivity::Rest || !Hero->SaveProgress(TEXT("StarterWorkshopMid")))return End(false,TEXT("materials-save"));UE_LOG(LogTemp,Display,TEXT("CF_STARTER_WORKSHOP_MID PASS actualTaken=14 material=15 newXP=15 oldXP=40"));Photo(TEXT("Materials"),Salvage->GetActorLocation());Phase=1;return true;}
        if(Phase==12){if(!Plan(FVector(-1000,450,40),13))return End(false,TEXT("resume-selection-path"));return true;}
        if(Phase==13){const int32 M=Move();if(M<0)return End(false,TEXT("resume-selection-walk"));if(M)Phase=14;return true;}
        if(Phase==14){Face(R);if(Hero->TraceCreature()!=Recruit.Get() || !CFInputQA::Press(Hero,EKeys::L) || !Hero->IsCommandTarget(Recruit.Get()) || Hero->IsCommandTarget(Pet.Get()))return End(false,TEXT("resume-selection"));Phase=Built?6:1;At=World->GetTimeSeconds();return true;}
        if(Phase==1){if(!Plan(FVector(-450,-400,40),2))return End(false,TEXT("door-path"));return true;}
        if(Phase==2 || Phase==4 || Phase==7 || Phase==9){const int32 M=Move();if(M<0)return End(false,TEXT("walk"));if(!M)return true;Phase=Phase==2?3:Phase==4?5:Phase==7?8:11;At=World->GetTimeSeconds();return true;}
        if(Phase==3){if(Door->DoorState!=2){Face(FVector(-200,-400,245));if(AHomeModule::TraceDoor(Hero)!=Door.Get() || !CFInputQA::Press(Hero,EKeys::E) || Door->DoorState!=2)return End(false,TEXT("open-door"));}if(UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;if(!Plan(FVector(0,-550,60),4))return End(false,TEXT("place-observer-path"));return true;}
        if(Phase==5){Face(FVector(100,-400,-30));if(!Hero->Building->IsActive()){if(!CFInputQA::Press(Hero,EKeys::B))return End(false,TEXT("preview-open"));for(int32 I=0;Hero->Building->Selected!=EWorkSpecialty::Crafting && I<6;++I)if(!CFInputQA::Press(Hero,EKeys::V))return End(false,TEXT("cycle"));}if(FParse::Param(FCommandLine::Get(),TEXT("CFFacilityRotationTest")) && RotationStep<2){const auto Yaw=Hero->Building->GetPlacementTransform().Rotator().Yaw;if(RotationStep==0){if(!FMath::IsNearlyEqual(Yaw,90.f,.01f) || !CFInputQA::Press(Hero,EKeys::Nine) || !FMath::IsNearlyEqual(FMath::Abs(Hero->Building->GetPlacementTransform().Rotator().Yaw),180.f,.01f))return End(false,TEXT("rotate-quarter"));Face(FVector(200,-550,-30));RotationStep=1;return true;}if(!FMath::IsNearlyEqual(FMath::Abs(Yaw),180.f,.01f))return End(false,TEXT("rotate-view-reset"));for(int32 I=0;I<3;++I)if(!CFInputQA::Press(Hero,EKeys::Nine))return End(false,TEXT("rotate-cycle-input"));if(!FMath::IsNearlyEqual(Hero->Building->GetPlacementTransform().Rotator().Yaw,90.f,.01f))return End(false,TEXT("rotate-cycle"));for(int32 I=0;I<2;++I)if(!CFInputQA::Press(Hero,EKeys::Nine))return End(false,TEXT("rotate-final-input"));if(!FMath::IsNearlyEqual(Hero->Building->GetPlacementTransform().Rotator().Yaw,-90.f,.01f))return End(false,TEXT("rotate-final"));for(int32 I=0;I<6;++I)if(!CFInputQA::Press(Hero,EKeys::V))return End(false,TEXT("rotate-cycle-type"));if(Hero->Building->Selected!=EWorkSpecialty::Crafting || !CFInputQA::Press(Hero,EKeys::Eight) || !CFInputQA::Press(Hero,EKeys::Nine) || !CFInputQA::Press(Hero,EKeys::Eight) || !FMath::IsNearlyEqual(Hero->Building->GetPlacementTransform().Rotator().Yaw,-90.f,.01f) || !Budget())return End(false,TEXT("rotate-category"));RotationStep=2;UE_LOG(LogTemp,Display,TEXT("CF_FACILITY_ROTATION PASS quarter=1 viewHeld=1 cycle=1 finalYaw=270"));return true;}if(Hero->Building->Selected!=EWorkSpecialty::Crafting || !Hero->Building->GetHint().Contains(TEXT("可以建造")))return End(false,TEXT("preview-invalid"));if(Photo(TEXT("Preview"),FVector(100,-400,-30)))return true;if(!CFInputQA::Action(Hero,TEXT("Attack"),EKeys::LeftMouseButton))return End(false,TEXT("place-input"));for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt && It->RequiredSpecialty==EWorkSpecialty::Crafting){Indoor=*It;IndoorId=It->PersistentId;}if(!Indoor.IsValid() || Hero->Inventory->Count(EFrontierItem::Material)!=0 || !CFInputQA::Press(Hero,EKeys::B))return End(false,TEXT("place-paid"));Built=true;if(!Budget() || !Hero->SaveProgress(TEXT("StarterWorkshopBuilt")))return End(false,TEXT("built-save"));UE_LOG(LogTemp,Display,TEXT("CF_STARTER_WORKSHOP_BUILD PASS originalInput=1 paid=15 supported=1 facilities=2"));Photo(TEXT("Built"),Indoor->GetActorLocation());Phase=6;At=World->GetTimeSeconds();return true;}
        if(Phase==6){if(World->GetTimeSeconds()-At<2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;if(!Plan(FVector(-300,-400,40),7))return End(false,TEXT("rest-select-path"));return true;}
        if(Phase==8){if(!Hero->IsCommandTarget(Recruit.Get()) || Hero->IsCommandTarget(Pet.Get()))return End(false,TEXT("rest-selection-lost"));Face(Indoor->GetActorLocation()+FVector(0,0,25));if(Hero->TraceFacility()!=Indoor.Get() || !CFInputQA::Press(Hero,EKeys::T) || Recruit->WorkSite!=Indoor.Get())return End(false,TEXT("indoor-order"));InsideOrdered=true;FNavLocation Projected;const auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);if(Nav && Nav->ProjectPointToNavigation(Indoor->GetActorLocation(),Projected,FVector(250,250,300)))UE_LOG(LogTemp,Display,TEXT("CF_STARTER_WORKSHOP_NAV goal=%s projected=%s"),*Indoor->GetActorLocation().ToString(),*Projected.Location.ToString());if(!Plan(FVector(-100,-550,60),9))return End(false,TEXT("indoor-observer-path"));return true;}
        if(Phase==11){if(R.X< -150 || R.X>150 || FMath::Abs(R.Y+400)>160 || R.Z<50 || !VisibleIndoor()){if(World->GetTimeSeconds()-At>25)return End(false,TEXT("indoor-arrival"));return true;}if(Photo(TEXT("Indoor"),R))return true;const auto HP=Hero->Health,PP=Pet->Health,RP=Recruit->Health;const auto HB=H,PB=P,RB=R;if(!Hero->SaveProgress(TEXT("StarterWorkshopDone")) || !Hero->LoadProgress(TEXT("StarterWorkshopDone")))return End(false,TEXT("save"));BindWorkshop();if(!Budget() || Hero->Health!=HP || Pet->Health!=PP || Recruit->Health!=RP || !Hero->GetActorLocation().Equals(HB,.01) || !Pet->GetActorLocation().Equals(PB,.01) || !Recruit->GetActorLocation().Equals(RB,.01) || !VisibleIndoor())return End(false,TEXT("restore"));if(FParse::Param(FCommandLine::Get(),TEXT("CFFacilityRotationTest"))){Face(FVector(100,-400,-30));const float AutoYaw=FMath::UnwindDegrees(FMath::GridSnap(Hero->GetControlRotation().Yaw,90.f));if(!CFInputQA::Press(Hero,EKeys::B) || !FMath::IsNearlyEqual(Hero->Building->GetPlacementTransform().Rotator().Yaw,AutoYaw,.01f) || !CFInputQA::Press(Hero,EKeys::Nine) || !CFInputQA::Press(Hero,EKeys::B) || !CFInputQA::Press(Hero,EKeys::B) || !FMath::IsNearlyEqual(Hero->Building->GetPlacementTransform().Rotator().Yaw,AutoYaw,.01f) || !CFInputQA::Press(Hero,EKeys::B) || !Budget())return End(false,TEXT("rotate-reset"));UE_LOG(LogTemp,Display,TEXT("CF_FACILITY_ROTATION_RESET PASS cancel=1 load=1 category=1"));}UE_LOG(LogTemp,Display,TEXT("CF_STARTER_WORKSHOP_LIFE PASS facilities=2 modules=7 paid=15 material=0 newXP=15 inside=1 visible=1 identity=1"));return End(true,TEXT("complete"));}
        return true;
    }
};
}
namespace
{
struct FIndoorCraft : FStarterWorkshop
{
    TWeakObjectPtr<AResourceNode> Fiber;int32 MaterialStart=0,FiberStart=0;bool Crafted=false,Ordered=false;
    void BindCraft(){BindWorkshop();Fiber.Reset();for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,8))Fiber=*It;}
    bool End(bool Pass,const TCHAR* Why){if(Input.IsValid())Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::M);CFInputQA::Release(Hero,EKeys::LeftShift);const auto Feedback=Hero->Feedback;if(!Pass)Hero->SaveProgress(TEXT("IndoorCraftBlocked"));UE_LOG(LogTemp,Display,TEXT("CF_INDOOR_CRAFT %s reason=%s phase=%d material=%d fiber=%d hero=%s new=%s feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Why,Phase,Hero->Inventory->Count(EFrontierItem::Material),Hero->Inventory->Count(EFrontierItem::Fiber),*Hero->GetActorLocation().ToString(),Recruit.IsValid()?*Recruit->GetActorLocation().ToString():TEXT("none"),*Feedback);FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;}
    bool Budget()const
    {
        if(!Pet.IsValid() || !Recruit.IsValid() || !Farm.IsValid() || !Salvage.IsValid() || !Fiber.IsValid() || !Indoor.IsValid() || Hero->Health!=PlayerHealth || Hero->Health<=0 || Pet->Health!=110 || Recruit->Health!=80 || Pet->Level!=2 || Pet->Experience!=40 || Pet->BondPoints!=6 || Pet->Nickname!=Nickname || Recruit->Level!=1 || Recruit->BondPoints!=0 || Hero->GetCompanionRoster().Num()!=2 || Pet->Activity!=ECreatureActivity::Work || Pet->WorkSite!=Farm.Get())return false;
        const int32 M=MaterialStart-Salvage->Remaining,F=FiberStart-Fiber->Remaining;if(M<0 || M>2 || F<0 || F>2 || Recruit->Experience!=15+M)return false;
        auto Expected=Bag;Expected.Add(EFrontierItem::Material,M+(Crafted?3:0));Expected.Add(EFrontierItem::Fiber,F-(Crafted?2:0));for(int32 I=0;I<static_cast<int32>(EFrontierItem::Count);++I)if(Hero->Inventory->Count(static_cast<EFrontierItem>(I))!=Expected.FindRef(static_cast<EFrontierItem>(I)))return false;
        if(Farm->StoredFood!=Garden.StoredFood || Farm->Crop->Seeds!=Garden.Seeds || Farm->Crop->bPlanted!=Garden.bPlanted || Farm->Crop->GrowthWork!=Garden.GrowthWork || !Farm->GetActorTransform().Equals(Garden.Transform,.01))return false;
        int32 Facilities=0;for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt){++Facilities;if(It->PersistentId!=Garden.Id && It->PersistentId!=IndoorId)return false;}if(Facilities!=2 || !Indoor->GetActorTransform().Equals(Placement(),.01) || !HomeLayout::SupportsFacility(Indoor->GetActorTransform(),HomeLayout::Collect(World)))return false;
        if(Ordered && (Recruit->Activity!=ECreatureActivity::Rest || Recruit->WorkSite!=Indoor.Get()))return false;
        const auto Layout=HomeLayout::Collect(World);if(Layout.Num()!=Original.Num())return false;for(const auto& R:Original){const auto* V=Layout.FindByPredicate([&](const FHomeModuleRecord& X){return X.Id==R.Id;});if(!V || V->Definition!=R.Definition || !V->Transform.Equals(R.Transform,.01) || V->PaidMaterials!=R.PaidMaterials || !V->StoredItems.OrderIndependentCompareEqual(R.StoredItems))return false;}
        for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId!=Salvage->PersistentId && It->PersistentId!=Fiber->PersistentId)if(const auto* Before=Resources.Find(It->PersistentId))if(It->Remaining!=*Before)return false;
        return Hero->Research->Has(EFrontierResearch::CrystalStudy) && Hero->GetObjectives().Claimed==5 && Pet->Cargo->Count()==0 && Recruit->Cargo->Count()==0;
    }
    bool OnlyIndoor()const
    {
        int32 Count=0;AWorkStation* Found=nullptr;FCollisionQueryParams Q(SCENE_QUERY_STAT(IndoorCraftOnly),false,Hero);for(TActorIterator<APawn> It(World);It;++It)Q.AddIgnoredActor(*It);
        for(TActorIterator<AWorkStation> It(World);It;++It)if(!It->IsActorBeingDestroyed() && It->RequiredSpecialty!=EWorkSpecialty::Feeding && Hero->GetDistanceTo(*It)<=800){FHitResult Hit;if(!World->LineTraceSingleByChannel(Hit,Hero->FirstPersonCamera->GetComponentLocation(),It->GetActorLocation()+FVector(0,0,30),ECC_Visibility,Q) || Hit.GetActor()==*It){++Count;Found=*It;}}
        return Count==1 && Found==Indoor.Get() && Hero->IsNearWorkStation();
    }
    bool Tick()
    {
        if(!Budget() || FPlatformTime::Seconds()>Deadline)return End(false,TEXT("budget-time"));const auto H=Hero->GetActorLocation(),P=Pet->GetActorLocation(),R=Recruit->GetActorLocation();const double Dt=World->GetTimeSeconds()-LastTime;
        if(!Motion(Hero,LastH,VelocityH,Dt) || !Motion(Pet.Get(),LastP,VelocityP,Dt) || !Motion(Recruit.Get(),LastR,VelocityR,Dt))return End(false,TEXT("motion"));LastH=H;LastP=P;LastR=R;VelocityH=Hero->GetVelocity();VelocityP=Pet->GetVelocity();VelocityR=Recruit->GetVelocity();LastTime=World->GetTimeSeconds();
        int32 Regions=0;for(TActorIterator<AWildEncounter> It(World);It;++It){++Regions;if(!It->IsActorTickEnabled())return End(false,TEXT("region"));}if(Regions!=3)return End(false,TEXT("regions"));for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Health>0 && (!It->IsActorTickEnabled() || !It->GetActorEnableCollision() || It->GetCharacterMovement()->MovementMode==MOVE_None))return End(false,TEXT("ecology"));
        if(World->GetTimeSeconds()>=NextDiagnostic){NextDiagnostic=World->GetTimeSeconds()+5;UE_LOG(LogTemp,Display,TEXT("CF_INDOOR_CRAFT_STATE phase=%d hero=%s new=%s material=%d fiber=%d xp=%d"),Phase,*H.ToString(),*R.ToString(),Hero->Inventory->Count(EFrontierItem::Material),Hero->Inventory->Count(EFrontierItem::Fiber),Recruit->Experience);}if(FPlatformTime::Seconds()<ShotUntil)return true;
        if(Phase==-1){if(World->GetTimeSeconds()-At<2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;Phase=Read?12:0;At=World->GetTimeSeconds();return true;}
        if(Phase==0 || Phase==12){if(Phase==0 && Photo(TEXT("Departure"),R))return true;Face(R);if(Hero->TraceCreature()!=Recruit.Get()){if(World->GetTimeSeconds()-At>5)return End(false,TEXT("select-sight"));return true;}if(!CFInputQA::Press(Hero,EKeys::L) || !Hero->IsCommandTarget(Recruit.Get()) || Hero->IsCommandTarget(Pet.Get()))return End(false,TEXT("select"));if(Phase==12){Phase=6;return true;}if(!CFInputQA::Press(Hero,EKeys::F) || !Plan(FVector(-1000,450,40),1))return End(false,TEXT("outside-path"));return true;}
        if(Phase==1 || Phase==4 || Phase==7 || Phase==9){const int32 Result=Move();if(Result<0)return End(false,TEXT("walk"));if(!Result)return true;Phase=Phase==1?2:Phase==4?5:Phase==7?8:10;At=World->GetTimeSeconds();return true;}
        if(Phase==2){Face(Outside->GetActorLocation()+FVector(0,0,20));if(!CFInputQA::Press(Hero,EKeys::E) || Recruit->WorkSite!=Outside.Get() || Recruit->Activity!=ECreatureActivity::Work)return End(false,TEXT("produce-order"));Phase=3;At=World->GetTimeSeconds();return true;}
        if(Phase==3){if(Hero->Inventory->Count(EFrontierItem::Material)<2){if(World->GetTimeSeconds()-At>25)return End(false,TEXT("produce-time"));return true;}if(!CFInputQA::Press(Hero,EKeys::F))return End(false,TEXT("stop-produce"));if(Photo(TEXT("Materials"),R))return true;if(!Plan(FVector(-1500,1150,40),4))return End(false,TEXT("fiber-path"));return true;}
        if(Phase==5){Face(Fiber->GetActorLocation());if(Hero->Inventory->Count(EFrontierItem::Fiber)==0){if(Hero->TraceResource()!=Fiber.Get()){if(World->GetTimeSeconds()-At>4)return End(false,TEXT("fiber-sight"));return true;}if(!Hero->Gathering->IsGathering() && !CFInputQA::Press(Hero,EKeys::M))return End(false,TEXT("fiber-input"));}if(Hero->Inventory->Count(EFrontierItem::Fiber)<2){if(!Hero->Gathering->IsGathering() || World->GetTimeSeconds()-At>10)return End(false,TEXT("fiber-gather"));return true;}if(!CFInputQA::Release(Hero,EKeys::M) || !Hero->SaveProgress(TEXT("IndoorCraftMid")))return End(false,TEXT("mid-save"));UE_LOG(LogTemp,Display,TEXT("CF_INDOOR_CRAFT_MID PASS materialTaken=2 fiberTaken=2 material=2 fiber=2 newXP=17"));Photo(TEXT("Raw"),Fiber->GetActorLocation());Phase=6;return true;}
        if(Phase==6){if(!Plan(FVector(-450,-400,40),7))return End(false,TEXT("home-path"));return true;}
        if(Phase==8){if(Door->DoorState!=2){Face(FVector(-200,-400,245));if(AHomeModule::TraceDoor(Hero)!=Door.Get() || !CFInputQA::Press(Hero,EKeys::E) || Door->DoorState!=2)return End(false,TEXT("open"));}Face(Indoor->GetActorLocation()+FVector(0,0,25));if(Hero->TraceFacility()!=Indoor.Get() || !CFInputQA::Press(Hero,EKeys::T) || Recruit->WorkSite!=Indoor.Get())return End(false,TEXT("rest-order"));Ordered=true;if(!Plan(FVector(-100,-550,60),9))return End(false,TEXT("inside-path"));return true;}
        if(Phase==10){if(R.X< -150 || R.X>150 || FMath::Abs(R.Y+400)>160 || R.Z<50 || !VisibleIndoor()){if(World->GetTimeSeconds()-At>25)return End(false,TEXT("inside-arrival"));return true;}if(Door->DoorState==2){Face(FVector(-200,-400,245));if(AHomeModule::TraceDoor(Hero)!=Door.Get() || !CFInputQA::Press(Hero,EKeys::E) || Door->DoorState!=1)return End(false,TEXT("close"));}if(!OnlyIndoor())return End(false,TEXT("indoor-not-unique"));Face(Indoor->GetActorLocation());if(!Hero->IsBackpackOpen()){const auto* Catalog=UCraftingCatalog::Load();const int32 Index=Catalog?Catalog->Recipes.IndexOfByPredicate([](const FFrontierRecipe& X){return X.Id==TEXT("WeaveSupplies");}):INDEX_NONE;if(Index<0 || !CFInputQA::Press(Hero,EKeys::C))return End(false,TEXT("menu"));for(int32 I=0;I<Index;++I)if(!CFInputQA::Press(Hero,EKeys::Down))return End(false,TEXT("recipe-select"));}if(!Hero->GetBackpackRecipeStatus(Hero->GetBackpackRecipe()).StartsWith(TEXT("可以制作")))return End(false,TEXT("recipe-permission"));if(Photo(TEXT("Recipe"),Indoor->GetActorLocation()))return true;if(!CFInputQA::Press(Hero,EKeys::Enter))return End(false,TEXT("craft-input"));Crafted=true;if(!Budget() || Hero->Inventory->Count(EFrontierItem::Material)!=5 || Hero->Inventory->Count(EFrontierItem::Fiber)!=0)return End(false,TEXT("craft-cost"));UE_LOG(LogTemp,Display,TEXT("CF_INDOOR_CRAFT_PROCESS PASS indoorOnly=1 inputMaterial=1 inputFiber=2 outputMaterial=4 finalMaterial=5"));Photo(TEXT("Crafted"),Indoor->GetActorLocation());Phase=11;return true;}
        if(Phase==11){const auto Before=Hero->Inventory->GetStacks();if(!CFInputQA::Press(Hero,EKeys::Enter) || !Hero->Inventory->GetStacks().OrderIndependentCompareEqual(Before) || !CFInputQA::Press(Hero,EKeys::C))return End(false,TEXT("repeat"));const auto HB=H,PB=P,RB=R;const auto HP=Hero->Health,PP=Pet->Health,RP=Recruit->Health;if(!Hero->SaveProgress(TEXT("IndoorCraftDone")) || !Hero->LoadProgress(TEXT("IndoorCraftDone")))return End(false,TEXT("save"));BindCraft();if(!Budget() || Door->DoorState!=1 || !OnlyIndoor() || Hero->Health!=HP || Pet->Health!=PP || Recruit->Health!=RP || !Hero->GetActorLocation().Equals(HB,.01) || !Pet->GetActorLocation().Equals(PB,.01) || !Recruit->GetActorLocation().Equals(RB,.01))return End(false,TEXT("restore"));UE_LOG(LogTemp,Display,TEXT("CF_INDOOR_CRAFT_LIFE PASS material=5 fiber=0 facilities=2 modules=7 oldXP=40 newXP=17 identity=1 closed=1"));return End(true,TEXT("complete"));}
        return true;
    }
};
}
namespace
{
struct FCompanionCraft : FStarterWorkshop
{
    float HeldWork=0;
    bool Done()const{return Hero->Inventory->Count(EFrontierItem::Material)==0 && Hero->Inventory->Count(EFrontierItem::Medicine)==Bag.FindRef(EFrontierItem::Medicine)+1;}
    bool End(bool Pass,const TCHAR* Why){if(Input.IsValid())Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::LeftShift);const auto Feedback=Hero->Feedback;if(!Pass)Hero->SaveProgress(TEXT("CompanionCraftBlocked"));UE_LOG(LogTemp,Display,TEXT("CF_COMPANION_CRAFT %s reason=%s phase=%d work=%.4f hero=%s new=%s feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Why,Phase,Indoor.IsValid()?Indoor->CraftWork:-1,*Hero->GetActorLocation().ToString(),Recruit.IsValid()?*Recruit->GetActorLocation().ToString():TEXT("none"),*Feedback);FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;}
    bool Budget()const
    {
        if(!Pet.IsValid() || !Recruit.IsValid() || !Farm.IsValid() || !Indoor.IsValid() || Hero->Health!=PlayerHealth || Hero->Health<=0 || Pet->Health!=110 || Recruit->Health!=80 || Pet->Level!=2 || Pet->Experience!=40 || Pet->BondPoints!=6 || Pet->Nickname!=Nickname || Recruit->Level!=1 || Recruit->Experience!=(Done()?18:17) || Recruit->BondPoints!=0 || Hero->GetCompanionRoster().Num()!=2 || Pet->Activity!=ECreatureActivity::Work || Pet->WorkSite!=Farm.Get())return false;
        auto Expected=Bag;Expected.Add(EFrontierItem::Material,Done()?0:5);Expected.Add(EFrontierItem::Medicine,Bag.FindRef(EFrontierItem::Medicine)+(Done()?1:0));for(int32 I=0;I<static_cast<int32>(EFrontierItem::Count);++I)if(Hero->Inventory->Count(static_cast<EFrontierItem>(I))!=Expected.FindRef(static_cast<EFrontierItem>(I)))return false;
        if(Farm->StoredFood!=Garden.StoredFood || Farm->Crop->Seeds!=Garden.Seeds || Farm->Crop->bPlanted!=Garden.bPlanted || Farm->Crop->GrowthWork!=Garden.GrowthWork || !Farm->GetActorTransform().Equals(Garden.Transform,.01) || !Farm->CraftRecipeId.IsNone() || Farm->CraftWork!=0)return false;
        if(Done()){if(!Indoor->CraftRecipeId.IsNone() || Indoor->CraftWork!=0 || Recruit->Activity!=ECreatureActivity::Rest || Recruit->WorkSite!=Indoor.Get())return false;}
        else if(Phase>=1 && (Indoor->CraftRecipeId!=TEXT("Medicine") || Indoor->CraftWork<0 || Indoor->CraftWork>=8))return false;
        int32 Facilities=0;for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt){++Facilities;if(It->PersistentId!=Garden.Id && It->PersistentId!=IndoorId)return false;}if(Facilities!=2 || !Indoor->GetActorTransform().Equals(Placement(),.01))return false;
        const auto Layout=HomeLayout::Collect(World);if(Layout.Num()!=Original.Num())return false;for(const auto& R:Original){const auto* V=Layout.FindByPredicate([&](const FHomeModuleRecord& X){return X.Id==R.Id;});if(!V || V->Definition!=R.Definition || !V->Transform.Equals(R.Transform,.01) || V->PaidMaterials!=R.PaidMaterials || !V->StoredItems.OrderIndependentCompareEqual(R.StoredItems))return false;}
        for(TActorIterator<AResourceNode> It(World);It;++It)if(const auto* Before=Resources.Find(It->PersistentId))if(It->Remaining!=*Before)return false;
        return Pet->Cargo->Count()==0 && Recruit->Cargo->Count()==0 && Hero->GetObjectives().Claimed==5;
    }
    bool BadRecords()
    {
        const auto HB=Hero->GetActorLocation(),PB=Pet->GetActorLocation(),RB=Recruit->GetActorLocation();
        for(int32 Case=0;Case<7;++Case){auto* Bad=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Read?TEXT("CompanionCraftInput"):TEXT("CompanionCraftMid"),0));if(!Bad)return false;auto* Record=Bad->Facilities.FindByPredicate([&](const FFacilitySaveRecord& X){return X.Id==IndoorId;});if(!Record)return false;
            if(Case==0)Record->CraftRecipe=TEXT("UnknownRecipe");if(Case==1)Record->CraftWork=std::numeric_limits<float>::quiet_NaN();if(Case==2)Record->CraftWork=9;if(Case==3)Record->CraftWork=-1;if(Case==4)Record->CraftRevision=2;if(Case==5)Record->Specialty=EWorkSpecialty::Farming;if(Case==6)Record->CraftRevision=0;
            if(!UGameplayStatics::SaveGameToSlot(Bad,TEXT("CompanionCraftBad"),0) || Hero->LoadProgress(TEXT("CompanionCraftBad")) || !Budget() || Indoor->CraftWork!=HeldWork || !Hero->GetActorLocation().Equals(HB,.01) || !Pet->GetActorLocation().Equals(PB,.01) || !Recruit->GetActorLocation().Equals(RB,.01))return false;
        }
        UE_LOG(LogTemp,Display,TEXT("CF_COMPANION_CRAFT_INVALID PASS rejected=7 worldUnchanged=1"));return true;
    }
    bool Tick()
    {
        if(!Budget() || FPlatformTime::Seconds()>Deadline)return End(false,TEXT("budget-time"));const auto H=Hero->GetActorLocation(),P=Pet->GetActorLocation(),R=Recruit->GetActorLocation();const double Dt=World->GetTimeSeconds()-LastTime;
        if(!Motion(Hero,LastH,VelocityH,Dt) || !Motion(Pet.Get(),LastP,VelocityP,Dt) || !Motion(Recruit.Get(),LastR,VelocityR,Dt))return End(false,TEXT("motion"));LastH=H;LastP=P;LastR=R;VelocityH=Hero->GetVelocity();VelocityP=Pet->GetVelocity();VelocityR=Recruit->GetVelocity();LastTime=World->GetTimeSeconds();
        int32 Regions=0;for(TActorIterator<AWildEncounter> It(World);It;++It){++Regions;if(!It->IsActorTickEnabled())return End(false,TEXT("region"));}if(Regions!=3)return End(false,TEXT("regions"));for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Health>0 && (!It->IsActorTickEnabled() || !It->GetActorEnableCollision() || It->GetCharacterMovement()->MovementMode==MOVE_None))return End(false,TEXT("ecology"));
        if(FPlatformTime::Seconds()<ShotUntil)return true;
        if(Phase==-1){if(World->GetTimeSeconds()-At<2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;Phase=Read?6:0;At=World->GetTimeSeconds();return true;}
        if(Phase==0){Face(R);if(Hero->TraceCreature()!=Recruit.Get() || !CFInputQA::Press(Hero,EKeys::L))return End(false,TEXT("select"));Face(Indoor->GetActorLocation()+FVector(0,0,25));if(!CFInputQA::Press(Hero,EKeys::C) || !CFInputQA::Press(Hero,EKeys::Down) || !CFInputQA::ShiftPress(Hero,EKeys::Enter) || Indoor->CraftRecipeId!=TEXT("Medicine") || Indoor->CraftWork!=0 || !Budget())return End(false,TEXT("queue"));if(!CFInputQA::ShiftPress(Hero,EKeys::Enter) || !Indoor->CraftRecipeId.IsNone() || !Budget() || !CFInputQA::ShiftPress(Hero,EKeys::Enter))return End(false,TEXT("cancel-requeue"));UE_LOG(LogTemp,Display,TEXT("CF_COMPANION_CRAFT_CANCEL PASS noSpend=1 requeued=1"));if(Photo(TEXT("Queued"),Indoor->GetActorLocation())){Phase=8;return true;}Phase=8;return true;}
        if(Phase==8){if(Hero->IsBackpackOpen() && !CFInputQA::Press(Hero,EKeys::C))return End(false,TEXT("close-menu"));Face(FVector(-200,-400,245));if(Door->DoorState==1 && (AHomeModule::TraceDoor(Hero)!=Door.Get() || !CFInputQA::Press(Hero,EKeys::E) || Door->DoorState!=2))return End(false,TEXT("open"));Phase=9;At=World->GetTimeSeconds();return true;}
        if(Phase==9){if(World->GetTimeSeconds()-At<1 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;if(!CFInputQA::Press(Hero,EKeys::F) || !Plan(FVector(-450,-400,40),1))return End(false,TEXT("outside"));return true;}
        if(Phase==1){const int32 Result=Move();if(Result<0)return End(false,TEXT("outside-walk"));if(!Result)return true;if(R.X> -250){if(World->GetTimeSeconds()-At>20)return End(false,TEXT("pet-outside"));return true;}if(Indoor->CraftWork!=0)return End(false,TEXT("idle-produced"));Face(Indoor->GetActorLocation()+FVector(0,0,25));if(!CFInputQA::Press(Hero,EKeys::E) || Recruit->WorkSite!=Indoor.Get() || Recruit->Activity!=ECreatureActivity::Work || !Plan(FVector(-100,-550,60),2))return End(false,TEXT("assign"));return true;}
        if(Phase==2){const int32 Result=Move();if(Result<0)return End(false,TEXT("inside-walk"));if(Result){Phase=3;At=World->GetTimeSeconds();}return true;}
        if(Phase==3){if(Indoor->CraftWork<1){if(World->GetTimeSeconds()-At>20)return End(false,TEXT("work-arrival"));return true;}if(Photo(TEXT("Working"),R))return true;if(Indoor->CraftWork<2)return true;if(!CFInputQA::Press(Hero,EKeys::F) || Recruit->Activity!=ECreatureActivity::Follow)return End(false,TEXT("interrupt"));HeldWork=Indoor->CraftWork;Phase=4;At=World->GetTimeSeconds();return true;}
        if(Phase==4){if(Indoor->CraftWork!=HeldWork)return End(false,TEXT("interrupted-advanced"));if(World->GetTimeSeconds()-At<1)return true;if(Recruit->Activity==ECreatureActivity::Follow){Face(Indoor->GetActorLocation()+FVector(0,0,25));if(!CFInputQA::Press(Hero,EKeys::T) || Recruit->Activity!=ECreatureActivity::Rest || Recruit->bResumeWorkAfterRest || Recruit->WorkSite!=Indoor.Get())return End(false,TEXT("hold-rest"));return true;}if(Recruit->GetDistanceTo(Indoor.Get())>220 || R.Z<50 || !VisibleIndoor()){if(World->GetTimeSeconds()-At>25)return End(false,TEXT("hold-arrival"));return true;}Face(FVector(-200,-400,245));if(Door->DoorState==2 && (AHomeModule::TraceDoor(Hero)!=Door.Get() || !CFInputQA::Press(Hero,EKeys::E) || Door->DoorState!=1))return End(false,TEXT("close"));if(!Hero->SaveProgress(TEXT("CompanionCraftMid")))return End(false,TEXT("mid-save"));UE_LOG(LogTemp,Display,TEXT("CF_COMPANION_CRAFT_MID PASS progress=%.4f material=5 noEarlyOutput=1"),HeldWork);if(Photo(TEXT("Interrupted"),Indoor->GetActorLocation())){Phase=5;return true;}Phase=5;return true;}
        if(Phase==5){if(!BadRecords())return End(false,TEXT("invalid-records"));Phase=6;return true;}
        if(Phase==6){if(Read){Face(R);if(Hero->TraceCreature()!=Recruit.Get() || !CFInputQA::Press(Hero,EKeys::L))return End(false,TEXT("read-select"));if(!BadRecords())return End(false,TEXT("read-invalid"));}Face(Indoor->GetActorLocation()+FVector(0,0,25));if(!CFInputQA::Press(Hero,EKeys::E) || Recruit->WorkSite!=Indoor.Get() || Recruit->Activity!=ECreatureActivity::Work)return End(false,TEXT("resume-work"));Phase=7;At=World->GetTimeSeconds();return true;}
        if(Phase==7){if(!Done()){if(World->GetTimeSeconds()-At>15)return End(false,TEXT("finish-time"));return true;}if(Photo(TEXT("Completed"),Indoor->GetActorLocation()))return true;const auto HB=H,PB=P,RB=R;if(!Hero->SaveProgress(TEXT("CompanionCraftDone")) || !Hero->LoadProgress(TEXT("CompanionCraftDone")))return End(false,TEXT("save"));BindWorkshop();if(!Budget() || Door->DoorState!=1 || !Hero->GetActorLocation().Equals(HB,.01) || !Pet->GetActorLocation().Equals(PB,.01) || !Recruit->GetActorLocation().Equals(RB,.01))return End(false,TEXT("restore"));UE_LOG(LogTemp,Display,TEXT("CF_COMPANION_CRAFT_LIFE PASS medicineAdded=1 materialPaid=5 newXP=18 oldXP=40 rest=1 identity=1 once=1"));return End(true,TEXT("complete"));}
        return true;
    }
};
}
namespace
{
struct FFacilityMove : FCompanionCraft
{
    bool Moved=false;
    FTransform Destination()const{return FTransform(FRotator(0,180,0),FVector(-500,-700,-15),FVector(2,1,.7));}
    bool EndMove(bool Pass,const TCHAR* Why)
    {
        if(Input.IsValid())Input->Direction=FVector::ZeroVector;
        CFInputQA::Release(Hero,EKeys::LeftShift);
        const FString BeforeFeedback=Hero->Feedback;
        if(!Pass)Hero->SaveProgress(TEXT("FacilityMoveBlocked"));
        const TCHAR* Result=Pass?TEXT("PASS"):TEXT("FAIL");
        UE_LOG(LogTemp,Display,TEXT("CF_FACILITY_MOVE %s reason=%s phase=%d work=%.4f hero=%s pet=%s feedback=%s"),Result,Why,Phase,Indoor.IsValid()?Indoor->CraftWork:-1,*Hero->GetActorLocation().ToString(),Recruit.IsValid()?*Recruit->GetActorLocation().ToString():TEXT("none"),*BeforeFeedback);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    bool TickMove()
    {
        if(!Budget() || FPlatformTime::Seconds()>Deadline)return EndMove(false,TEXT("budget-time"));
        const auto H=Hero->GetActorLocation(),P=Pet->GetActorLocation(),R=Recruit->GetActorLocation();const double Dt=World->GetTimeSeconds()-LastTime;
        if(!Motion(Hero,LastH,VelocityH,Dt) || !Motion(Pet.Get(),LastP,VelocityP,Dt) || !Motion(Recruit.Get(),LastR,VelocityR,Dt))return EndMove(false,TEXT("motion"));
        LastH=H;LastP=P;LastR=R;VelocityH=Hero->GetVelocity();VelocityP=Pet->GetVelocity();VelocityR=Recruit->GetVelocity();LastTime=World->GetTimeSeconds();
        int32 Regions=0;for(TActorIterator<AWildEncounter> It(World);It;++It){++Regions;if(!It->IsActorTickEnabled())return EndMove(false,TEXT("region"));}if(Regions!=3)return EndMove(false,TEXT("regions"));
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Health>0 && (!It->IsActorTickEnabled() || !It->GetActorEnableCollision() || It->GetCharacterMovement()->MovementMode==MOVE_None))return EndMove(false,TEXT("ecology"));
        if(!Done() && Phase<8 && (Indoor->CraftWork!=HeldWork || Recruit->WorkSite!=Indoor.Get() || Recruit->Activity!=ECreatureActivity::Rest || Recruit->bResumeWorkAfterRest))return EndMove(false,TEXT("paused-job"));
        if(FPlatformTime::Seconds()<ShotUntil)return true;
        if(Phase==-1){if(World->GetTimeSeconds()-At<2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;Phase=Read?7:0;return true;}
        if(Phase==0)
        {
            Face(R);if(Hero->TraceCreature()!=Recruit.Get() || !CFInputQA::Press(Hero,EKeys::L))return EndMove(false,TEXT("select"));
            Face(FVector(-200,-400,245));if(Door->DoorState==1 && (AHomeModule::TraceDoor(Hero)!=Door.Get() || !CFInputQA::Press(Hero,EKeys::E) || Door->DoorState!=2))return EndMove(false,TEXT("door"));
            Phase=1;At=World->GetTimeSeconds();return true;
        }
        if(Phase==1){if(World->GetTimeSeconds()-At<1 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;if(!Plan(FVector(-450,-500,40),2))return EndMove(false,TEXT("outside-plan"));return true;}
        if(Phase==2)
        {
            const int32 Result=Move();if(Result<0)return EndMove(false,TEXT("outside-walk"));if(!Result)return true;
            Face(Indoor->GetActorLocation()+FVector(0,-40,25));
            FHitResult AimHit;FCollisionQueryParams AimQuery(SCENE_QUERY_STAT(FacilityMovePickup),false,Hero);const FVector AimStart=Hero->FirstPersonCamera->GetComponentLocation();World->LineTraceSingleByChannel(AimHit,AimStart,AimStart+Hero->FirstPersonCamera->GetForwardVector()*600,ECC_Visibility,AimQuery);
            UE_LOG(LogTemp,Display,TEXT("CF_FACILITY_MOVE_AIM actor=%s point=%s"),*GetNameSafe(AimHit.GetActor()),*AimHit.ImpactPoint.ToString());
            if(!CFInputQA::Press(Hero,EKeys::B) || !CFInputQA::Press(Hero,EKeys::Insert) || !Hero->Building->IsRelocating())return EndMove(false,TEXT("pickup"));
            if(!CFInputQA::Press(Hero,EKeys::B) || Hero->Building->IsActive() || !Budget() || Indoor->CraftWork!=HeldWork)return EndMove(false,TEXT("cancel"));
            UE_LOG(LogTemp,Display,TEXT("CF_FACILITY_MOVE_CANCEL PASS sameActor=1 progress=1 noSpend=1"));
            Face(Indoor->GetActorLocation()+FVector(0,-40,25));
            if(!CFInputQA::Press(Hero,EKeys::B) || !CFInputQA::Press(Hero,EKeys::Insert) || !Hero->Building->IsRelocating() || !CFInputQA::Press(Hero,EKeys::Nine))return EndMove(false,TEXT("pickup-again"));
            Phase=3;return true;
        }
        if(Phase==3)
        {
            Face(FVector(-500,-700,-50));Hero->Building->TickComponent(0,LEVELTICK_All,nullptr);
            if(!Hero->Building->GetPlacementTransform().Equals(Destination(),.1))return EndMove(false,TEXT("preview-pose"));
            FString Reason;if(!Hero->Building->ValidatePlacement(Destination(),Reason))return EndMove(false,TEXT("preview-blocked"));
            if(Photo(TEXT("Preview"),FVector(-500,-700,-50)))return true;
            auto* Same=Indoor.Get();if(!CFInputQA::Action(Hero,TEXT("Attack"),EKeys::LeftMouseButton))return EndMove(false,TEXT("confirm-input"));
            ExpectedPlacement=Destination();Moved=true;
            if(Hero->Building->IsActive() || Indoor.Get()!=Same || !Budget() || Indoor->CraftWork!=HeldWork)return EndMove(false,TEXT("move-state"));
            UE_LOG(LogTemp,Display,TEXT("CF_FACILITY_MOVE_COMMIT PASS sameActor=1 sameId=1 progress=1 noSpend=1"));
            Phase=4;At=World->GetTimeSeconds();return true;
        }
        if(Phase==4){if(World->GetTimeSeconds()-At<1 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;if(!Plan(FVector(-250,-850,40),5))return EndMove(false,TEXT("observer-plan"));return true;}
        if(Phase==5){const int32 Result=Move();if(Result<0)return EndMove(false,TEXT("observer-walk"));if(Result){Phase=6;At=World->GetTimeSeconds();}return true;}
        if(Phase==6)
        {
            if(Recruit->GetDistanceTo(Indoor.Get())>220 || !VisibleIndoor()){if(World->GetTimeSeconds()-At>30)return EndMove(false,TEXT("arrival"));return true;}
            if(Photo(TEXT("Arrived"),Indoor->GetActorLocation()))return true;
            const auto HB=H,PB=P,RB=R;
            if(!Hero->SaveProgress(TEXT("FacilityMoveMid")) || !Hero->LoadProgress(TEXT("FacilityMoveMid")))return EndMove(false,TEXT("mid-save"));
            BindWorkshop();if(!Budget() || Indoor->CraftWork!=HeldWork || !Hero->GetActorLocation().Equals(HB,.01) || !Pet->GetActorLocation().Equals(PB,.01) || !Recruit->GetActorLocation().Equals(RB,.01))return EndMove(false,TEXT("mid-restore"));
            UE_LOG(LogTemp,Display,TEXT("CF_FACILITY_MOVE_MID PASS actualArrival=1 progress=%.4f identity=1 position=1"),HeldWork);
            Phase=7;return true;
        }
        if(Phase==7)
        {
            // 读档会清除临时单选范围，正常续工前必须重新选择伙伴。
            Face(R);if(Hero->TraceCreature()!=Recruit.Get() || !CFInputQA::Press(Hero,EKeys::L))return EndMove(false,TEXT("resume-select"));
            Face(Indoor->GetActorLocation()+FVector(0,0,25));if(!CFInputQA::Press(Hero,EKeys::E) || Recruit->WorkSite!=Indoor.Get() || Recruit->Activity!=ECreatureActivity::Work)return EndMove(false,TEXT("resume"));
            Phase=8;At=World->GetTimeSeconds();return true;
        }
        if(Phase==8)
        {
            if(!Done()){if(World->GetTimeSeconds()-At>20)return EndMove(false,TEXT("finish"));return true;}
            if(Photo(TEXT("Completed"),Indoor->GetActorLocation()))return true;
            if(!Hero->SaveProgress(TEXT("FacilityMoveDone")) || !Hero->LoadProgress(TEXT("FacilityMoveDone")))return EndMove(false,TEXT("done-save"));BindWorkshop();
            if(!Budget())return EndMove(false,TEXT("done-restore"));
            UE_LOG(LogTemp,Display,TEXT("CF_FACILITY_MOVE_LIFE PASS materialPaid=5 medicineAdded=1 oldXP=40 newXP=18 identity=1 moved=1 saved=1"));return EndMove(true,TEXT("complete"));
        }
        return true;
    }
};
}
namespace
{
struct FStarterPantry : FStarterWorkshop
{
    TWeakObjectPtr<AResourceNode> Supply;TWeakObjectPtr<AWorkStation> Trough;FGuid TroughId;
    int32 SupplyStart=0;bool Paid=false,Stocked=false,SawMeal=false,Funded=false,ReturnOnly=false;
    FTransform PantryPose()const{return FTransform(FRotator(0,90,0),FVector(100,-400,5),FVector(2,1,.7));}
    void BindPantry(){BindWorkshop();Supply.Reset();Trough.Reset();for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,6))Supply=*It;for(TActorIterator<AWorkStation> It(World);It;++It)if(It->PersistentId==TroughId)Trough=*It;}
    bool EndPantry(bool Pass,const TCHAR* Why)
    {
        if(Input.IsValid())Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::M);CFInputQA::Release(Hero,EKeys::LeftShift);
        const FString Feedback=Hero->Feedback;if(!Pass)Hero->SaveProgress(TEXT("StarterPantryBlocked"));const TCHAR* Result=Pass?TEXT("PASS"):TEXT("FAIL");
        UE_LOG(LogTemp,Display,TEXT("CF_STARTER_PANTRY %s reason=%s phase=%d material=%d stock=%d nutrition=%.2f hero=%s worker=%s feedback=%s"),Result,Why,Phase,Hero->Inventory->Count(EFrontierItem::Material),Trough.IsValid()?Trough->StoredFood:-1,Pet.IsValid()?Pet->Nutrition:-1,*Hero->GetActorLocation().ToString(),Pet.IsValid()?*Pet->GetActorLocation().ToString():TEXT("none"),*Feedback);FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    bool BudgetPantry()const
    {
        if(!Pet.IsValid() || !Recruit.IsValid() || !Farm.IsValid() || !Indoor.IsValid() || !Supply.IsValid() || Hero->Health!=PlayerHealth || Pet->Health!=110 || Recruit->Health!=80 || Pet->Experience!=40 || Recruit->Experience!=18 || Pet->Nickname!=Nickname || Pet->WorkSite!=Farm.Get() || Pet->Activity!=ECreatureActivity::Work || Recruit->WorkSite!=Indoor.Get() || Recruit->Activity!=ECreatureActivity::Rest || Hero->GetCompanionRoster().Num()!=2)return false;
        const int32 Taken=SupplyStart-Supply->Remaining;if(Taken<0 || Taken>12)return false;
        auto Expected=Bag;Expected.Add(EFrontierItem::Material,Taken-(Paid?12:0));Expected.Add(EFrontierItem::Food,Stocked?0:3);for(int32 I=0;I<static_cast<int32>(EFrontierItem::Count);++I)if(Hero->Inventory->Count(static_cast<EFrontierItem>(I))!=Expected.FindRef(static_cast<EFrontierItem>(I)))return false;
        if(!Indoor->GetActorTransform().Equals(ExpectedPlacement,.01) || !Indoor->CraftRecipeId.IsNone() || Indoor->CraftWork!=0 || Farm->StoredFood!=Garden.StoredFood || Farm->Crop->Seeds!=Garden.Seeds || Farm->Crop->bPlanted!=Garden.bPlanted || Farm->Crop->GrowthWork!=Garden.GrowthWork || !Farm->GetActorTransform().Equals(Garden.Transform,.01))return false;
        int32 Count=0;for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt){++Count;if(It->PersistentId!=IndoorId && It->PersistentId!=Garden.Id && (!Paid || It->PersistentId!=TroughId))return false;}if(Count!=(Paid?3:2))return false;
        if(Paid && (!Trough.IsValid() || !Trough->GetActorTransform().Equals(PantryPose(),.01) || Trough->RequiredSpecialty!=EWorkSpecialty::Feeding || Trough->StoredFood!=(Stocked?(SawMeal?2:3):0)))return false;
        const auto Layout=HomeLayout::Collect(World);if(Layout.Num()!=Original.Num())return false;for(const auto& R:Original){const auto* V=Layout.FindByPredicate([&](const FHomeModuleRecord& X){return X.Id==R.Id;});if(!V || V->Definition!=R.Definition || !V->Transform.Equals(R.Transform,.01) || V->PaidMaterials!=R.PaidMaterials || !V->StoredItems.OrderIndependentCompareEqual(R.StoredItems))return false;}
        for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId!=Supply->PersistentId)if(const auto* N=Resources.Find(It->PersistentId))if(It->Remaining!=*N)return false;
        return Pet->Cargo->Count()==0 && Recruit->Cargo->Count()==0 && Hero->GetObjectives().Claimed==5;
    }
    bool TickPantry()
    {
        if(Stocked && Trough.IsValid() && Trough->StoredFood==2 && !SawMeal){const FVector P=Pet->GetActorLocation();if(P.X< -150 || P.X>150 || FMath::Abs(P.Y+400)>160 || P.Z<50 || Pet->Nutrition<=40)return EndPantry(false,TEXT("meal-not-inside"));SawMeal=true;UE_LOG(LogTemp,Display,TEXT("CF_STARTER_PANTRY_MEAL PASS inside=1 stock=2 nutrition=%.3f task=1"),Pet->Nutrition);}
        if(!BudgetPantry() || FPlatformTime::Seconds()>Deadline)return EndPantry(false,TEXT("budget-time"));
        const auto H=Hero->GetActorLocation(),P=Pet->GetActorLocation(),R=Recruit->GetActorLocation();const double Dt=World->GetTimeSeconds()-LastTime;
        if(!Motion(Hero,LastH,VelocityH,Dt) || !Motion(Pet.Get(),LastP,VelocityP,Dt) || !Motion(Recruit.Get(),LastR,VelocityR,Dt))return EndPantry(false,TEXT("motion"));LastH=H;LastP=P;LastR=R;VelocityH=Hero->GetVelocity();VelocityP=Pet->GetVelocity();VelocityR=Recruit->GetVelocity();LastTime=World->GetTimeSeconds();
        int32 Regions=0;for(TActorIterator<AWildEncounter> It(World);It;++It){++Regions;if(!It->IsActorTickEnabled())return EndPantry(false,TEXT("region"));}if(Regions!=3)return EndPantry(false,TEXT("regions"));for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Health>0 && (!It->IsActorTickEnabled() || !It->GetActorEnableCollision() || It->GetCharacterMovement()->MovementMode==MOVE_None))return EndPantry(false,TEXT("ecology"));
        if(FPlatformTime::Seconds()<ShotUntil)return true;
        if(Phase==-1){if(World->GetTimeSeconds()-At<2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;if(ReturnOnly){Phase=7;At=World->GetTimeSeconds();return true;}if(Read){if(!Plan(FVector(-450,-270,40),6))return EndPantry(false,TEXT("stocked-layout-plan"));return true;}if(Funded){if(!Plan(FVector(-650,-1000,40),90))return EndPantry(false,TEXT("funded-return-plan"));return true;}if(!Plan(FVector(600,-1200,40),0))return EndPantry(false,TEXT("supply-plan"));return true;}
        if(Phase==0){const int32 Result=Move();if(Result<0)return EndPantry(false,TEXT("supply-walk"));if(!Result)return true;Face(Supply->GetActorLocation());if(Hero->TraceResource()!=Supply.Get() || !CFInputQA::Press(Hero,EKeys::M))return EndPantry(false,TEXT("gather-input"));Phase=1;At=World->GetTimeSeconds();return true;}
        if(Phase==1){Face(Supply->GetActorLocation());if(Hero->Inventory->Count(EFrontierItem::Material)<12){if(World->GetTimeSeconds()-At>30)return EndPantry(false,TEXT("gather-time"));return true;}CFInputQA::Release(Hero,EKeys::M);UE_LOG(LogTemp,Display,TEXT("CF_STARTER_PANTRY_FUND PASS actualHarvest=12 supplyRemaining=%d material=12"),Supply->Remaining);if(!Hero->SaveProgress(TEXT("StarterPantryFunded")) || !Plan(FVector(-650,-1000,40),90))return EndPantry(false,TEXT("return-plan"));return true;}
        if(Phase==90){const int32 Result=Move();if(Result<0)return EndPantry(false,TEXT("south-return-walk"));if(!Result)return true;if(!Plan(FVector(-650,-200,40),91))return EndPantry(false,TEXT("west-return-plan"));return true;}
        if(Phase==91){const int32 Result=Move();if(Result<0)return EndPantry(false,TEXT("west-return-walk"));if(!Result)return true;if(!Plan(FVector(-450,-270,40),2))return EndPantry(false,TEXT("door-return-plan"));return true;}
        if(Phase==2){const int32 Result=Move();if(Result<0)return EndPantry(false,TEXT("return-walk"));if(!Result)return true;if(!Plan(FVector(-100,-530,60),3))return EndPantry(false,TEXT("inside-plan"));return true;}
        if(Phase==3){const int32 Result=Move();if(Result<0)return EndPantry(false,TEXT("inside-walk"));if(!Result)return true;Face(FVector(100,-400,-30));if(!CFInputQA::Press(Hero,EKeys::B))return EndPantry(false,TEXT("build-open"));for(int32 I=0;I<6 && Hero->Building->Selected!=EWorkSpecialty::Feeding;++I)if(!CFInputQA::Press(Hero,EKeys::V))return EndPantry(false,TEXT("build-type"));if(!CFInputQA::Press(Hero,EKeys::Nine))return EndPantry(false,TEXT("build-rotate"));Phase=4;return true;}
        if(Phase==4){Face(FVector(100,-400,-30));Hero->Building->TickComponent(0,LEVELTICK_All,nullptr);if(!Hero->Building->GetPlacementTransform().Equals(PantryPose(),.1))return EndPantry(false,TEXT("build-pose"));if(Photo(TEXT("Preview"),FVector(100,-400,-30)))return true;if(!CFInputQA::Action(Hero,TEXT("Attack"),EKeys::LeftMouseButton))return EndPantry(false,TEXT("build-confirm"));for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt && It->RequiredSpecialty==EWorkSpecialty::Feeding){Trough=*It;TroughId=It->PersistentId;}Paid=true;if(!BudgetPantry() || !CFInputQA::Press(Hero,EKeys::B))return EndPantry(false,TEXT("build-cost"));Phase=5;return true;}
        if(Phase==5){Face(Trough->GetActorLocation()+FVector(0,0,25));if(Hero->TraceFacility()!=Trough.Get() || !CFInputQA::Press(Hero,EKeys::U))return EndPantry(false,TEXT("deposit"));Stocked=true;if(!BudgetPantry() || !Hero->SaveProgress(TEXT("StarterPantryMid")))return EndPantry(false,TEXT("stock-save"));UE_LOG(LogTemp,Display,TEXT("CF_STARTER_PANTRY_MID PASS paid=12 stock=3 bagFood=0 workerNutrition=%.3f"),Pet->Nutrition);Phase=51;return true;}
        if(Phase==51){if(Photo(TEXT("Stocked"),Trough->GetActorLocation()+FVector(0,0,25)))return true;if(!Plan(FVector(-450,-270,40),6))return EndPantry(false,TEXT("observer-plan"));return true;}
        if(Phase==6){const int32 Result=Move();if(Result<0)return EndPantry(false,TEXT("observer-walk"));if(!Result)return true;Face(Farm->GetActorLocation()+FVector(0,0,25));if(!CFInputQA::Press(Hero,EKeys::B) || !CFInputQA::Press(Hero,EKeys::Insert) || !Hero->Building->IsRelocating() || !Plan(FVector(-650,-200,40),64))return EndPantry(false,TEXT("farm-pickup"));return true;}
        if(Phase==64){const int32 Result=Move();if(Result<0)return EndPantry(false,TEXT("move-preview-west"));if(!Result)return true;if(!Plan(FVector(-650,-1000,40),61))return EndPantry(false,TEXT("move-preview-south-plan"));return true;}
        if(Phase==61){const int32 Result=Move();if(Result<0)return EndPantry(false,TEXT("move-preview-south"));if(!Result)return true;if(!Plan(FVector(-100,-1100,40),62))return EndPantry(false,TEXT("move-preview-clear-plan"));return true;}
        if(Phase==62){const int32 Result=Move();if(Result<0)return EndPantry(false,TEXT("move-preview-clear"));if(!Result)return true;Phase=60;return true;}
        if(Phase==60){Face(FVector(-300,-900,-50));Hero->Building->TickComponent(0,LEVELTICK_All,nullptr);FTransform Expected=Garden.Transform;Expected.SetLocation(FVector(-300,-900,-15));if(!Hero->Building->GetPlacementTransform().Equals(Expected,.1))return EndPantry(false,TEXT("farm-preview"));auto* Same=Farm.Get();if(!CFInputQA::Action(Hero,TEXT("Attack"),EKeys::LeftMouseButton) || Hero->Building->IsActive() || Farm.Get()!=Same)return EndPantry(false,TEXT("farm-move"));Garden.Transform=Expected;if(!BudgetPantry())return EndPantry(false,TEXT("farm-state"));UE_LOG(LogTemp,Display,TEXT("CF_STARTER_PANTRY_LAYOUT PASS originalFarm=1 originalJob=1 noSpend=1 searchRangeUnchanged=1"));Phase=7;At=World->GetTimeSeconds();return true;}
        if(Phase==7){if(!SawMeal || Pet->FeedingSite.IsValid() || Pet->GetDistanceTo(Farm.Get())>220){if(World->GetTimeSeconds()-At>35)return EndPantry(false,TEXT("meal-return"));return true;}if(Photo(TEXT("Returned"),Pet->GetActorLocation()))return true;const auto HB=H,PB=P,RB=R;if(!Hero->SaveProgress(TEXT("StarterPantryDone")) || !Hero->LoadProgress(TEXT("StarterPantryDone")))return EndPantry(false,TEXT("save"));BindPantry();if(!BudgetPantry() || !Hero->GetActorLocation().Equals(HB,.01) || !Pet->GetActorLocation().Equals(PB,.01) || !Recruit->GetActorLocation().Equals(RB,.01))return EndPantry(false,TEXT("restore"));UE_LOG(LogTemp,Display,TEXT("CF_STARTER_PANTRY_LIFE PASS paid=12 harvested=12 stock=2 bagFood=0 naturalMeal=1 originalJob=1 saved=1"));return EndPantry(true,TEXT("complete"));}
        return true;
    }
};
}
void RunStarterPantryScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto S=MakeShared<FStarterPantry>();S->World=World;S->Hero=Hero;S->Read=Read;S->At=World->GetTimeSeconds();S->Deadline=FPlatformTime::Seconds()+150;
    S->Funded=FParse::Param(FCommandLine::Get(),TEXT("CFStarterPantryFunded"));
    S->ReturnOnly=FParse::Param(FCommandLine::Get(),TEXT("CFStarterPantryReturn"));S->SawMeal=S->ReturnOnly;
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("StarterPantryInput"),0));if(!Source || Source->FormatVersion!=38 || Source->HomeModules.Num()!=7 || !Hero->LoadProgress(TEXT("StarterPantryInput"))){S->EndPantry(false,TEXT("source"));return;}
    S->Original=Source->HomeModules;for(const auto& F:Source->Facilities){if(F.Specialty==EWorkSpecialty::Farming)S->Garden=F;if(F.Specialty==EWorkSpecialty::Crafting){S->IndoorId=F.Id;S->ExpectedPlacement=F.Transform;}if(F.Specialty==EWorkSpecialty::Feeding)S->TroughId=F.Id;}
    S->Bag=Hero->Inventory->GetStacks();S->Bag.Add(EFrontierItem::Food,3);S->PlayerHealth=Hero->Health;for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController() && It->Definition){if(It->Definition->Form==ECreatureForm::Leaf){S->PetId=It->PersistentId;S->Nickname=It->Nickname;}if(It->Definition->Form==ECreatureForm::Ember)S->RecruitId=It->PersistentId;}
    S->BindPantry();for(const auto& N:Source->ResourceNodes)S->Resources.Add(N.Id,N.Remaining);if(S->Supply.IsValid())S->SupplyStart=S->Supply->Remaining+((Read || S->Funded)?12:0);S->Paid=Read;S->Stocked=Read;
    if(!S->BudgetPantry() || !S->Door.IsValid() || S->Door->DoorState!=2 || !S->Pet.IsValid() || S->Pet->Nutrition<=0 || (!S->ReturnOnly && S->Pet->Nutrition>40) || (!Read && S->TroughId.IsValid())){S->EndPantry(false,TEXT("source-budget"));return;}
    S->LastH=Hero->GetActorLocation();S->LastP=S->Pet->GetActorLocation();S->LastR=S->Recruit->GetActorLocation();S->VelocityH=Hero->GetVelocity();S->VelocityP=S->Pet->GetVelocity();S->VelocityR=S->Recruit->GetVelocity();S->LastTime=World->GetTimeSeconds();S->Input=NewObject<UHomeMovementProbe>(Hero);S->Input->RegisterComponent();
    S->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFStarterPantryPreview"));if(S->Preview){S->Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("StarterPantryPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*S->Root,true);UE_LOG(LogTemp,Display,TEXT("CF_STARTER_PANTRY_PREVIEW_ROOT %s"),*S->Root);}FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){return S->TickPantry();}));
}
void RunFacilityMoveScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto S=MakeShared<FFacilityMove>();S->World=World;S->Hero=Hero;S->Read=Read;S->At=World->GetTimeSeconds();S->Deadline=FPlatformTime::Seconds()+150;
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("FacilityMoveInput"),0));
    if(!Source || Source->FormatVersion!=38 || Source->Facilities.Num()!=2 || Source->HomeModules.Num()!=7 || !Hero->LoadProgress(TEXT("FacilityMoveInput"))){S->EndMove(false,TEXT("source"));return;}
    S->Original=Source->HomeModules;for(const auto& F:Source->Facilities){if(F.Specialty==EWorkSpecialty::Farming)S->Garden=F;if(F.Specialty==EWorkSpecialty::Crafting){S->IndoorId=F.Id;S->HeldWork=F.CraftWork;S->ExpectedPlacement=F.Transform;if(F.CraftRevision!=1 || F.CraftRecipe!=TEXT("Medicine") || F.CraftWork<=0 || F.CraftWork>=8){S->EndMove(false,TEXT("source-job"));return;}}}
    S->Bag=Hero->Inventory->GetStacks();S->PlayerHealth=Hero->Health;for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController() && It->Definition){if(It->Definition->Form==ECreatureForm::Leaf){S->PetId=It->PersistentId;S->Nickname=It->Nickname;}if(It->Definition->Form==ECreatureForm::Ember)S->RecruitId=It->PersistentId;}
    S->BindWorkshop();for(const auto& N:Source->ResourceNodes)S->Resources.Add(N.Id,N.Remaining);
    if(!S->Budget() || Hero->Inventory->Count(EFrontierItem::Material)!=5 || (Read && !S->ExpectedPlacement.Equals(S->Destination(),.01))){S->EndMove(false,TEXT("source-budget"));return;}
    S->Moved=Read;S->LastH=Hero->GetActorLocation();S->LastP=S->Pet->GetActorLocation();S->LastR=S->Recruit->GetActorLocation();S->VelocityH=Hero->GetVelocity();S->VelocityP=S->Pet->GetVelocity();S->VelocityR=S->Recruit->GetVelocity();S->LastTime=World->GetTimeSeconds();S->Input=NewObject<UHomeMovementProbe>(Hero);S->Input->RegisterComponent();
    S->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFFacilityMovePreview"));if(S->Preview){S->Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("FacilityMovePreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*S->Root,true);UE_LOG(LogTemp,Display,TEXT("CF_FACILITY_MOVE_PREVIEW_ROOT %s"),*S->Root);}
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){return S->TickMove();}));
}
void RunCompanionCraftScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto S=MakeShared<FCompanionCraft>();S->World=World;S->Hero=Hero;S->Read=Read;S->At=World->GetTimeSeconds();S->Deadline=FPlatformTime::Seconds()+120;
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("CompanionCraftInput"),0));if(!Source || Source->FormatVersion!=38 || Source->Facilities.Num()!=2 || Source->HomeModules.Num()!=7 || !Hero->LoadProgress(TEXT("CompanionCraftInput"))){S->End(false,TEXT("source"));return;}
    S->Original=Source->HomeModules;for(const auto& F:Source->Facilities){if(F.Specialty==EWorkSpecialty::Farming)S->Garden=F;if(F.Specialty==EWorkSpecialty::Crafting){S->IndoorId=F.Id;S->HeldWork=F.CraftWork;if(Read?(F.CraftRevision!=1 || F.CraftRecipe!=TEXT("Medicine") || F.CraftWork<=0 || F.CraftWork>=8):(F.CraftRevision!=0 || !F.CraftRecipe.IsNone() || F.CraftWork!=0)){S->End(false,TEXT("source-job"));return;}}}S->Bag=Hero->Inventory->GetStacks();S->PlayerHealth=Hero->Health;for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController() && It->Definition){if(It->Definition->Form==ECreatureForm::Leaf){S->PetId=It->PersistentId;S->Nickname=It->Nickname;}if(It->Definition->Form==ECreatureForm::Ember)S->RecruitId=It->PersistentId;}S->BindWorkshop();for(const auto& N:Source->ResourceNodes)S->Resources.Add(N.Id,N.Remaining);
    if(!S->Budget() || Hero->Inventory->Count(EFrontierItem::Material)!=5 || !Hero->GetActorTransform().Equals(Source->PlayerTransform,.01)){S->End(false,TEXT("source-budget"));return;}if(!Read)UE_LOG(LogTemp,Display,TEXT("CF_COMPANION_CRAFT_LEGACY PASS oldRecordEmpty=1 inventoryUnchanged=1"));
    S->LastH=Hero->GetActorLocation();S->LastP=S->Pet->GetActorLocation();S->LastR=S->Recruit->GetActorLocation();S->VelocityH=Hero->GetVelocity();S->VelocityP=S->Pet->GetVelocity();S->VelocityR=S->Recruit->GetVelocity();S->LastTime=World->GetTimeSeconds();S->Input=NewObject<UHomeMovementProbe>(Hero);S->Input->RegisterComponent();S->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFCompanionCraftPreview"));if(S->Preview){S->Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("CompanionCraftPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*S->Root,true);UE_LOG(LogTemp,Display,TEXT("CF_COMPANION_CRAFT_PREVIEW_ROOT %s"),*S->Root);}FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){return S->Tick();}));
}

void RunIndoorCraftScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto S=MakeShared<FIndoorCraft>();S->World=World;S->Hero=Hero;S->Read=Read;S->At=World->GetTimeSeconds();S->Deadline=FPlatformTime::Seconds()+180;
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("IndoorCraftInput"),0));if(!Source || Source->FormatVersion!=38 || Source->Facilities.Num()!=2 || Source->HomeModules.Num()!=7 || !Hero->LoadProgress(TEXT("IndoorCraftInput"))){S->End(false,TEXT("source"));return;}
    S->Original=Source->HomeModules;for(const auto& F:Source->Facilities){if(F.Specialty==EWorkSpecialty::Farming)S->Garden=F;if(F.Specialty==EWorkSpecialty::Crafting)S->IndoorId=F.Id;}S->Bag=Hero->Inventory->GetStacks();S->PlayerHealth=Hero->Health;for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController() && It->Definition){if(It->Definition->Form==ECreatureForm::Leaf){S->PetId=It->PersistentId;S->Nickname=It->Nickname;}if(It->Definition->Form==ECreatureForm::Ember)S->RecruitId=It->PersistentId;}S->BindCraft();for(const auto& N:Source->ResourceNodes)S->Resources.Add(N.Id,N.Remaining);if(S->Salvage.IsValid())S->MaterialStart=S->Salvage->Remaining+(Read?2:0);if(S->Fiber.IsValid())S->FiberStart=S->Fiber->Remaining+(Read?2:0);
    if(!S->Budget() || Hero->Inventory->Count(EFrontierItem::Material)!=(Read?2:0) || Hero->Inventory->Count(EFrontierItem::Fiber)!=(Read?2:0) || !Hero->GetActorTransform().Equals(Source->PlayerTransform,.01)){S->End(false,TEXT("source-budget"));return;}
    S->LastH=Hero->GetActorLocation();S->LastP=S->Pet->GetActorLocation();S->LastR=S->Recruit->GetActorLocation();S->VelocityH=Hero->GetVelocity();S->VelocityP=S->Pet->GetVelocity();S->VelocityR=S->Recruit->GetVelocity();S->LastTime=World->GetTimeSeconds();S->Input=NewObject<UHomeMovementProbe>(Hero);S->Input->RegisterComponent();S->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFIndoorCraftPreview"));if(S->Preview){S->Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("IndoorCraftPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*S->Root,true);UE_LOG(LogTemp,Display,TEXT("CF_INDOOR_CRAFT_PREVIEW_ROOT %s"),*S->Root);}FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){return S->Tick();}));
}

void RunStarterWorkshopScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto S=MakeShared<FStarterWorkshop>();S->World=World;S->Hero=Hero;S->Read=Read;S->At=World->GetTimeSeconds();S->Deadline=FPlatformTime::Seconds()+180;
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("StarterWorkshopInput"),0));if(!Source || Source->FormatVersion!=38 || (Source->Facilities.Num()!=1 && !(Read && Source->Facilities.Num()==2)) || Source->HomeModules.Num()!=7 || !Hero->LoadProgress(TEXT("StarterWorkshopInput"))){S->End(false,TEXT("source"));return;}
    S->Original=Source->HomeModules;for(const auto& F:Source->Facilities){if(F.Specialty==EWorkSpecialty::Farming)S->Garden=F;if(F.Specialty==EWorkSpecialty::Crafting){S->IndoorId=F.Id;S->ExpectedPlacement=F.Transform;S->Built=true;}}S->Bag=Hero->Inventory->GetStacks();S->PlayerHealth=Hero->Health;for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController() && It->Definition){if(It->Definition->Form==ECreatureForm::Leaf){S->PetId=It->PersistentId;S->Nickname=It->Nickname;}if(It->Definition->Form==ECreatureForm::Ember)S->RecruitId=It->PersistentId;}S->BindWorkshop();for(const auto& N:Source->ResourceNodes)S->Resources.Add(N.Id,N.Remaining);if(S->Salvage.IsValid())S->SalvageStart=S->Salvage->Remaining+(Read?14:0);
    if(!S->Built && FParse::Param(FCommandLine::Get(),TEXT("CFFacilityRotationTest")))S->ExpectedPlacement.SetRotation(FRotator(0,-90,0).Quaternion());
    if(!S->Budget() || UBaseBuildingComponent::GetCost(EWorkSpecialty::Crafting)!=15 || Hero->Inventory->Count(EFrontierItem::Material)!=(S->Built?0:Read?15:1) || !Hero->GetActorTransform().Equals(Source->PlayerTransform,.01)){S->End(false,TEXT("source-budget"));return;}
    if(S->Built)UE_LOG(LogTemp,Display,TEXT("CF_STARTER_WORKSHOP_BUILD_RESTORED PASS facilities=2 paidCheckpoint=1"));S->LastH=Hero->GetActorLocation();S->LastP=S->Pet->GetActorLocation();S->LastR=S->Recruit->GetActorLocation();S->VelocityH=Hero->GetVelocity();S->VelocityP=S->Pet->GetVelocity();S->VelocityR=S->Recruit->GetVelocity();S->LastTime=World->GetTimeSeconds();S->Input=NewObject<UHomeMovementProbe>(Hero);S->Input->RegisterComponent();S->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFStarterWorkshopPreview"));if(S->Preview){S->Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("StarterWorkshopPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*S->Root,true);UE_LOG(LogTemp,Display,TEXT("CF_STARTER_WORKSHOP_PREVIEW_ROOT %s"),*S->Root);}FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){return S->Tick();}));
}
