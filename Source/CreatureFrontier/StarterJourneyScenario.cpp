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
#include "PlayerGatheringComponent.h"
#include "Components/CapsuleComponent.h"
#include "PlayerEquipmentComponent.h"

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
    bool End(bool Pass,const TCHAR* Reason)
    {
        if(Held)CFInputQA::Release(Hero,EKeys::M);if(Input.IsValid())Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::LeftShift);
        const auto Feedback=Hero->Feedback;if(!Pass)Hero->SaveProgress(TEXT("StarterRecoveryBlocked"));
        UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RECOVERY %s reason=%s phase=%d heroTravel=%.1f petTravel=%.1f wildTravel=%.1f heroHP=%.1f petHP=%.1f crafted=%d used=%d feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Reason,Phase,HeroTravel,PetTravel,WildTravel,Hero->Health,Pet.IsValid()?Pet->Health:-1,Crafted,Used,*Feedback);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    bool Budget() const
    {
        if(!Pet.IsValid() || !Brush.IsValid() || !Garden.IsValid())return false;const int32 Harvested=14-Brush->Remaining;
        int32 Farms=0;for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt)++Farms;
        const auto& Gear=Hero->Equipment->GetState();
        return Harvested>=0 && Harvested<=10 && Hero->Inventory->Count(EFrontierItem::Material)+5*Crafted==Harvested && Hero->Inventory->Count(EFrontierItem::Medicine)==Crafted-Used &&
            Hero->Inventory->Count(EFrontierItem::Food)==2 && Hero->Inventory->Count(EFrontierItem::Seeds)==1 && Hero->Inventory->Count(EFrontierItem::CaptureDevice)==7 && Hero->Inventory->Count(EFrontierItem::Fiber)==0 &&
            Pet->Experience==12 && Pet->BondPoints==3 && Pet->Nickname==Nickname && Pet->bAvoidCombat && Gear.bSpearEquipped && Gear.bVestEquipped && Hero->GetObjectives().Claimed==4 && !Hero->GetObjectives().bAlphaResolved &&
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
            if(Hero->Inventory->Count(EFrontierItem::Material)<10){if(!Hero->Gathering->IsGathering() || World->GetTimeSeconds()-At>30)return End(false,TEXT("gather-interrupted"));return true;}
            if(!CFInputQA::Release(Hero,EKeys::M) || Brush->Remaining!=4)return End(false,TEXT("gather-count"));Held=false;
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RECOVERY_GATHER PASS input=1 material=10 sourcePaid=10 experience=12"));Photo(TEXT("Gathered"),Brush->GetActorLocation());Phase=3;return true;
        }
        if(Phase==3){if(!Plan(Bench->GetActorLocation()+FVector(210,-180,40),4))return End(false,TEXT("return-path"));return true;}
        if(Phase==5)
        {
            if(!CFInputQA::Press(Hero,EKeys::Two) || Hero->Inventory->Count(EFrontierItem::Medicine)!=1 || Hero->Inventory->Count(EFrontierItem::Material)!=5)return End(false,TEXT("craft-first"));++Crafted;
            if(!CFInputQA::Press(Hero,EKeys::Two) || Hero->Inventory->Count(EFrontierItem::Medicine)!=2 || Hero->Inventory->Count(EFrontierItem::Material)!=0)return End(false,TEXT("craft-second"));++Crafted;
            if(!Hero->GetObjectiveHint().Contains(TEXT("H治疗自己")))return End(false,TEXT("medicine-hint"));Phase=9;return true;
        }
        if(Phase==9)
        {
            if(Photo(TEXT("MedicineReady"),P))return true;
            if(!CFInputQA::Press(Hero,EKeys::H) || Hero->Health!=100 || Hero->Inventory->Count(EFrontierItem::Medicine)!=1)return End(false,TEXT("heal-self"));++Used;
            if(!CFInputQA::Press(Hero,EKeys::H) || Hero->Inventory->Count(EFrontierItem::Medicine)!=1)return End(false,TEXT("full-health-reject"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RECOVERY_MEDICINE PASS crafted=2 materialPaid=10 selfUsed=1 retained=1 fullHealthReject=1"));Photo(TEXT("Resupplied"),P);Phase=6;At=World->GetTimeSeconds();return true;
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
            for(TActorIterator<AWorkStation> It(World);It;++It)if(It->PersistentId==OriginalGarden.Id)Garden=*It;for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,1))Brush=*It;
            if(!Pet.IsValid() || !Hero->GetActorLocation().Equals(H,.01) || !Pet->GetActorLocation().Equals(P,.01) || Hero->Health!=100 || Pet->Health!=HP || Pet->Stamina!=SP || !Budget())return End(false,TEXT("restore"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RECOVERY_HINTS PASS states=3"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RECOVERY_LIFE PASS version=38 positionRetained=1 heroHP=100 petFull=1 medicine=1 material=0 claimed=4"));return End(true,TEXT("complete"));
        }
        return true;
    }
};
}
void RunStarterRecoveryScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto S=MakeShared<FStarterRecovery>();S->World=World;S->Hero=Hero;S->Read=Read;S->Deadline=FPlatformTime::Seconds()+150;
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("StarterRecoveryInput"),0));
    if(!Source || Source->FormatVersion!=38 || Source->HomeModules.Num()!=0 || Source->Facilities.Num()!=1 || !Hero->LoadProgress(TEXT("StarterRecoveryInput"))){S->End(false,TEXT("source"));return;}
    S->OriginalGarden=Source->Facilities[0];int32 Owned=0;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController()){S->Pet=*It;++Owned;}
    for(TActorIterator<AWorkStation> It(World);It;++It){if(It->PersistentId==S->OriginalGarden.Id)S->Garden=*It;if(!It->bPlayerBuilt && It->RequiredSpecialty==EWorkSpecialty::Gathering)S->Bench=*It;}
    for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,1))S->Brush=*It;
    if(Owned!=1 || !S->Pet.IsValid() || !S->Bench.IsValid() || !S->Brush.IsValid() || S->Brush->Remaining!=14 || Hero->Health>=100 || S->Pet->Health<=0 || S->Pet->Health>=S->Pet->GetMaxHealth() || (Read && (S->Pet->Activity!=ECreatureActivity::Rest || S->Pet->WorkSite!=S->Bench.Get()))){S->End(false,TEXT("source-state"));return;}
    S->PetId=S->Pet->PersistentId;S->Nickname=S->Pet->Nickname;S->InitialHealth=Hero->Health;const auto* Partner=Source->Creatures.FindByPredicate([&](const FCreatureSaveRecord& R){return R.Id==S->PetId;});
    const auto* Catalog=UCraftingCatalog::Load();const auto* Recipe=Catalog?Catalog->Find(TEXT("Medicine")):nullptr;
    if(!Partner || !Hero->GetActorLocation().Equals(Source->PlayerTransform.GetLocation(),.01) || !S->Pet->GetActorLocation().Equals(Partner->Transform.GetLocation(),.01) || !Recipe || Recipe->Inputs.Num()!=1 || Recipe->Inputs.FindRef(EFrontierItem::Material)!=5 || Recipe->OutputAmount!=1 || !S->Budget()){S->End(false,TEXT("source-position-budget"));return;}
    if(!Hero->GetObjectiveHint().Contains(TEXT("缺药"))){S->End(false,TEXT("source-recovery-hint"));return;}
    S->LastH=Hero->GetActorLocation();S->LastP=S->Pet->GetActorLocation();S->VelocityH=Hero->GetVelocity();S->VelocityP=S->Pet->GetVelocity();S->LastTime=S->At=World->GetTimeSeconds();S->Input=NewObject<UHomeMovementProbe>(Hero);S->Input->RegisterComponent();
    S->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFStarterRecoveryPreview"));if(S->Preview){S->Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("StarterRecoveryPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*S->Root,true);UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RECOVERY_PREVIEW_ROOT %s"),*S->Root);}
    UE_LOG(LogTemp,Display,TEXT("CF_STARTER_RECOVERY_SOURCE PASS read=%d positionRetained=1 pet=%s"),Read,*S->PetId.ToString());FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){return S->Tick();}));
}

