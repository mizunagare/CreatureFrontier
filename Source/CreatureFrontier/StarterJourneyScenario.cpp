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
