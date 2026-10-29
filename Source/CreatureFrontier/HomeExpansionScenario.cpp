#include "HomeExpansionScenario.h"
#include "CFPlayerCharacter.h"
#include "HomeLayout.h"
#include "HomeModule.h"
#include "InventoryComponent.h"
#include "CreatureCharacter.h"
#include "ResourceNode.h"
#include "ResourceDefinition.h"
#include "WorkStation.h"
#include "FarmCropComponent.h"
#include "EngineUtils.h"
#include "HomeScenario.h"
#include "HomeBuildingComponent.h"
#include "BaseBuildingComponent.h"
#include "PlayerGatheringComponent.h"
#include "PlayerPaceComponent.h"
#include "CompanionCargoComponent.h"
#include "StorageInteractionComponent.h"
#include "FrontierSaveGame.h"
#include "InputRoutingQA.h"
#include "WildEncounter.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Containers/Ticker.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
bool PlanExpansion(UWorld* World,ACFPlayerCharacter* Hero,bool Funded=false)
{
    const auto Original=HomeLayout::Collect(World);
    if(Original.Num()!=14 || Hero->Inventory->Count(EFrontierItem::Material)!=(Funded?74:0) || Hero->Inventory->Count(EFrontierItem::Food)!=0 || Hero->Inventory->Count(EFrontierItem::Seeds)!=2)return false;
    auto Find=[&](EHomeModuleKind Kind,FVector Position)->const FHomeModuleRecord*
    {return Original.FindByPredicate([&](const FHomeModuleRecord& R){return R.Definition.Get()->Kind==Kind && R.Transform.GetLocation().Equals(Position,.01);});};
    const auto* Ground=Find(EHomeModuleKind::Floor,FVector(-400,-800,-40));
    const auto* Court=Find(EHomeModuleKind::Floor,FVector(0,-800,-40));
    const auto* Roof=Find(EHomeModuleKind::Roof,FVector(-400,-800,280));
    const auto* Door=Find(EHomeModuleKind::Door,FVector(-200,-800,-30));
    const auto* South=Find(EHomeModuleKind::Wall,FVector(-400,-1000,-30));
    const auto* Box=Find(EHomeModuleKind::Storage,FVector(-900,-900,-30));
    if(!Ground || !Court || !Roof || !Door || !South || !Box || Box->StoredItems.FindRef(EFrontierItem::Material)!=(Funded?0:3))return false;
    TArray<FHomeModuleRecord> Layout=Original;
    Layout.RemoveAll([&](const FHomeModuleRecord& R){return R.Id==Roof->Id;});
    int32 Cost=18;
    auto Add=[&](EHomeModuleKind Kind,FVector Position,FGuid Parent,double Yaw=0)->FGuid
    {
        FHomeModuleRecord R;R.Id=FGuid::NewGuid();R.Definition=UHomeModuleDefinition::LoadDefault(Kind);R.FloorId=Parent;
        R.Transform=FTransform(FRotator(0,Yaw,0),Position);R.PaidMaterials=R.Definition.Get()->MaterialCost;R.PaidExtraItems=R.Definition.Get()->ExtraCosts;R.DoorState=Kind==EHomeModuleKind::Door?1:0;
        Cost+=R.PaidMaterials;Layout.Add(R);return R.Id;
    };
    const auto Upper=Add(EHomeModuleKind::Floor,FVector(-400,-800,280),Ground->Id);
    Add(EHomeModuleKind::Stairs,FVector(0,-800,-30),Court->Id,180);
    FString Reason;
    if(HomeLayout::Validate(Layout,Reason))return false;
    UE_LOG(LogTemp,Display,TEXT("CF_HOME_EXPAND_DOOR_CONFLICT PASS rejected=1 reason=%s"),*Reason);
    // 真实改建必须移除冲突旧门并另开入口，不能忽略门扇或楼梯的碰撞。
    Layout.RemoveAll([&](const FHomeModuleRecord& R){return R.Id==Door->Id;});
    auto* Entrance=Layout.FindByPredicate([&](const FHomeModuleRecord& R){return R.Id==South->Id;});
    Entrance->Definition=UHomeModuleDefinition::LoadDefault(EHomeModuleKind::Door);Entrance->PaidMaterials=Entrance->Definition.Get()->MaterialCost;Entrance->PaidExtraItems=Entrance->Definition.Get()->ExtraCosts;Entrance->DoorState=1;Cost+=Entrance->PaidMaterials;
    Add(EHomeModuleKind::Wall,FVector(-400,-1000,290),Upper);
    Add(EHomeModuleKind::Wall,FVector(-400,-600,290),Upper);
    Add(EHomeModuleKind::Wall,FVector(-600,-800,290),Upper,90);
    Add(EHomeModuleKind::Doorway,FVector(-200,-800,290),Upper,90);
    Add(EHomeModuleKind::Roof,FVector(-400,-800,600),Upper);
    if(!HomeLayout::Validate(Layout,Reason) || !HomeLayout::SupportsFacility(FTransform(FRotator::ZeroRotator,FVector(-400,-700,325),FVector(2,1,.7)),Layout))
    {UE_LOG(LogTemp,Error,TEXT("CF_HOME_EXPAND_LAYOUT FAIL reason=%s"),*Reason);return false;}
    const int32 Refund=Roof->PaidMaterials/2+Door->PaidMaterials/2+South->PaidMaterials/2;
    if(Layout.Num()!=19 || Cost!=86 || Refund!=14)return false;
    int32 Owned=0,Farms=0;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController())
    {++Owned;if(It->BondPoints!=6 || It->Activity!=ECreatureActivity::Follow)return false;UE_LOG(LogTemp,Display,TEXT("CF_HOME_EXPAND_ORIGIN pet=%s xp=%d nutrition=%.2f stamina=%.2f position=%s"),*It->PersistentId.ToString(),It->Experience,It->Nutrition,It->Stamina,*It->GetActorLocation().ToString());}
    for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt){++Farms;if(It->RequiredSpecialty!=EWorkSpecialty::Farming || It->StoredFood!=0 || It->Crop->Seeds!=0)return false;}
    for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId.A==0xCF170000)
        UE_LOG(LogTemp,Display,TEXT("CF_HOME_EXPAND_SOURCE id=%u remaining=%d regrow=%.2f item=%d position=%s"),It->PersistentId.D,It->Remaining,It->RegrowRemaining,static_cast<int32>(It->Definition->OutputItem),*It->GetActorLocation().ToString());
    if(Owned!=1 || Farms!=1)return false;
    if(Funded){UE_LOG(LogTemp,Display,TEXT("CF_HOME_EXPAND_PLAN_FUNDED PASS modules=19 cost=86 refund=14 material=74 stored=0 original=14"));}
    else{UE_LOG(LogTemp,Display,TEXT("CF_HOME_EXPAND_PLAN PASS modules=19 cost=86 refund=14 stored=3 required=69 original=14"));}return true;
}
struct FExpansionTest
{
    UWorld* World;ACFPlayerCharacter* Hero;TWeakObjectPtr<ACreatureCharacter> Pet;TWeakObjectPtr<AHomeModule> Box;TWeakObjectPtr<AWorkStation> UpperFarm;
    TWeakObjectPtr<UHomeMovementProbe> Input;FGuid PetId,BoxId,OriginalFarmId;FTransform OriginalFarmPose;TArray<FHomeModuleRecord> Original,Expanded;TArray<FVector> Points;
    bool FundedRestore=false,ColdRead=false;
    int32 Phase=0,Step=0,Point=0,Before=0,StartXP=0;bool Gathering=false,Planned=false,Started=false,SawCargo=false,HeroUp=false,PetUp=false,PetDown=false;
    FVector PreviousHero,PreviousPet,Anchor;double HeroTravel=0,PetTravel=0,ProgressAt=0,At=0,Deadline=0,StartRegrow=0,StartedAt=0;
    const int32 SourceIds[4]={6,5,8,1},Amounts[4]={11,30,4,21};
    AResourceNode* Node(int32 Id) const {for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,Id))return *It;return nullptr;}
    void Face(FVector Target){const auto R=(Target-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->SetActorRotation(FRotator(0,R.Yaw,0));Hero->FirstPersonCamera->SetRelativeRotation(FRotator::ZeroRotator);Hero->GetController()->SetControlRotation(R);Hero->FirstPersonCamera->SetWorldRotation(R);}
    void Stop(){if(Input.IsValid())Input->Direction=FVector::ZeroVector;Hero->GetCharacterMovement()->StopMovementImmediately();Hero->ConsumeMovementInputVector();}
    bool Finish(bool Pass,const TCHAR* Reason)
    {
        Stop();Hero->Gathering->Stop();if(Input.IsValid())Input->DestroyComponent();CFInputQA::Release(Hero,EKeys::LeftShift);
        UE_LOG(LogTemp,Display,TEXT("CF_HOME_EXPAND %s reason=%s phase=%d step=%d material=%d heroTravel=%.1f petTravel=%.1f pet=%s hero=%s partner=%s feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Reason,Phase,Step,Hero->Inventory->Count(EFrontierItem::Material),HeroTravel,PetTravel,*PetId.ToString(),*Hero->GetActorLocation().ToString(),Pet.IsValid()?*Pet->GetActorLocation().ToString():TEXT("无"),*Hero->Feedback);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    void Route(TArray<FVector> NewPoints){Points=MoveTemp(NewPoints);Point=0;Planned=true;Anchor=Hero->GetActorLocation();ProgressAt=World->GetTimeSeconds();}
    bool Plan(AResourceNode* Source,FVector Fixed=FVector::ZeroVector)
    {
        auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);if(!Nav)return false;double Best=DBL_MAX;TArray<FVector> BestPoints;
        for(int32 Index=0;Index<(Source?8:1);++Index)
        {
            FVector Goal=Source?Source->GetActorLocation()+FVector(FMath::Cos(Index*PI/4)*240,FMath::Sin(Index*PI/4)*240,40):Fixed;
            FNavLocation Projected;if(!Nav->ProjectPointToNavigation(Goal,Projected,FVector(50,50,120))){UE_LOG(LogTemp,Display,TEXT("CF_HOME_EXPAND_PATH_REJECT index=%d reason=projection goal=%s"),Index,*Goal.ToString());continue;}
            Goal=Projected.Location+FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
            FCollisionQueryParams Q(SCENE_QUERY_STAT(ExpansionStand),false,Hero);Q.AddIgnoredActor(Pet.Get());
            // 核对两个真实胶囊站位和伙伴完整路径，不要求周围整块460厘米正方形没有任何物体。
            const auto* Shape=Hero->GetCapsuleComponent();
            if(World->OverlapBlockingTestByChannel(Goal,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Shape->GetScaledCapsuleRadius()-1,Shape->GetScaledCapsuleHalfHeight()-1),Q)){UE_LOG(LogTemp,Display,TEXT("CF_HOME_EXPAND_PATH_REJECT index=%d reason=player-space goal=%s"),Index,*Goal.ToString());continue;}
            if(Source){FHitResult Hit;if(World->LineTraceSingleByChannel(Hit,Goal,Source->GetActorLocation(),ECC_Visibility,Q) && Hit.GetActor()!=Source){UE_LOG(LogTemp,Display,TEXT("CF_HOME_EXPAND_PATH_REJECT index=%d reason=sight actor=%s goal=%s"),Index,*GetNameSafe(Hit.GetActor()),*Goal.ToString());continue;}}
            if(Source)
            {
                FNavLocation PartnerFloor;const auto Away=(Goal-Source->GetActorLocation()).GetSafeNormal2D();
                if(!Nav->ProjectPointToNavigation(Goal+Away*170,PartnerFloor,FVector(50,50,120)))continue;
                const auto* PetShape=Pet->GetCapsuleComponent();const auto PartnerGoal=PartnerFloor.Location+FVector(0,0,PetShape->GetScaledCapsuleHalfHeight());
                if(World->OverlapBlockingTestByChannel(PartnerGoal,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(PetShape->GetScaledCapsuleRadius()-1,PetShape->GetScaledCapsuleHalfHeight()-1),Q))continue;
                auto* PartnerPath=UNavigationSystemV1::FindPathToLocationSynchronously(World,Pet->GetActorLocation(),PartnerFloor.Location,Pet.Get());
                if(!PartnerPath || !PartnerPath->IsValid() || PartnerPath->IsPartial())continue;
            }
            auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Hero->GetActorLocation(),Goal,Hero);
            if(Path && Path->IsValid() && !Path->IsPartial() && Path->PathPoints.Num()>1 && Path->GetPathLength()<Best){Best=Path->GetPathLength();BestPoints=Path->PathPoints;}
            else if(!Path || !Path->IsValid() || Path->IsPartial() || Path->PathPoints.Num()<2)UE_LOG(LogTemp,Display,TEXT("CF_HOME_EXPAND_PATH_REJECT index=%d reason=path valid=%d partial=%d points=%d goal=%s"),Index,Path && Path->IsValid(),Path && Path->IsPartial(),Path?Path->PathPoints.Num():0,*Goal.ToString());
        }
        if(BestPoints.IsEmpty())return false;Route(MoveTemp(BestPoints));Point=1;return true;
    }
    int32 Move()
    {
        const auto Now=Hero->GetActorLocation();while(Point<Points.Num() && FVector::Dist2D(Now,Points[Point])<12)++Point;
        if(Point==Points.Num()){Stop();return 1;}const auto Direction=(Points[Point]-Now).GetSafeNormal2D();Face(Now+Direction*500+FVector(0,0,64));Input->Direction=Direction;
        if(FVector::Dist2D(Now,Anchor)>20){Anchor=Now;ProgressAt=World->GetTimeSeconds();}
        if(World->GetTimeSeconds()-ProgressAt>5){UE_LOG(LogTemp,Error,TEXT("CF_HOME_EXPAND_BLOCKED phase=%d point=%d target=%s"),Phase,Point,*Points[Point].ToString());return -1;}return 0;
    }
    bool BoxRoute(){if(!Plan(nullptr,FVector(500,-800,40)))return false;Points.Append({FVector(80,-800,60),FVector(-400,-800,60),FVector(-800,-800,60)});return true;}
    void Next(int32 Value){Phase=Value;Planned=false;Started=false;At=World->GetTimeSeconds();}
    AHomeModule* Module(EHomeModuleKind Kind,FVector Position) const {for(TActorIterator<AHomeModule> It(World);It;++It)if(!It->bPreview && It->GetKind()==Kind && It->GetActorLocation().Equals(Position,.01))return *It;return nullptr;}
    bool Build()
    {
        auto* Roof=Module(EHomeModuleKind::Roof,FVector(-400,-800,280));auto* East=Module(EHomeModuleKind::Door,FVector(-200,-800,-30));auto* South=Module(EHomeModuleKind::Wall,FVector(-400,-1000,-30));
        auto* Ground=Module(EHomeModuleKind::Floor,FVector(-400,-800,-40));auto* Court=Module(EHomeModuleKind::Floor,FVector(0,-800,-40));
        if(!Roof || !East || !South || !Ground || !Court || Hero->Inventory->Count(EFrontierItem::Material)!=74)return false;
        const auto RoofId=Roof->PersistentId,EastId=East->PersistentId,SouthId=South->PersistentId;
        // 建造仍使用明确观察站位；资源阶段和施工后往返分别检查连续移动，不设置伙伴位置或暂停其Tick。
        Hero->SetActorLocation(FVector(-100,-1200,60));
        if(!Hero->HomeBuilding->Dismantle(Roof) || !Hero->HomeBuilding->Dismantle(East))return false;
        Hero->HomeBuilding->Selected=EHomeModuleKind::Door;if(!Hero->HomeBuilding->ReplaceWall(South))return false;
        auto Place=[&](EHomeModuleKind Kind,FVector Position,FGuid Parent,double Yaw=0)->AHomeModule*
        {Hero->SetActorLocation(Position+FVector(0,-350,100));Hero->HomeBuilding->Selected=Kind;return Hero->HomeBuilding->BuildAt(FTransform(FRotator(0,Yaw,0),Position),Parent);};
        auto* Upper=Place(EHomeModuleKind::Floor,FVector(-400,-800,280),Ground->PersistentId);if(!Upper)return false;const auto Id=Upper->PersistentId;
        if(!Place(EHomeModuleKind::Stairs,FVector(0,-800,-30),Court->PersistentId,180) || !Place(EHomeModuleKind::Wall,FVector(-400,-1000,290),Id) ||
            !Place(EHomeModuleKind::Wall,FVector(-400,-600,290),Id) || !Place(EHomeModuleKind::Wall,FVector(-600,-800,290),Id,90) ||
            !Place(EHomeModuleKind::Doorway,FVector(-200,-800,290),Id,90) || !Place(EHomeModuleKind::Roof,FVector(-400,-800,600),Id))return false;
        Hero->SetActorLocation(FVector(-100,-1200,380));Hero->Building->Selected=EWorkSpecialty::Farming;UpperFarm=Hero->Building->BuildAt(FTransform(FRotator::ZeroRotator,FVector(-400,-700,325),FVector(2,1,.7)));
        Expanded=HomeLayout::Collect(World);if(!UpperFarm.IsValid() || Expanded.Num()!=19 || Hero->Inventory->Count(EFrontierItem::Material)!=2)return false;
        int32 Kept=0;for(const auto& Old:Original)
        {
            const auto* Live=Expanded.FindByPredicate([&](const FHomeModuleRecord& R){return R.Id==Old.Id;});
            if(Old.Id==RoofId || Old.Id==EastId){if(Live)return false;continue;}
            if(!Live || Live->FloorId!=Old.FloorId || !Live->Transform.Equals(Old.Transform,.01))return false;++Kept;
            if(Old.Id==SouthId){if(Live->Definition.Get()->Kind!=EHomeModuleKind::Door || Live->PaidMaterials!=12 || Live->DoorState!=1)return false;}
            else if(Live->Definition!=Old.Definition || Live->PaidMaterials!=Old.PaidMaterials || Live->DoorState!=Old.DoorState || !Live->PaidExtraItems.OrderIndependentCompareEqual(Old.PaidExtraItems))return false;
            if(Old.Id==BoxId){for(const auto& Stack:Live->StoredItems)if(Stack.Value!=0)return false;}
            else if(!Live->StoredItems.OrderIndependentCompareEqual(Old.StoredItems))return false;
        }
        if(Kept!=12 || Box->PersistentId!=BoxId || Box->StorageInventory->Count(EFrontierItem::Material)!=0)return false;
        Hero->SetActorLocation(FVector(500,-800,40));Stop();PreviousHero=Hero->GetActorLocation();
        UE_LOG(LogTemp,Display,TEXT("CF_HOME_EXPAND_BUILD PASS modules=19 kept=12 material=2 farms=2 fixedObserver=1 box=%s pet=%s"),*BoxId.ToString(),*PetId.ToString());return true;
    }
    bool EndSave()
    {
        Stop();const auto Id=UpperFarm->PersistentId;const auto Bag=Hero->Inventory->GetStacks();const auto Partner=Pet->GetActorTransform();
        const float HP=Pet->Health,Nutrition=Pet->Nutrition,Stamina=Pet->Stamina;const auto Name=Pet->Nickname;const auto Definition=Pet->Definition;const auto Level=Pet->Level;
        if(!Hero->SaveProgress(TEXT("HomeExpansionDone")) || !Hero->LoadProgress(TEXT("HomeExpansionDone")))return false;
        const auto* Save=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("HomeExpansionDone"),0));
        if(!Save || Save->FormatVersion!=FrontierCurrentSaveVersion || Save->HomeModules.Num()!=19 || Save->Facilities.Num()!=2 || !Bag.OrderIndependentCompareEqual(Hero->Inventory->GetStacks()))return false;
        const auto LiveLayout=HomeLayout::Collect(World);
        auto Same=[](const FHomeModuleRecord& A,const FHomeModuleRecord& B){return A.Id==B.Id && A.FloorId==B.FloorId && A.Definition==B.Definition && A.Transform.Equals(B.Transform,.01) && A.PaidMaterials==B.PaidMaterials && A.DoorState==B.DoorState && A.StoredItems.OrderIndependentCompareEqual(B.StoredItems) && A.PaidExtraItems.OrderIndependentCompareEqual(B.PaidExtraItems);};
        for(const auto& BeforeRecord:Expanded){const auto* After=Save->HomeModules.FindByPredicate([&](const FHomeModuleRecord& R){return R.Id==BeforeRecord.Id;});const auto* Live=LiveLayout.FindByPredicate([&](const FHomeModuleRecord& R){return R.Id==BeforeRecord.Id;});if(!After || !Live || !Same(*After,BeforeRecord) || !Same(*Live,BeforeRecord))return false;}
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->PersistentId==PetId)Pet=*It;
        for(TActorIterator<AWorkStation> It(World);It;++It)if(It->PersistentId==Id)UpperFarm=*It;
        const auto* OldFarm=Save->Facilities.FindByPredicate([&](const FFacilitySaveRecord& R){return R.Id==OriginalFarmId;});
        return Pet.IsValid() && Pet->Tamer==Hero->GetController() && Pet->Activity==ECreatureActivity::Follow && Pet->BondPoints==6 && Pet->Experience==StartXP+4 &&
            Pet->Level==Level && Pet->Nickname==Name && Pet->Definition==Definition && FMath::IsNearlyEqual(Pet->Health,HP,.01f) && FMath::IsNearlyEqual(Pet->Nutrition,Nutrition,.01f) && FMath::IsNearlyEqual(Pet->Stamina,Stamina,.01f) &&
            OldFarm && OldFarm->Transform.Equals(OriginalFarmPose,.01) && OldFarm->StoredFood==0 && OldFarm->Seeds==0 && !OldFarm->bPlanted && OldFarm->GrowthWork==0 &&
            Pet->GetActorTransform().Equals(Partner,.01) && !Pet->WorkSite && !Pet->GetFieldResource() && !Pet->Cargo->HasOrder() && Pet->Cargo->Count()==0 && UpperFarm.IsValid() && UpperFarm->StoredFood==0 && UpperFarm->Crop->Seeds==0 &&
            Hero->Inventory->Count(EFrontierItem::Material)==2 && Hero->Inventory->Count(EFrontierItem::Seeds)==1 && Hero->Inventory->Count(EFrontierItem::Food)==1;
    }
    bool Tick()
    {
        if(!Pet.IsValid() || !Box.IsValid() || FPlatformTime::Seconds()>Deadline)return Finish(false,TEXT("lifetime"));
        const auto Now=Hero->GetActorLocation(),PetNow=Pet->GetActorLocation();
        if(FVector::Dist(Now,PreviousHero)>=80 || FVector::Dist(PetNow,PreviousPet)>=120 || Hero->Health<=0 || Pet->Health<=0 || !Hero->GetActorEnableCollision() || !Pet->GetActorEnableCollision() || !Pet->IsActorTickEnabled() || !Hero->Pace->IsSlowWalking() || Hero->GetCharacterMovement()->MovementMode!=MOVE_Walking || Pet->GetCharacterMovement()->MovementMode==MOVE_None)return Finish(false,TEXT("continuous"));
        HeroTravel+=FVector::Dist2D(Now,PreviousHero);PetTravel+=FVector::Dist2D(PetNow,PreviousPet);PreviousHero=Now;PreviousPet=PetNow;
        if(Phase>=7 && Phase<10){HeroUp |= Now.Z>350 && Now.X<-210;PetUp |= PetNow.Z>350 && PetNow.X<-210;}if(Phase>=10)PetDown |= PetNow.Z>110 && PetNow.Z<330;
        if(Phase==0)
        {
            auto* Source=Node(SourceIds[Step]);if(!Source)return Finish(false,TEXT("source"));
            if(!Planned && !Plan(Source))return Finish(false,TEXT("source-path"));const int32 Arrived=Move();if(Arrived<0)return Finish(false,TEXT("source-blocked"));if(!Arrived)return true;
            Face(Source->GetActorLocation());if(FVector::Dist2D(PetNow,Pet->GetFollowDestination())>65)return true;
            if(!Gathering)
            {
                if(Source->Remaining==0 && Source->RegrowRemaining>0)return true;
                if(!Source->CanReach(Hero,300) || Source->Remaining<Amounts[Step])return Finish(false,TEXT("source-reach-or-stock"));
                if(Step==3){UE_LOG(LogTemp,Display,TEXT("CF_HOME_EXPAND_REGROW_STATE waited=%.3f required=%.3f remaining=%d capacity=%d timer=%.3f"),World->GetTimeSeconds()-StartedAt,StartRegrow,Source->Remaining,Source->Definition->Capacity,Source->RegrowRemaining);if(World->GetTimeSeconds()-StartedAt+.1<StartRegrow || Source->Remaining!=24)return Finish(false,TEXT("regrow-time"));UE_LOG(LogTemp,Display,TEXT("CF_HOME_EXPAND_REGROW PASS waited=%.2f required=%.2f capacity=24"),World->GetTimeSeconds()-StartedAt,StartRegrow);}
                Before=Source->Remaining;if(!CFInputQA::Press(Hero,EKeys::M))return Finish(false,TEXT("gather-input"));Gathering=true;
            }
            const int32 Taken=Before-Source->Remaining;if(Taken>Amounts[Step])return Finish(false,TEXT("extra-harvest"));if(Taken<Amounts[Step])return true;
            if(!CFInputQA::Release(Hero,EKeys::M))return Finish(false,TEXT("release"));
            UE_LOG(LogTemp,Display,TEXT("CF_HOME_EXPAND_HARVEST PASS id=%d taken=%d remaining=%d material=%d fiber=%d"),SourceIds[Step],Taken,Source->Remaining,Hero->Inventory->Count(EFrontierItem::Material),Hero->Inventory->Count(EFrontierItem::Fiber));
            ++Step;Gathering=false;Planned=false;if(Step==4)Next(1);return true;
        }
        if(Phase==1)
        {
            if(!Planned && !Plan(nullptr,FVector(-100,600,40)))return Finish(false,TEXT("craft-path"));const auto Arrived=Move();if(Arrived<0)return Finish(false,TEXT("craft-walk"));if(!Arrived)return true;
            if(!Hero->CraftRecipe(TEXT("WeaveSupplies")) || !Hero->CraftRecipe(TEXT("WeaveSupplies")) || Hero->Inventory->Count(EFrontierItem::Material)!=68 || Hero->Inventory->Count(EFrontierItem::Fiber)!=0)return Finish(false,TEXT("craft-budget"));
            if(!BoxRoute())return Finish(false,TEXT("box-path"));Phase=2;return true;
        }
        if(Phase==2 || Phase==5)
        {
            const auto Arrived=Move();if(Arrived<0)return Finish(false,TEXT("box-walk"));if(!Arrived)return true;Face(Box->GetActorLocation()+FVector(0,0,40));
            if(Phase==2){if(!CFInputQA::Press(Hero,EKeys::V) || !Hero->HasDeliverySelection())return Finish(false,TEXT("box-selection"));Next(3);return true;}
            if(!CFInputQA::Press(Hero,EKeys::E) || !Hero->Storage->IsOpen() || !CFInputQA::Press(Hero,EKeys::Left) || !CFInputQA::Press(Hero,EKeys::E) || Hero->Inventory->Count(EFrontierItem::Material)!=74 || Box->StorageInventory->Count(EFrontierItem::Material)!=0)return Finish(false,TEXT("withdraw-budget"));
            Route({FVector(-400,-800,60),FVector(100,-800,60),FVector(500,-800,40)});Phase=6;return true;
        }
        if(Phase==3)
        {
            if(!Planned && !Plan(Node(1)))return Finish(false,TEXT("cargo-order-path"));const auto Arrived=Move();if(Arrived<0)return Finish(false,TEXT("cargo-order-walk"));if(!Arrived)return true;Face(Node(1)->GetActorLocation());
            if(!CFInputQA::Press(Hero,EKeys::E) || Pet->GetFieldResource()!=Node(1) || Pet->Cargo->GetTarget()!=Box.Get())return Finish(false,TEXT("cargo-order"));Next(4);
            // 下令后实际走到旁侧等候，不把玩家胶囊长期停在资源与箱子的通道上。
            if(!Plan(nullptr,FVector(-100,600,40)))return Finish(false,TEXT("cargo-observe-path"));return true;
        }
        if(Phase==4)
        {
            SawCargo |= Pet->Cargo->Count()>0;
            if(Node(1)->Remaining+Pet->Cargo->Count()+Box->StorageInventory->Count(EFrontierItem::Material)!=6 || Hero->Inventory->Count(EFrontierItem::Material)!=68 || World->GetTimeSeconds()-At>90)
            {UE_LOG(LogTemp,Display,TEXT("CF_HOME_EXPAND_CARGO_STATE source=%d held=%d stored=%d stamina=%.2f nutrition=%.2f activity=%d velocity=%s"),Node(1)->Remaining,Pet->Cargo->Count(),Box->StorageInventory->Count(EFrontierItem::Material),Pet->Stamina,Pet->Nutrition,static_cast<int32>(Pet->Activity),*Pet->GetVelocity().ToString());return Finish(false,TEXT("cargo-conservation-or-timeout"));}
            if(Move()<0)return Finish(false,TEXT("cargo-observe-walk"));
            if(Box->StorageInventory->Count(EFrontierItem::Material)!=6)return true;
            if(!SawCargo || Pet->Cargo->Count()!=0 || Pet->Experience!=StartXP+3 || !CFInputQA::Press(Hero,EKeys::F))return Finish(false,TEXT("cargo-delivery"));
            UE_LOG(LogTemp,Display,TEXT("CF_HOME_EXPAND_CARGO PASS source=0 cargo=0 stored=6 material=68 pet=%s"),*PetId.ToString());
            if(!BoxRoute())return Finish(false,TEXT("cargo-box-path"));Phase=5;return true;
        }
        if(Phase==6)
        {
            const auto Arrived=Move();if(Arrived<0)return Finish(false,TEXT("park-walk"));if(!Arrived || PetNow.X<240 || FVector::Dist2D(PetNow,Pet->GetFollowDestination())>65)return true;
            Stop();if(!Hero->SaveProgress(TEXT("HomeExpansionFunded")))return Finish(false,TEXT("funded-save"));
            UE_LOG(LogTemp,Display,TEXT("CF_HOME_EXPAND_FUNDED PASS modules=14 material=74 stored=0 seeds=2 carried=3 pet=%s"),*PetId.ToString());
            if(!Build())return Finish(false,TEXT("build"));Route({FVector(-400,-850,380),FVector(-510,-850,380)});Phase=7;At=World->GetTimeSeconds();return true;
        }
        if(Phase==7)
        {
            if(World->GetTimeSeconds()-At>30){UE_LOG(LogTemp,Display,TEXT("CF_HOME_EXPAND_UPPER_WAIT heroUp=%d petUp=%d destination=%s"),HeroUp,PetUp,*Pet->GetFollowDestination().ToString());return Finish(false,TEXT("upper-follow-timeout"));}
            const auto Arrived=Move();if(Arrived<0)return Finish(false,TEXT("ascend-walk"));if(!Arrived || !PetUp || FVector::Dist2D(PetNow,Pet->GetFollowDestination())>65)return true;
            if(ColdRead)
            {
                UE_LOG(LogTemp,Display,TEXT("CF_HOME_EXPAND_COLD_UP PASS hero=1 pet=1 food=1 seeds=1 material=2 pet=%s"),*PetId.ToString());
                Route({FVector(-400,-850,380),FVector(-250,-850,380),FVector(80,-850,140),FVector(500,-800,40)});Phase=10;At=World->GetTimeSeconds();return true;
            }
            Face(UpperFarm->GetActorLocation()+FVector(0,0,30));if(!HeroUp || !CFInputQA::Press(Hero,EKeys::F2) || UpperFarm->Crop->Seeds!=1 || !CFInputQA::Press(Hero,EKeys::E) || Pet->WorkSite!=UpperFarm.Get())return Finish(false,TEXT("upper-seed-order"));Next(8);return true;
        }
        if(Phase==8)
        {
            if(World->GetTimeSeconds()-At>30)return Finish(false,TEXT("upper-production-timeout"));if(UpperFarm->StoredFood==0)return true;
            Face(UpperFarm->GetActorLocation()+FVector(0,0,30));if(Pet->Experience!=StartXP+4 || !CFInputQA::Press(Hero,EKeys::O) || Hero->Inventory->Count(EFrontierItem::Food)!=1 || !CFInputQA::Press(Hero,EKeys::F))return Finish(false,TEXT("upper-withdraw"));
            UE_LOG(LogTemp,Display,TEXT("CF_HOME_EXPAND_UPPER PASS hero=1 pet=1 food=1 seeds=1 material=2 pet=%s"),*PetId.ToString());
            Route({FVector(-400,-850,380),FVector(-250,-850,380),FVector(80,-850,140),FVector(500,-800,40)});Phase=10;At=World->GetTimeSeconds();return true;
        }
        const auto Arrived=Move();if(Arrived<0)return Finish(false,TEXT("descend-walk"));if(!Arrived || PetNow.Z>100 || PetNow.X<240 || FVector::Dist2D(PetNow,Pet->GetFollowDestination())>65)return true;
        if(!PetDown || !HeroUp || !PetUp || HeroTravel<(FundedRestore?1400:7000) || PetTravel<(FundedRestore?1000:5000) || !EndSave())return Finish(false,TEXT("end-state-or-save"));
        UE_LOG(LogTemp,Display,TEXT("CF_HOME_EXPAND_SAVE PASS version=%d modules=19 farms=2 material=2 food=1 seeds=1 bond=6 pet=%s"),FrontierCurrentSaveVersion,*PetId.ToString());return Finish(true,TEXT("complete"));
    }
};
}
void RunHomeExpansionScenario(UWorld* World,ACFPlayerCharacter* Hero,bool PlanOnly)
{
    const bool Funded=FParse::Param(FCommandLine::Get(),TEXT("CFHomeExpansionFunded"));
    const bool Read=FParse::Param(FCommandLine::Get(),TEXT("CFHomeExpansionRead"));
    FVector FreshStone=FVector::ZeroVector;for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,5))FreshStone=It->GetActorLocation();
    bool Pass=FreshStone.Equals(StarterStoneMineLocation(),.01) && Hero->LoadProgress(TEXT("HomeExpansionInput")) && (Read || PlanExpansion(World,Hero,Funded));
    if(Read && Pass)
    {
        const auto* Save=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("HomeExpansionInput"),0));const auto Layout=HomeLayout::Collect(World);
        Pass=Save && (Save->FormatVersion>=34 && Save->FormatVersion<=FrontierCurrentSaveVersion) && Save->HomeModules.Num()==19 && Layout.Num()==19 && Save->Facilities.Num()==2 &&
            Hero->GetActorTransform().Equals(Save->PlayerTransform,.01) && Hero->Inventory->GetStacks().OrderIndependentCompareEqual(Save->InventoryStacks) &&
            Hero->Inventory->Count(EFrontierItem::Material)==2 && Hero->Inventory->Count(EFrontierItem::Food)==1 && Hero->Inventory->Count(EFrontierItem::Seeds)==1;
        if(Pass)for(const auto& R:Save->HomeModules){const auto* L=Layout.FindByPredicate([&](const FHomeModuleRecord& V){return V.Id==R.Id;});Pass &= L && L->Definition==R.Definition && L->FloorId==R.FloorId && L->Transform.Equals(R.Transform,.01) && L->PaidMaterials==R.PaidMaterials && L->DoorState==R.DoorState && L->StoredItems.OrderIndependentCompareEqual(R.StoredItems) && L->PaidExtraItems.OrderIndependentCompareEqual(R.PaidExtraItems);}
        int32 Owned=0;
        if(Pass)for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController())
        {++Owned;const auto* R=Save->Creatures.FindByPredicate([&](const FCreatureSaveRecord& V){return V.Id==It->PersistentId && V.bOwned;});Pass &= R && It->BondPoints==6 && It->Activity==ECreatureActivity::Follow && !It->WorkSite && !It->GetFieldResource() && It->Cargo->Count()==0 && !It->Cargo->HasOrder() && It->Experience==R->Experience && It->Level==R->Level && It->Nickname==R->Nickname && It->Definition==R->Definition.Get() && It->GetActorTransform().Equals(R->Transform,.01) && FMath::IsNearlyEqual(It->Health,R->Health,.01f) && FMath::IsNearlyEqual(It->Nutrition,R->Nutrition,.01f) && FMath::IsNearlyEqual(It->Stamina,R->Stamina,.01f);}
        Pass &= Owned==1;
        if(Pass)for(const auto& R:Save->Facilities){AWorkStation* Live=nullptr;for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt && It->PersistentId==R.Id)Live=*It;Pass &= Live && Live->GetActorTransform().Equals(R.Transform,.01) && Live->RequiredSpecialty==R.Specialty && Live->StoredFood==0 && R.StoredFood==0 && Live->Crop->Seeds==0 && R.Seeds==0 && !Live->Crop->bPlanted && !R.bPlanted && Live->Crop->GrowthWork==0 && R.GrowthWork==0;}
    }
    if(PlanOnly){UE_LOG(LogTemp,Display,TEXT("CF_HOME_EXPAND_PLAN_ONLY %s readonly=1"),Pass?TEXT("PASS"):TEXT("FAIL"));FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return;}
    if(!Pass){UE_LOG(LogTemp,Error,TEXT("CF_HOME_EXPAND FAIL reason=initial-plan"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    if(!Funded && !Read)
    {
    // 在真实输入副本之外另建进度样本，验证旧坐标纠正不把部分采集进度重置为满矿。
    auto* Sample=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("HomeExpansionInput"),0));
    auto* Record=Sample?Sample->ResourceNodes.FindByPredicate([](const FResourceSaveRecord& R){return R.Id==FGuid(0xCF170000,0,0,5);}):nullptr;
    if(!Record){UE_LOG(LogTemp,Error,TEXT("CF_HOME_EXPAND FAIL reason=resource-record"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    Record->Transform.SetLocation(FVector(1250,1000,0));Record->Remaining=13;Record->HarvestCooldown=.4f;Record->RegrowRemaining=0;
    bool Partial=UGameplayStatics::SaveGameToSlot(Sample,TEXT("HomeExpansionPartial"),0) && Hero->LoadProgress(TEXT("HomeExpansionPartial"));
    AResourceNode* Restored=nullptr;for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==Record->Id)Restored=*It;
    Partial &= Restored && Restored->Remaining==13 && FMath::IsNearlyEqual(Restored->HarvestCooldown,.4f,.001f) && Restored->RegrowRemaining==0 && Restored->GetActorLocation().Equals(StarterStoneMineLocation(),.01) && Hero->Inventory->GetStacks().OrderIndependentCompareEqual(Sample->InventoryStacks);
    UE_LOG(LogTemp,Display,TEXT("CF_RESOURCE_POSITION_PROGRESS %s fresh=1 remaining=13 cooldown=0.4 inventory=1"),Partial?TEXT("PASS"):TEXT("FAIL"));
    if(!Partial || !Hero->LoadProgress(TEXT("HomeExpansionInput"))){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    // 独立计时单元样本，不快进实际资源或用它供应建造材料。
    auto* ProbeResource=World->SpawnActor<AResourceNode>(FVector(-5000,-5000,-5000),FRotator::ZeroRotator);
    ProbeResource->SetActorTickEnabled(false);ProbeResource->Definition=LoadObject<UResourceDefinition>(nullptr,TEXT("/Game/Resources/DA_Brush.DA_Brush"));ProbeResource->Remaining=0;ProbeResource->RegrowRemaining=143.04f;
    for(int32 Index=0;Index<1400000;++Index)ProbeResource->Tick(.0001f);
    bool Precise=ProbeResource->Remaining==0 && FMath::IsNearlyEqual(ProbeResource->RegrowRemaining,3.04f,.002f);
    ProbeResource->Tick(3.1f);Precise &= ProbeResource->RegrowRemaining==0 && ProbeResource->Remaining==24;
    ProbeResource->Destroy();UE_LOG(LogTemp,Display,TEXT("CF_RESOURCE_TIMER_PRECISION %s steps=1400000 delta=0.0001 elapsed=140 remaining=3.04 regrew=1"),Precise?TEXT("PASS"):TEXT("FAIL"));
    if(!Precise){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    if(FParse::Param(FCommandLine::Get(),TEXT("CFHomeExpansionRepair")))
    {UE_LOG(LogTemp,Display,TEXT("CF_RESOURCE_REPAIR PASS fresh=1 position=1 progress=1 timer=1"));FPlatformMisc::RequestExitWithStatus(false,0);return;}
    }
    const auto State=MakeShared<FExpansionTest>();State->World=World;State->Hero=Hero;State->Original=HomeLayout::Collect(World);
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController())State->Pet=*It;
    State->Box=State->Module(EHomeModuleKind::Storage,FVector(-900,-900,-30));State->PetId=State->Pet->PersistentId;State->BoxId=State->Box->PersistentId;State->StartXP=State->Pet->Experience-(Read?4:Funded?3:0);
    State->FundedRestore=Funded || Read;State->ColdRead=Read;for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt){if(It->GetActorLocation().Z<100){State->OriginalFarmId=It->PersistentId;State->OriginalFarmPose=It->GetActorTransform();}else State->UpperFarm=*It;}
    for(TActorIterator<AWildEncounter> It(World);It;++It)It->SetActorTickEnabled(false);
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(*It!=State->Pet.Get()){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();}
    auto* Probe=NewObject<UHomeMovementProbe>(Hero);Probe->RegisterComponent();State->Input=Probe;
    if(!CFInputQA::Press(Hero,EKeys::LeftShift) || !Hero->Pace->IsSlowWalking()){State->Finish(false,TEXT("slow-input"));return;}
    State->PreviousHero=Hero->GetActorLocation();State->PreviousPet=State->Pet->GetActorLocation();State->Deadline=FPlatformTime::Seconds()+540;State->StartedAt=World->GetTimeSeconds();State->StartRegrow=State->Node(1)->RegrowRemaining;
    if(Funded){State->Deadline=FPlatformTime::Seconds()+120;State->Step=4;State->Phase=6;State->Route({Hero->GetActorLocation()});UE_LOG(LogTemp,Display,TEXT("CF_HOME_EXPAND_FUNDED_RESTORE PASS modules=14 material=74 stored=0 seeds=2 pet=%s"),*State->PetId.ToString());}
    if(Read){State->Expanded=HomeLayout::Collect(World);State->Deadline=FPlatformTime::Seconds()+120;State->Step=4;State->Phase=7;State->At=World->GetTimeSeconds();State->Route({FVector(-400,-850,380),FVector(-510,-850,380)});UE_LOG(LogTemp,Display,TEXT("CF_HOME_EXPAND_RESTORE PASS version=%d modules=19 farms=2 material=2 food=1 seeds=1 pet=%s box=%s"),Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("HomeExpansionInput"),0))->FormatVersion,*State->PetId.ToString(),*State->BoxId.ToString());}
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float){return State->Tick();}));
}