namespace
{
// 沿真实导航绕行旧烬尾兽出生点；所有敌人仍正常警戒、追击、攻击和归巢。
struct FStarterFlank : FStarterRecovery
{
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
        if(Ecology!=3 || Owned!=1 || !Budget() || Boss->Health<=0 || !Boss->bAlpha)return End(false,TEXT("budget-home"));
        if(Pet->SpecialWindupRemaining>0)SkillStarted=true;
        if(Phase==2 && Boss->Health<Boss->GetMaxHealth()){Hit=true;DamageSeen=FMath::Max(DamageSeen,Boss->GetMaxHealth()-Boss->Health);}
        if(World->GetTimeSeconds()>=NextDiagnostic){NextDiagnostic=World->GetTimeSeconds()+3;UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FLANK_STATE phase=%d leg=%d hero=%s pet=%s boss=%s heroHP=%.1f petHP=%.1f bossHP=%.1f activity=%d stamina=%.1f windup=%.2f"),Phase,Leg,*H.ToString(),*P.ToString(),*Boss->GetActorLocation().ToString(),Hero->Health,Pet->Health,Boss->Health,static_cast<int32>(Pet->Activity),Pet->Stamina,Pet->SpecialWindupRemaining);}
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
            if(!Hit){if(World->GetTimeSeconds()-At>15)return End(false,TEXT("combat-no-hit"));return true;}
            if(Photo(TEXT("Engaged"),Boss->GetActorLocation()))return true;
            if(!CFInputQA::Press(Hero,EKeys::F) || Pet->FocusTarget || !Pet->bAvoidCombat || !Hero->SaveProgress(TEXT("StarterFlankMid")))return End(false,TEXT("withdraw-input"));
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
            Pet.Reset();Garden.Reset();Brush.Reset();Boss.Reset();for(TActorIterator<ACreatureCharacter> It(World);It;++It){if(It->PersistentId==PetId)Pet=*It;if(It->EncounterId==TEXT("CrystalRise") && It->bAlpha && It->Health>0)Boss=*It;}
            for(TActorIterator<AWorkStation> It(World);It;++It)if(It->PersistentId==OriginalGarden.Id)Garden=*It;for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,1))Brush=*It;
            if(!Pet.IsValid() || !Boss.IsValid() || Boss->Health!=BHP || !Hero->GetActorLocation().Equals(H,.01) || !Pet->GetActorLocation().Equals(P,.01) || Hero->Health!=HP || Pet->Health!=PHP || !Budget())return End(false,TEXT("restore"));
            UE_LOG(LogTemp,Display,TEXT("CF_STARTER_FLANK_LIFE PASS version=38 positionRetained=1 homeRetained=1 medicine=1 claimed=4 safeSeconds=2"));return End(true,TEXT("complete"));
        }
        return true;
    }
};
}
void RunStarterFlankScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto S=MakeShared<FStarterFlank>();S->World=World;S->Hero=Hero;S->Read=Read;S->Deadline=FPlatformTime::Seconds()+150;
    // 只继承真实恢复档中既有的材料账：此前制2药、用1药，本段不再次制作。
    S->Crafted=2;S->Used=1;
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("StarterFlankInput"),0));
    if(!Source || Source->FormatVersion!=38 || Source->Facilities.Num()!=1 || !Source->HomeModules.IsEmpty() || !Hero->LoadProgress(TEXT("StarterFlankInput"))){S->End(false,TEXT("source"));return;}
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
