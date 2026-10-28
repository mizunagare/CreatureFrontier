#include "HollowScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "ResourceNode.h"
#include "ResourceDefinition.h"
#include "InventoryComponent.h"
#include "ResearchComponent.h"
#include "FrontierSaveGame.h"
#include "WildEncounter.h"
#include "HomeScenario.h"
#include "InputRoutingQA.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "HAL/PlatformMisc.h"
#include "Containers/Ticker.h"
#include "Algo/Reverse.h"
#include "GameFramework/Controller.h"
#include "NavMesh/NavMeshBoundsVolume.h"

namespace CFHollowQA
{
AResourceNode* Node(UWorld* World,uint32 Index)
{for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,Index))return *It;return nullptr;}
ACreatureCharacter* Owned(UWorld* World,ACFPlayerCharacter* Hero)
{for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController())return *It;return nullptr;}
bool Snapshot(UWorld* World,ACFPlayerCharacter* Hero)
{
    auto* Cache=Node(World,11);auto* Fiber=Node(World,8);auto* Pet=Owned(World,Hero);
    int32 Count=0;for(TActorIterator<AResourceNode> It(World);It;++It)++Count;
    return Count==11 && Cache && Cache->Remaining==0 && Cache->RegrowRemaining==0 && Fiber && Fiber->Remaining==2 &&
        Hero->Inventory->Count(EFrontierItem::Scrap)==4 && Hero->Inventory->Count(EFrontierItem::ResonanceDevice)==2 &&
        Hero->Inventory->Count(EFrontierItem::Crystal)==0 && Hero->Inventory->Count(EFrontierItem::Material)==4 && Pet && Pet->PersistentId.IsValid();
}
bool Finish(UWorld* World,ACFPlayerCharacter* Hero,const TCHAR* Slot)
{
    if(!Hero->CraftRecipe(TEXT("ScrapResonator")) || !Snapshot(World,Hero))return false;
    const FGuid Id=Owned(World,Hero)->PersistentId;
    return Hero->SaveProgress(Slot) && Hero->LoadProgress(Slot) && Snapshot(World,Hero) && Owned(World,Hero)->PersistentId==Id;
}
struct FRoute
{
    UWorld* World;ACFPlayerCharacter* Hero;ACreatureCharacter* Pet;UHomeMovementProbe* Movement;
    TArray<FVector> Points;int32 Index=0,Harvested=0;bool Returning=false;double Deadline=0;FVector Previous;float Travel=0,PetSouth=0;
    bool Tick()
    {
        const FVector Now=Hero->GetActorLocation();Travel+=FVector::Dist2D(Now,Previous);const float Step=FVector::Dist(Now,Previous);Previous=Now;
        PetSouth=FMath::Min(PetSouth,Pet->GetActorLocation().Y);
        if(FPlatformTime::Seconds()>Deadline || Step>160 || Now.Z<0 || Pet->GetActorLocation().Z<-10 ||
            Hero->GetCapsuleComponent()->GetCollisionEnabled()==ECollisionEnabled::NoCollision || Pet->GetCapsuleComponent()->GetCollisionEnabled()==ECollisionEnabled::NoCollision)
        {UE_LOG(LogTemp,Error,TEXT("CF_HOLLOW_ROUTE FAIL index=%d return=%d hero=%s pet=%s step=%.1f"),Index,Returning,*Now.ToString(),*Pet->GetActorLocation().ToString(),Step);FPlatformMisc::RequestExitWithStatus(false,1);return false;}
        if(Index<Points.Num())
        {
            const auto Delta=Points[Index]-Now;
            if(Delta.Size2D()<70){++Index;Movement->Direction=FVector::ZeroVector;return true;}
            Movement->Direction=FVector(Delta.X,Delta.Y,0).GetSafeNormal()*.45f;
            Hero->SetActorRotation(Movement->Direction.Rotation());Hero->GetController()->SetControlRotation(Movement->Direction.Rotation());return true;
        }
        Movement->Direction=FVector::ZeroVector;
        if(!Returning)
        {
            auto* Cache=Node(World,11);if(!Cache){FPlatformMisc::RequestExitWithStatus(false,1);return false;}
            if(Harvested<6)
            {
                if(Cache->HarvestCooldown>0)return true;
                const auto View=(Cache->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
                const int32 Before=Hero->Inventory->Count(EFrontierItem::Scrap);
                if(!CFInputQA::Press(Hero,EKeys::M) || Hero->Inventory->Count(EFrontierItem::Scrap)!=Before+1){UE_LOG(LogTemp,Error,TEXT("CF_HOLLOW_ROUTE FAIL harvest %s"),*Hero->Feedback);FPlatformMisc::RequestExitWithStatus(false,1);return false;}
                ++Harvested;return true;
            }
            if(PetSouth>-3600 || Pet->GetDistanceTo(Hero)>330)return true;
            Algo::Reverse(Points);Points.Add(FVector(-250,-400,40));Index=0;Returning=true;return true;
        }
        if(Pet->GetDistanceTo(Hero)>300)return true;
        const bool Pass=Travel>7000 && PetSouth<-3600 && Finish(World,Hero,TEXT("HollowRoute"));
        UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_ROUTE %s travel=%.1f petSouth=%.1f harvested=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Travel,PetSouth,Harvested);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
};
}
void RunHollowScenario(UWorld* World,ACFPlayerCharacter* Hero,int Mode)
{
    if(Mode==4){RunHollowPreview(World,Hero);return;}
    using namespace CFHollowQA;
    if(Mode==2)
    {const bool Pass=Hero->LoadProgress(TEXT("HollowCross")) && Snapshot(World,Hero);UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_READ %s"),Pass?TEXT("PASS"):TEXT("FAIL"));FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return;}
    auto* Cache=Node(World,11);auto* Fiber=Node(World,8);AWildEncounter* Camp=nullptr;
    for(TActorIterator<AWildEncounter> It(World);It;++It)if(It->EncounterId==TEXT("EmberHollow"))Camp=*It;
    auto* Leaf=LoadObject<UCreatureDefinition>(nullptr,TEXT("/Game/Creatures/DA_Mosskip.DA_Mosskip"));
    if(!Cache || !Fiber || !Camp || !Leaf || Camp->bBoss || Camp->CreatureLevel!=3 || !Camp->Definition || Camp->Definition->Form!=ECreatureForm::Ember || Cache->Remaining!=6 || Cache->Definition->OutputItem!=EFrontierItem::Scrap)
    {UE_LOG(LogTemp,Error,TEXT("CF_HOLLOW FAIL fixture"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)It->Destroy();
    Hero->SetActorLocation(FVector(-250,-400,40));Hero->Inventory->Restore({{EFrontierItem::Material,4},{EFrontierItem::Crystal,1},{EFrontierItem::Food,9}});
    FResearchProgress Study;Study.bCrystalSample=true;Study.bCrystalStudy=true;Hero->Research->Restore(Study);Fiber->Remaining=2;Fiber->Refresh();
    const FTransform At(Hero->GetActorLocation()+FVector(0,180,0));auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),At);Pet->Definition=Leaf;Pet->FinishSpawning(At);Pet->Tame(Hero->GetController());
    if(Mode==3)
    {
        auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
        auto* Path=Nav?Nav->FindPathToLocationSynchronously(World,Hero->GetActorLocation(),FVector(-550,-3930,40),Hero):nullptr;
        UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_NAV system=%d building=%d path=%d valid=%d partial=%d points=%d"),Nav!=nullptr,UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World),Path!=nullptr,Path && Path->IsValid(),Path && Path->IsPartial(),Path?Path->PathPoints.Num():0);
        for(TActorIterator<ANavMeshBoundsVolume> It(World);It;++It)UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_NAV bounds location=%s min=%s max=%s"),*It->GetActorLocation().ToString(),*It->GetComponentsBoundingBox(true).Min.ToString(),*It->GetComponentsBoundingBox(true).Max.ToString());
        FVector Previous=Hero->GetActorLocation();
        for(const FVector Point:{FVector(-250,-400,40),FVector(-450,-1100,40),FVector(0,-1750,40),FVector(0,-2300,40),FVector(260,-2850,40),FVector(260,-3250,40),FVector(-300,-3350,40),FVector(-550,-3930,40)})
        {
            FNavLocation Projected;const bool Found=Nav && Nav->ProjectPointToNavigation(Point,Projected,FVector(80,80,200));
            auto* Segment=Nav?Nav->FindPathToLocationSynchronously(World,Previous,Point,Hero):nullptr;
            UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_NAV input=%s found=%d projected=%s segment=%d partial=%d points=%d"),*Point.ToString(),Found,*Projected.Location.ToString(),Segment && Segment->IsValid(),Segment && Segment->IsPartial(),Segment?Segment->PathPoints.Num():0);Previous=Point;
        }
        if(!Path || !Path->IsValid() || Path->IsPartial() || Path->PathPoints.Num()<4){UE_LOG(LogTemp,Error,TEXT("CF_HOLLOW_ROUTE FAIL path"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
        auto State=MakeShared<FRoute>();State->World=World;State->Hero=Hero;State->Pet=Pet;State->Movement=NewObject<UHomeMovementProbe>(Hero);State->Movement->RegisterComponent();
        State->Points={FVector(-450,-1100,40),FVector(0,-1750,40),FVector(0,-2300,40),FVector(0,-2700,40),FVector(260,-2850,40),FVector(260,-3250,40),FVector(-300,-3350,40),FVector(-550,-3750,40),FVector(-550,-3930,40)};
        State->Previous=Hero->GetActorLocation();State->Deadline=FPlatformTime::Seconds()+55;
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float){return State->Tick();}),.1f);return;
    }
    Hero->GetCharacterMovement()->DisableMovement();Pet->SetActorTickEnabled(false);
    bool Geometry=true;FHitResult GeometryHit;FCollisionQueryParams GeometryQuery(SCENE_QUERY_STAT(HollowBoundary),false,Hero);GeometryQuery.AddIgnoredActor(Pet);
    for(const FVector Point:{FVector(0,-2450,0),FVector(-800,-3800,0),FVector(800,-3800,0),FVector(-800,-4250,0),FVector(800,-4250,0)})
        Geometry &= World->LineTraceSingleByChannel(GeometryHit,Point,Point-FVector(0,0,200),ECC_Visibility,GeometryQuery) && FMath::IsNearlyEqual(GeometryHit.Location.Z,-50.f,2.f);
    for(const auto Segment:TArray<TPair<FVector,FVector>>{{FVector(0,-2450,100),FVector(-800,-2450,100)},{FVector(0,-2450,100),FVector(800,-2450,100)},{FVector(0,-4100,100),FVector(0,-4600,100)},{FVector(0,-3800,100),FVector(-1200,-3800,100)},{FVector(0,-3800,100),FVector(1200,-3800,100)},{FVector(800,-2900,100),FVector(800,-2450,100)}})
        Geometry &= World->LineTraceSingleByChannel(GeometryHit,Segment.Key,Segment.Value,ECC_Visibility,GeometryQuery);
    Geometry &= !World->LineTraceSingleByChannel(GeometryHit,FVector(0,-1900,100),FVector(0,-2300,100),ECC_Visibility,GeometryQuery);
    bool Migration=Hero->SaveProgress(TEXT("HollowInitial"));auto* Old=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("HollowInitial"),0));
    if(!Old){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    Old->FormatVersion=27;Old->ResourceNodes.RemoveAll([](const auto& R){return R.Id==FGuid(0xCF170000,0,0,11);});Old->EncounterTimers.Remove(TEXT("EmberHollow"));
    Migration &= UGameplayStatics::SaveGameToSlot(Old,TEXT("HollowLegacy27"),0) && Hero->LoadProgress(TEXT("HollowLegacy27")) && Node(World,11) && Node(World,11)->Remaining==6 && Node(World,8)->Remaining==2;
    Migration &= Hero->SaveProgress(TEXT("HollowMigrated"));auto* Missing=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("HollowMigrated"),0));
    if(!Missing){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    Missing->ResourceNodes.RemoveAll([](const auto& R){return R.Id==FGuid(0xCF170000,0,0,11);});
    Migration &= Missing->FormatVersion==28 && UGameplayStatics::SaveGameToSlot(Missing,TEXT("HollowMissing28"),0) && Hero->LoadProgress(TEXT("HollowMissing28")) && !Node(World,11) && Hero->LoadProgress(TEXT("HollowMissing28")) && !Node(World,11);
    Migration &= Hero->LoadProgress(TEXT("HollowMigrated"));Cache=Node(World,11);
    if(!Cache){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    bool Harvest=Cache && !Cache->TryHarvest(Hero,Hero);Hero->SetActorLocation(FVector(-550,-3930,40));
    for(int32 I=0;I<6 && Cache;++I){Harvest &= Cache->TryHarvest(Hero,Hero);Cache->Tick(.81f);}
    Harvest &= Cache && !Cache->TryHarvest(Hero,Hero) && Hero->Inventory->Count(EFrontierItem::Scrap)==6;Cache->Tick(1000);Harvest &= Cache->Remaining==0;
    Hero->SetActorLocation(FVector(-250,-400,40));const bool Saved=Finish(World,Hero,TEXT("HollowCross"));
    const bool Pass=Geometry && Migration && Harvest && Saved;
    UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW%s %s geometry=%d migration=%d harvest=%d saved=%d"),Mode==1?TEXT("_WRITE"):TEXT(""),Pass?TEXT("PASS"):TEXT("FAIL"),Geometry,Migration,Harvest,Saved);
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}
