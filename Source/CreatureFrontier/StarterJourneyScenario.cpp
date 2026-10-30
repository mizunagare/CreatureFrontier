#include "ConstructionScenario.h"
#include "CFPlayerCharacter.h"
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
#include "PlayerGatheringComponent.h"
#include "Components/CapsuleComponent.h"

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
