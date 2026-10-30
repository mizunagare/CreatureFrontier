#include "ConstructionScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "HomeScenario.h"
#include "HomeLayout.h"
#include "InventoryComponent.h"
#include "FarmCropComponent.h"
#include "WorkStation.h"
#include "FrontierSaveGame.h"
#include "WildEncounter.h"
#include "CompanionCargoComponent.h"
#include "InputRoutingQA.h"
#include "ResourceNode.h"
#include "ResourceDefinition.h"
#include "PlayerGatheringComponent.h"
#include "CraftingCatalog.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
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
// 从真实施工或照料完成档继续，复用原采集、配方、农圃和玩家输入，不重设位置或需求。
struct FHomeCareTest
{
    UWorld* World;ACFPlayerCharacter* Hero;TWeakObjectPtr<ACreatureCharacter> Pet;TWeakObjectPtr<AWorkStation> Farm;
    TWeakObjectPtr<UHomeMovementProbe> Input;TArray<FVector> Route;TArray<FHomeModuleRecord> House;TArray<FFacilitySaveRecord> Facilities;
    FGuid PetId,FarmId;FString Nickname,ImageRoot;TSet<FString> Photos;int32 Phase=0,Point=0;
    bool Read=false,ReservedRead=false,ResumeOnly=false,Resupply=false,Supplied=false,FoodMade=false,GatherHeld=false,HungerChecked=false,Preview=false,Mid=false,Fed=false;double Deadline=0,At=0,WaitAt=-1,ShotUntil=0,ProgressAt=0,PreviousTime=0,HeroTravel=0,PetTravel=0;
    TWeakObjectPtr<AResourceNode> SupplyNode;int32 SupplyPart=0,NodeBefore=0,ItemBefore=0;
    int32 StartXP() const{return Resupply?9:8;}
    int32 StartBond() const{return Resupply?12:9;}
    int32 SpareSeeds() const{return Resupply?2:0;}
    float WaitStamina=0;FVector PreviousHero,PreviousPet,HeroVelocity,PetVelocity,Anchor;
    bool Bind()
    {
        Pet.Reset();Farm.Reset();int32 Count=0;
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController()){Pet=*It;++Count;}
        if(Count!=1 || !Pet.IsValid() || (PetId.IsValid() && Pet->PersistentId!=PetId))return false;
        PetId=Pet->PersistentId;
        for(TActorIterator<AWorkStation> It(World);It;++It)if(FarmId.IsValid() && It->PersistentId==FarmId && It->bPlayerBuilt)Farm=*It;
        if(Read && !Farm.IsValid() && IsValid(Pet->WorkSite)){Farm=Pet->WorkSite;FarmId=Farm->PersistentId;}
        PreviousHero=Hero->GetActorLocation();PreviousPet=Pet->GetActorLocation();HeroVelocity=Hero->GetVelocity();PetVelocity=Pet->GetVelocity();PreviousTime=World->GetTimeSeconds();
        for(TActorIterator<AWildEncounter> It(World);It;++It)It->SetActorTickEnabled(false);
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(*It!=Pet.Get()){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();}
        return true;
    }
    bool End(bool Pass,const TCHAR* Reason)
    {
        if(Input.IsValid())Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::LeftShift);
        if(GatherHeld)CFInputQA::Release(Hero,EKeys::M);
        const FString OutcomeFeedback=Hero->Feedback;
        if(!Pass)Hero->SaveProgress(TEXT("HomeCareBlocked"));
        UE_LOG(LogTemp,Display,TEXT("CF_HOME_CARE %s reason=%s phase=%d heroTravel=%.1f petTravel=%.1f hero=%s pet=%s feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Reason,Phase,HeroTravel,PetTravel,*Hero->GetActorLocation().ToString(),Pet.IsValid()?*Pet->GetActorLocation().ToString():TEXT("无"),*OutcomeFeedback);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    void Face(FVector Target)
    {const auto R=(Target-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->SetActorRotation(FRotator(0,R.Yaw,0));Hero->FirstPersonCamera->SetRelativeRotation(FRotator::ZeroRotator);Hero->GetController()->SetControlRotation(R);Hero->FirstPersonCamera->SetWorldRotation(R);}
    bool Photo(const TCHAR* Name,FVector Target)
    {
        if(!Preview || Photos.Contains(Name))return false;Photos.Add(Name);Face(Target);
        FScreenshotRequest::RequestScreenshot(ImageRoot/(FString(Name)+TEXT(".png")),false,false);ShotUntil=FPlatformTime::Seconds()+.2;return true;
    }
    bool Plan(AWorkStation* Target,bool ClearStand=false)
    {
        auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);if(!Nav)return false;TArray<FVector> Best;double Length=DBL_MAX;
        for(int32 I=0;I<(Target && !ClearStand?8:1);++I)
        {
            const auto Candidate=Target?Target->GetActorLocation()+(ClearStand?FVector(0,-250,0):FVector(FMath::Cos(I*PI/4)*190,FMath::Sin(I*PI/4)*190,0)):FVector(-1500,-1080,40);
            FNavLocation Floor;if(!Nav->ProjectPointToNavigation(Candidate,Floor,FVector(60,60,160))){UE_LOG(LogTemp,Display,TEXT("CF_HOME_CARE_PATH_REJECT index=%d reason=projection"),I);continue;}
            const auto* Shape=Hero->GetCapsuleComponent();const FVector Center=Floor.Location+FVector(0,0,Shape->GetScaledCapsuleHalfHeight());
            FCollisionQueryParams Q(SCENE_QUERY_STAT(HomeCareStand),false,Hero);FHitResult Hit;
            if(World->OverlapBlockingTestByChannel(Center,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Shape->GetScaledCapsuleRadius()-1,Shape->GetScaledCapsuleHalfHeight()-1),Q)){UE_LOG(LogTemp,Display,TEXT("CF_HOME_CARE_PATH_REJECT index=%d reason=space goal=%s"),I,*Center.ToString());continue;}
            if(Target && (FVector::Dist(Center,Target->GetActorLocation())>280 || (World->LineTraceSingleByChannel(Hit,Center+FVector(0,0,64),Target->GetActorLocation()+FVector(0,0,30),ECC_Visibility,Q) && Hit.GetActor()!=Target))){UE_LOG(LogTemp,Display,TEXT("CF_HOME_CARE_PATH_REJECT index=%d reason=sight actor=%s goal=%s"),I,*GetNameSafe(Hit.GetActor()),*Center.ToString());continue;}
            auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Hero->GetActorLocation(),Floor.Location,Hero);
            if(Path && Path->IsValid() && !Path->IsPartial() && Path->PathPoints.Num()>1 && Path->GetPathLength()<Length){Length=Path->GetPathLength();Best=Path->PathPoints;}
            else if(!Path || !Path->IsValid() || Path->IsPartial())UE_LOG(LogTemp,Display,TEXT("CF_HOME_CARE_PATH_REJECT index=%d reason=path valid=%d partial=%d goal=%s"),I,Path && Path->IsValid(),Path && Path->IsPartial(),*Center.ToString());
        }
        if(Best.IsEmpty())return false;Route=MoveTemp(Best);Point=1;Anchor=Hero->GetActorLocation();ProgressAt=World->GetTimeSeconds();return CFInputQA::Press(Hero,EKeys::LeftShift);
    }
    int32 Move()
    {
        const auto Here=Hero->GetActorLocation();while(Point<Route.Num() && FVector::Dist2D(Here,Route[Point])<16)++Point;
        if(Point==Route.Num()){Input->Direction=FVector::ZeroVector;if(Hero->GetVelocity().Size2D()>3)return 0;CFInputQA::Release(Hero,EKeys::LeftShift);return 1;}
        const auto Direction=(Route[Point]-Here).GetSafeNormal2D();Input->Direction=Direction;Face(Here+Direction*500+FVector(0,0,64));
        if(FVector::Dist2D(Here,Anchor)>20){Anchor=Here;ProgressAt=World->GetTimeSeconds();}return World->GetTimeSeconds()-ProgressAt>5?-1:0;
    }
    bool PlanResource(AResourceNode* Node)
    {
        auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);if(!Nav || !Node)return false;
        TArray<FVector> Best;double Length=DBL_MAX;
        for(int32 I=0;I<8;++I)
        {
            const double A=I*PI/4;FNavLocation Floor;
            if(!Nav->ProjectPointToNavigation(Node->GetActorLocation()+FVector(FMath::Cos(A)*220,FMath::Sin(A)*220,0),Floor,FVector(60,60,160)))continue;
            const auto* Shape=Hero->GetCapsuleComponent();const FVector Center=Floor.Location+FVector(0,0,Shape->GetScaledCapsuleHalfHeight());
            FCollisionQueryParams Q(SCENE_QUERY_STAT(HomeSupplyStand),false,Hero);FHitResult Hit;
            if(World->OverlapBlockingTestByChannel(Center,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Shape->GetScaledCapsuleRadius()-1,Shape->GetScaledCapsuleHalfHeight()-1),Q))continue;
            if(World->LineTraceSingleByChannel(Hit,Center,Node->GetActorLocation(),ECC_Visibility,Q) && Hit.GetActor()!=Node)continue;
            auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Hero->GetActorLocation(),Floor.Location,Hero);
            if(Path && Path->IsValid() && !Path->IsPartial() && Path->PathPoints.Num()>1 && Path->GetPathLength()<Length){Length=Path->GetPathLength();Best=Path->PathPoints;}
        }
        if(Best.IsEmpty())return false;Route=MoveTemp(Best);Point=1;Anchor=Hero->GetActorLocation();ProgressAt=World->GetTimeSeconds();return CFInputQA::Press(Hero,EKeys::LeftShift);
    }
    bool GatherSupply()
    {
        if(World->GetTimeSeconds()-At<.1 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;
        const auto Item=SupplyPart==0?EFrontierItem::Material:EFrontierItem::Fiber;const int32 Wanted=SupplyPart==0?2:1;
        if(Hero->Inventory->Count(EFrontierItem::Seeds)!=0 || Hero->Inventory->Count(EFrontierItem::Food)!=0 ||
            Pet->Experience!=StartXP() || Pet->BondPoints!=StartBond() || Pet->WorkSite || !SameWorld())return End(false,TEXT("supply-world"));
        if(!SupplyNode.IsValid())
        {
            for(TActorIterator<AResourceNode> It(World);It;++It)if(It->Definition && It->Definition->OutputItem==Item && It->Remaining>0 && PlanResource(*It)){SupplyNode=*It;return true;}
            return End(false,TEXT("supply-source"));
        }
        auto* Node=SupplyNode.Get();
        if(!GatherHeld)
        {
            const int32 Result=Move();if(Result<0)return End(false,TEXT("supply-walk"));if(!Result)return true;
            Face(Node->GetActorLocation());
            if(Photo(SupplyPart==0?TEXT("Material"):TEXT("Fiber"),Node->GetActorLocation()))return true;
            ItemBefore=Hero->Inventory->Count(Item);NodeBefore=Node->Remaining;
            if(Hero->TraceResource()!=Node || !Node->CanReach(Hero,300) || !CFInputQA::Press(Hero,EKeys::M) || !Hero->Gathering->IsGathering())return End(false,TEXT("supply-input"));
            GatherHeld=true;return true;
        }
        const int32 Gained=Hero->Inventory->Count(Item)-ItemBefore;
        if(Gained<0 || Node->Remaining!=NodeBefore-Gained || Hero->Inventory->Count(Item)>Wanted)return End(false,TEXT("supply-resource-budget"));
        if(Hero->Inventory->Count(Item)==Wanted)
        {
            if(!CFInputQA::Release(Hero,EKeys::M) || Hero->Gathering->IsGathering())return End(false,TEXT("supply-release"));
            GatherHeld=false;SupplyNode.Reset();
            UE_LOG(LogTemp,Display,TEXT("CF_HOME_RESUPPLY_GATHER PASS part=%d gained=%d remaining=%d input=1"),SupplyPart,Gained,Node->Remaining);
            if(++SupplyPart==2){Phase=-1;At=World->GetTimeSeconds();}return true;
        }
        if(!Hero->Gathering->IsGathering())return End(false,TEXT("supply-stopped"));return true;
    }
    bool SameWorld() const
    {
        const auto Current=HomeLayout::Collect(World);if(Current.Num()!=House.Num())return false;
        for(const auto& Old:House){const auto* Now=Current.FindByPredicate([&](const FHomeModuleRecord& R){return R.Id==Old.Id;});
            if(!Now || Now->Definition!=Old.Definition || Now->FloorId!=Old.FloorId || !Now->Transform.Equals(Old.Transform,.01) || Now->PaidMaterials!=Old.PaidMaterials || Now->DoorState!=Old.DoorState || !Now->StoredItems.OrderIndependentCompareEqual(Old.StoredItems) || !Now->PaidExtraItems.OrderIndependentCompareEqual(Old.PaidExtraItems))return false;}
        int32 Count=0;
        for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt)
        {
            ++Count;const auto* Old=Facilities.FindByPredicate([&](const FFacilitySaveRecord& R){return R.Id==It->PersistentId;});
            if(!Old || !It->GetActorTransform().Equals(Old->Transform,.01) || It->RequiredSpecialty!=Old->Specialty)return false;
            if(It->PersistentId!=FarmId && (It->StoredFood!=Old->StoredFood || It->Crop->Seeds!=Old->Seeds || It->Crop->bPlanted!=Old->bPlanted || !FMath::IsNearlyEqual(It->Crop->GrowthWork,Old->GrowthWork,.01f)))return false;
        }
        return Count==Facilities.Num() && Pet->Nickname==Nickname;
    }
    bool Motion(const ACharacter* Pawn,FVector Before,FVector Position,FVector Velocity,double Delta) const
    {
        const auto* M=Pawn->GetCharacterMovement();if(!M || Delta<0 || !FMath::IsFinite(Delta) || Position.ContainsNaN())return false;
        return FVector::Dist2D(Before,Position)<=FMath::Max(static_cast<double>(M->GetMaxSpeed()),Velocity.Size2D())*Delta+3 &&
            FMath::Abs(Position.Z-Before.Z)<=FMath::Max(FMath::Abs(Pawn->GetVelocity().Z),FMath::Abs(Velocity.Z))*Delta+.5*FMath::Abs(World->GetGravityZ())*Delta*Delta+M->MaxStepHeight+3;
    }
    bool Tick()
    {
        if(!Pet.IsValid() || (!Farm.IsValid() && Phase!=-1 && Phase!=7) || FPlatformTime::Seconds()>Deadline)return End(false,TEXT("lifetime"));
        const auto H=Hero->GetActorLocation(),P=Pet->GetActorLocation();const double Delta=World->GetTimeSeconds()-PreviousTime;
        const bool Valid=Motion(Hero,PreviousHero,H,HeroVelocity,Delta) && Motion(Pet.Get(),PreviousPet,P,PetVelocity,Delta);
        HeroTravel+=FVector::Dist(H,PreviousHero);PetTravel+=FVector::Dist(P,PreviousPet);PreviousHero=H;PreviousPet=P;HeroVelocity=Hero->GetVelocity();PetVelocity=Pet->GetVelocity();PreviousTime=World->GetTimeSeconds();
        if(!Valid || !Hero->GetActorEnableCollision() || !Pet->GetActorEnableCollision() || !Pet->IsActorTickEnabled() || Pet->GetCharacterMovement()->MovementMode==MOVE_None || Hero->Health<=0 || Pet->Health<=0)return End(false,TEXT("motion"));
        if(Phase==7){if(FPlatformTime::Seconds()<ShotUntil)return true;return GatherSupply();}
        if(Phase==-1)
        {
            // 读档刚重建实体时先等导航完成，之后只做一次有诊断的选路，不反复试跑掩盖无路。
            if(World->GetTimeSeconds()-At<.1 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))
            {if(World->GetTimeSeconds()-At>5)return End(false,TEXT("navigation-not-ready"));return true;}
            for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt && It->RequiredSpecialty==EWorkSpecialty::Farming && It->StoredFood==0 && !It->Crop->IsOccupied() && Plan(*It))
            {Farm=*It;FarmId=It->PersistentId;Phase=0;UE_LOG(LogTemp,Display,TEXT("CF_HOME_CARE_FARM_SELECTED id=%s position=%s"),*FarmId.ToString(),*Farm->GetActorLocation().ToString());return true;}
            return End(false,TEXT("farm-path"));
        }
        const int32 Units=Hero->Inventory->Count(EFrontierItem::Seeds)+Hero->Inventory->Count(EFrontierItem::Food)+Farm->Crop->Seeds+(Farm->Crop->bPlanted?1:0)+Farm->StoredFood;
        const bool Preparing=Resupply && !Supplied;
        if((Preparing?(Units!=(FoodMade?1:0) || Hero->Inventory->Count(EFrontierItem::Material)!=(FoodMade?0:2) || Hero->Inventory->Count(EFrontierItem::Fiber)!=1):
            (Units!=SpareSeeds()+(Fed?0:1) || Hero->Inventory->Count(EFrontierItem::Material)!=0 || (Resupply && Hero->Inventory->Count(EFrontierItem::Fiber)!=0))) || Pet->Cargo->Count()!=0 || !SameWorld())return End(false,TEXT("conservation"));
        // 截图等待仍逐帧采样真实位移和物资，不暂停生产或伙伴。
        if(FPlatformTime::Seconds()<ShotUntil)return true;
        if(Phase==6)
        {
            if(World->GetTimeSeconds()-At<1)return true;
            if(Pet->WorkSite!=Farm.Get() || Farm->Crop->Seeds!=1 || Farm->Crop->bPlanted || !Hero->Feedback.Contains(TEXT("通道被角色占用")))return End(false,TEXT("blocked-feedback"));
            if(Photo(TEXT("Blocked"),Farm->GetActorLocation()+FVector(0,0,30)))return true;
            UE_LOG(LogTemp,Display,TEXT("CF_HOME_CARE_BLOCKED_HINT PASS taskRetained=1 reserved=1"));
            if(!Plan(Farm.Get(),true))return End(false,TEXT("clear-stand-path"));Phase=4;return true;
        }
        if(Phase==4)
        {
            const int32 Result=Move();if(Result<0)return End(false,TEXT("clear-stand-blocked"));if(!Result)return true;
            Face(Farm->GetActorLocation()+FVector(0,0,30));Phase=1;WaitAt=-1;
            UE_LOG(LogTemp,Display,TEXT("CF_HOME_CARE_CLEAR_POSITION PASS input=1 position=%s"),*H.ToString());return true;
        }
        if(Phase==5)
        {
            if(!Mid || !Farm->Crop->bPlanted || Pet->WorkSite!=Farm.Get() || HeroTravel<100 || Pet->Experience!=8)return End(false,TEXT("resume-state"));
            UE_LOG(LogTemp,Display,TEXT("CF_HOME_CARE_RESUME PASS taskRetained=1 planted=1 walked=1 xp=8"));return End(true,TEXT("resume"));
        }
        if(Phase==0)
        {
            const int32 Result=Move();if(Result<0)return End(false,TEXT("farm-path-blocked"));if(!Result)return true;
            Face(Farm->GetActorLocation()+FVector(0,0,30));
            if(Hero->TraceFacility()!=Farm.Get()){if(WaitAt<0)WaitAt=World->GetTimeSeconds();if(World->GetTimeSeconds()-WaitAt>5)return End(false,TEXT("farm-aim"));return true;}
            if(Preparing)
            {
                if(!FoodMade)
                {if(!CFInputQA::Press(Hero,EKeys::Three) || Hero->Inventory->Count(EFrontierItem::Material)!=0 || Hero->Inventory->Count(EFrontierItem::Food)!=1)return End(false,TEXT("supply-food-craft"));FoodMade=true;}
                if(!Hero->IsBackpackOpen())
                {
                    const auto* Catalog=UCraftingCatalog::Load();if(!Catalog || !CFInputQA::Press(Hero,EKeys::C) || !Hero->IsBackpackOpen())return End(false,TEXT("supply-menu"));
                    const int32 Index=Catalog->Recipes.IndexOfByPredicate([](const FFrontierRecipe& R){return R.Id==TEXT("PlantingSeeds");});
                    if(Index<0)return End(false,TEXT("supply-recipe"));for(int32 I=0;I<Index;++I)if(!CFInputQA::Press(Hero,EKeys::Down))return End(false,TEXT("supply-menu-select"));
                    if(Photo(TEXT("Recipe"),Farm->GetActorLocation()+FVector(0,0,30)))return true;
                }
                if(!CFInputQA::Press(Hero,EKeys::Enter) || Hero->Inventory->Count(EFrontierItem::Seeds)!=3 || Hero->Inventory->Count(EFrontierItem::Fiber)!=0 || Hero->Inventory->Count(EFrontierItem::Food)!=0 || !CFInputQA::Press(Hero,EKeys::C) || Hero->IsBackpackOpen())return End(false,TEXT("supply-seed-craft"));
                Supplied=true;UE_LOG(LogTemp,Display,TEXT("CF_HOME_RESUPPLY_CRAFT PASS material=0 fiber=0 food=0 seeds=3 menuInput=1"));
            }
            if(Photo(TEXT("Farm"),Farm->GetActorLocation()+FVector(0,0,30)))return true;
            if(!CFInputQA::Press(Hero,EKeys::F2) || Hero->Inventory->Count(EFrontierItem::Seeds)!=SpareSeeds() || Farm->Crop->Seeds!=1 || !CFInputQA::Press(Hero,EKeys::E) || Pet->WorkSite!=Farm.Get())return End(false,TEXT("seed-order-input"));
            UE_LOG(LogTemp,Display,TEXT("CF_HOME_CARE_ORDER PASS seeds=%d reserved=1 aimed=1 farm=%s"),SpareSeeds(),*FarmId.ToString());
            if(!Plan(Farm.Get(),true))return End(false,TEXT("clear-stand-path"));Phase=4;WaitAt=-1;return true;
        }
        if(Phase==1)
        {
            if(Pet->WorkSite!=Farm.Get())return End(false,TEXT("production-task"));
            if(!Mid && Farm->Crop->bPlanted && Farm->Crop->GrowthWork>0 && Farm->Crop->GrowthWork<UFarmCropComponent::RequiredWork)
            {if(!Hero->SaveProgress(TEXT("HomeCareWork")))return End(false,TEXT("work-save"));Mid=true;UE_LOG(LogTemp,Display,TEXT("CF_HOME_CARE_WORK PASS planted=1 seeds=0 xp=%d"),StartXP());if(ResumeOnly)Phase=5;if(Photo(TEXT("Working"),Farm->GetActorLocation()+FVector(0,0,30)) || ResumeOnly)return true;}
            if(Farm->StoredFood==0)return true;
            if(!Mid || Farm->StoredFood!=1 || Pet->Experience!=StartXP()+1 || Farm->Crop->Seeds!=0 || Farm->Crop->bPlanted)return End(false,TEXT("harvest-state"));
            if(WaitAt<0){WaitAt=World->GetTimeSeconds();WaitStamina=Pet->Stamina;Photo(TEXT("Harvest"),Farm->GetActorLocation()+FVector(0,0,30));return true;}
            if(World->GetTimeSeconds()-WaitAt<5)return true;
            if(!FMath::IsNearlyZero(WaitStamina-Pet->Stamina,.01f))return End(false,TEXT("empty-seed-wait"));
            Face(Farm->GetActorLocation()+FVector(0,0,30));if(Hero->TraceFacility()!=Farm.Get() || !CFInputQA::Press(Hero,EKeys::O) || Hero->Inventory->Count(EFrontierItem::Food)!=1 || Farm->StoredFood!=0 || !CFInputQA::Press(Hero,EKeys::O) || Hero->Inventory->Count(EFrontierItem::Food)!=1 || !CFInputQA::Press(Hero,EKeys::F) || Pet->WorkSite)return End(false,TEXT("withdraw-follow-input"));
            UE_LOG(LogTemp,Display,TEXT("CF_HOME_CARE_HARVEST PASS food=1 xp=%d noDrain=1 duplicate=0"),StartXP()+1);if(!Plan(nullptr))return End(false,TEXT("return-path"));Phase=2;WaitAt=-1;return true;
        }
        if(Phase==2)
        {
            const int32 Result=Move();if(Result<0)return End(false,TEXT("return-blocked"));if(!Result)return true;Face(P+FVector(0,0,20));
            if(Pet->GetDistanceTo(Hero)>250 || Hero->TraceCreature()!=Pet.Get()){if(WaitAt<0)WaitAt=World->GetTimeSeconds();if(World->GetTimeSeconds()-WaitAt>15)return End(false,TEXT("join"));return true;}
            if(Resupply && Pet->Nutrition>80)
            {
                // 不把刚吃过的伙伴强行再喂一次；验证拒绝不耗粮后，让原需求自然更新。
                if(!HungerChecked)
                {
                    const float Before=Pet->Nutrition;
                    if(!CFInputQA::Press(Hero,EKeys::J) || !Hero->Feedback.Contains(TEXT("伙伴不饿")) || Hero->Inventory->Count(EFrontierItem::Food)!=1 || Pet->BondPoints!=StartBond() || Pet->Nutrition!=Before)return End(false,TEXT("not-hungry-rejection"));
                    HungerChecked=true;UE_LOG(LogTemp,Display,TEXT("CF_HOME_RESUPPLY_HUNGER_GATE PASS nutrition=%.3f food=1 bond=%d"),Before,Pet->BondPoints);
                    Photo(TEXT("NotHungry"),P+FVector(0,0,20));
                }
                return true;
            }
            const float Nutrition=Pet->Nutrition,Stamina=Pet->Stamina;
            if(!CFInputQA::Press(Hero,EKeys::J) || Hero->Inventory->Count(EFrontierItem::Food)!=0 || Pet->BondPoints!=StartBond()+3 || !FMath::IsNearlyEqual(Pet->Nutrition,FMath::Min(100.f,Nutrition+35),.01f) || !FMath::IsNearlyEqual(Pet->Stamina,FMath::Min(100.f,Stamina+15),.01f))
            {UE_LOG(LogTemp,Display,TEXT("CF_HOME_CARE_FEED_STATE nutrition=%.3f before=%.3f stamina=%.3f beforeStamina=%.3f bond=%d food=%d feedback=%s"),Pet->Nutrition,Nutrition,Pet->Stamina,Stamina,Pet->BondPoints,Hero->Inventory->Count(EFrontierItem::Food),*Hero->Feedback);return End(false,TEXT("feed-input"));}
            Fed=true;Phase=3;Photo(TEXT("Fed"),P+FVector(0,0,20));return true;
        }
        Face(P+FVector(0,0,20));if(!CFInputQA::Press(Hero,EKeys::J) || Pet->BondPoints!=StartBond()+3 || !Hero->Feedback.Contains(TEXT("冷却")))return End(false,TEXT("feed-cooldown"));
        const auto SavedH=Hero->GetActorLocation(),SavedP=Pet->GetActorLocation();
        if(!Hero->SaveProgress(TEXT("HomeCareDone")) || !Hero->LoadProgress(TEXT("HomeCareDone")) || !Bind() || !Farm.IsValid() || !SameWorld() || !Hero->GetActorLocation().Equals(SavedH,.01) || !Pet->GetActorLocation().Equals(SavedP,.01) || Pet->Experience!=StartXP()+1 || Pet->BondPoints!=StartBond()+3 || Pet->Activity!=ECreatureActivity::Follow || Pet->WorkSite || Hero->Inventory->Count(EFrontierItem::Seeds)!=SpareSeeds() || Hero->Inventory->Count(EFrontierItem::Food)!=0 || Farm->StoredFood!=0 || Farm->Crop->IsOccupied() || HeroTravel<1000)return End(false,TEXT("done-save"));
        UE_LOG(LogTemp,Display,TEXT("CF_HOME_CARE_LIFE PASS version=38 modules=25 facilities=2 seeds=%d food=0 xp=%d bond=%d observe=0 positionRetained=1"),SpareSeeds(),StartXP()+1,StartBond()+3);return End(true,TEXT("complete"));
    }
};
}
void RunHomeCareScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto State=MakeShared<FHomeCareTest>();State->World=World;State->Hero=Hero;State->Read=Read;State->Deadline=FPlatformTime::Seconds()+150;
    State->ReservedRead=FParse::Param(FCommandLine::Get(),TEXT("CFHomeCareReservedRead"));
    State->Resupply=FParse::Param(FCommandLine::Get(),TEXT("CFHomeCareResupply"));State->Supplied=State->Resupply && Read;
    if(State->Resupply && State->ReservedRead){State->End(false,TEXT("resupply-reserved-mode"));return;}
    State->ResumeOnly=FParse::Param(FCommandLine::Get(),TEXT("CFHomeCareResumeOnly"));
    if(State->ResumeOnly && !State->ReservedRead){State->End(false,TEXT("resume-only-mode"));return;}
    if(State->ReservedRead && !Read){State->End(false,TEXT("reserved-read-mode"));return;}
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("HomeCareInput"),0));
    if(!Source || Source->FormatVersion!=38 || Source->HomeModules.Num()!=25 || Source->Facilities.Num()!=2 || !Hero->LoadProgress(TEXT("HomeCareInput")) || !State->Bind() || State->PetId!=FGuid(0x1209AAFE,0x4D5C4544,0x1D65AF94,0x1B0435D1) || State->Pet->Experience!=State->StartXP() || State->Pet->BondPoints!=State->StartBond() || Hero->Inventory->Count(EFrontierItem::Seeds)!=(State->Resupply?(Read?2:0):(Read?0:1)) || Hero->Inventory->Count(EFrontierItem::Food)!=0 || Hero->Inventory->Count(EFrontierItem::Material)!=0 || (State->Resupply && Hero->Inventory->Count(EFrontierItem::Fiber)!=0)){State->End(false,TEXT("source"));return;}
    State->House=HomeLayout::Collect(World);State->Facilities=Source->Facilities;State->Nickname=State->Pet->Nickname;State->Input=NewObject<UHomeMovementProbe>(Hero);State->Input->RegisterComponent();
    if(Read)
    {
        if(!State->Farm.IsValid() || State->Farm->StoredFood!=0 || (State->ReservedRead?(State->Farm->Crop->bPlanted || State->Farm->Crop->Seeds!=1):(!State->Farm->Crop->bPlanted || State->Farm->Crop->Seeds!=0))){State->End(false,TEXT("work-source"));return;}
        const auto* Partner=Source->Creatures.FindByPredicate([&](const FCreatureSaveRecord& R){return R.Id==State->PetId;});
        if(!Partner || !Hero->GetActorLocation().Equals(Source->PlayerTransform.GetLocation(),.01) || !State->Pet->GetActorLocation().Equals(Partner->Transform.GetLocation(),.01)){State->End(false,TEXT("restore-position"));return;}
        State->Phase=1;State->Mid=!State->ReservedRead;
        if(State->ReservedRead){State->Phase=6;State->At=World->GetTimeSeconds();}
        if(State->ReservedRead){UE_LOG(LogTemp,Display,TEXT("CF_HOME_CARE_RESTORE_RESERVED PASS version=38 planted=0 reserved=1 xp=8 positionRetained=1 nutrition=%.3f stamina=%.3f"),State->Pet->Nutrition,State->Pet->Stamina);}
        else {UE_LOG(LogTemp,Display,TEXT("CF_HOME_CARE_RESTORE PASS version=38 planted=1 seeds=0 xp=%d"),State->StartXP());}
    }
    else
    {
        for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt)UE_LOG(LogTemp,Display,TEXT("CF_HOME_CARE_SOURCE_FARM id=%s position=%s seeds=%d planted=%d growth=%.3f food=%d"),*It->PersistentId.ToString(),*It->GetActorLocation().ToString(),It->Crop->Seeds,It->Crop->bPlanted,It->Crop->GrowthWork,It->StoredFood);
        State->Phase=-1;State->At=World->GetTimeSeconds();UE_LOG(LogTemp,Display,TEXT("CF_HOME_CARE_NAV_WAIT building=%d"),UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World));
        if(State->Resupply)State->Phase=7;
    }
    State->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFHomeCarePreview"));
    if(State->Preview){State->ImageRoot=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("HomeCarePreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*State->ImageRoot,true);UE_LOG(LogTemp,Display,TEXT("CF_HOME_CARE_PREVIEW_ROOT %s"),*State->ImageRoot);}
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float){return State->Tick();}));
}
