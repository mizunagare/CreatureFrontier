#include "ConstructionScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "HomeBuildingComponent.h"
#include "HomeModule.h"
#include "HomeLayout.h"
#include "HomeScenario.h"
#include "InventoryComponent.h"
#include "CompanionConstructionComponent.h"
#include "CompanionCargoComponent.h"
#include "FrontierSaveGame.h"
#include "ResourceNode.h"
#include "ResourceDefinition.h"
#include "WildEncounter.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/Ticker.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "PlayerGatheringComponent.h"
#include "InputRoutingQA.h"
#include "Components/CapsuleComponent.h"
#include "StorageInteractionComponent.h"
#include "BaseBuildingComponent.h"
#include "InputKeyEventArgs.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"

namespace
{
bool MovementContinuous(const ACharacter* Pawn,FVector Before,FVector Position,FVector PreviousVelocity,double Delta)
{
    if(!Pawn || !FMath::IsFinite(Delta) || Delta<0 || Position.ContainsNaN())return false;
    const auto* Movement=Pawn->GetCharacterMovement();if(!Movement)return false;
    // 用游戏时间与角色真实运动上限检查，不把渲染长帧中的合法行走当作瞬移。
    // 平常短帧同样受此上限约束；竖向另计重力与原有跨阶高度，不改任何移动参数。
    const double Horizontal=FMath::Max(static_cast<double>(Movement->GetMaxSpeed()),PreviousVelocity.Size2D())*Delta+3;
    const double Vertical=FMath::Max(FMath::Abs(Pawn->GetVelocity().Z),FMath::Abs(PreviousVelocity.Z))*Delta+
        .5*FMath::Abs(Pawn->GetWorld()->GetGravityZ())*Delta*Delta+Movement->MaxStepHeight+3;
    return FVector::Dist2D(Before,Position)<=Horizontal && FMath::Abs(Position.Z-Before.Z)<=Vertical;
}
struct FPlatformBuildTest
{
    UWorld* World;ACFPlayerCharacter* Hero;TWeakObjectPtr<ACreatureCharacter> Pet;TWeakObjectPtr<AHomeModule> Box,Site;
    TArray<FHomeModuleRecord> Original;int32 Part=0,Phase=0,Harvested=0,Point=0;bool Mid=false,Preview=false,DeckWalked=false,PetDeck=false,Inside=false,PetInside=false,HomeEntered=false,PetHomeEntered=false;
    double Deadline=0,NextPhoto=0,PetTravel=0,HeroTravel=0,ProgressAt=0,PreviousGameTime=0,LastDelta=0,LastStep=0;FVector PreviousPet,PreviousHero,ProgressAnchor,PreviousPetVelocity,PreviousHeroVelocity;
    FString ImageRoot;TSet<FString> Photos;TWeakObjectPtr<UHomeMovementProbe> Input;
    const TArray<EHomeModuleKind> Kinds={EHomeModuleKind::Floor,EHomeModuleKind::Floor,EHomeModuleKind::Wall,EHomeModuleKind::Wall,EHomeModuleKind::Roof};
    const TArray<FVector> Places={FVector(-1200,-800,-40),FVector(-1200,-400,-40),FVector(-1400,-400,-30),FVector(-1000,-400,-30),FVector(-1200,-400,280)};
    const TArray<FVector> Route={FVector(-1200,-800,60),FVector(-1200,-300,60),FVector(-1200,-800,60),FVector(-650,-800,60),FVector(-1200,-800,60),FVector(-1500,-800,40)};
    AHomeModule* Find(int32 Index) const
    {for(TActorIterator<AHomeModule> It(World);It;++It)if(!It->bPreview && It->GetKind()==Kinds[Index] && It->GetActorLocation().Equals(Places[Index],.01))return *It;return nullptr;}
    void Face(FVector Target){const auto Rotation=(Target-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(Rotation);Hero->FirstPersonCamera->SetWorldRotation(Rotation);}
    void Observer(FVector Position){Hero->SetActorLocation(Position);PreviousHero=Hero->GetActorLocation();}
    void ObserveSite()
    {
        // 选择真实可站立高度和无遮挡方向，不让落地后相机偏离料箱，也不隔墙取景。
        Observer(Part==4?FVector(-1200,-750,60.15):Part==3?FVector(-1250,-750,60.15):FVector(Places[Part].X-350,Places[Part].Y,40.15));
    }
    bool End(bool Pass,const TCHAR* Reason)
    {
        if(Input.IsValid())Input->Direction=FVector::ZeroVector;
        if(!Pass){const bool Saved=Hero->SaveProgress(TEXT("PlatformBlocked"));UE_LOG(LogTemp,Display,TEXT("CF_PLATFORM_DIAGNOSTIC saved=%d step=%.2f delta=%.4f speed=%.1f mode=%d collision=%d tick=%d health=%.1f"),Saved,LastStep,LastDelta,Pet.IsValid()?Pet->GetVelocity().Size():-1.,Pet.IsValid()?static_cast<int32>(Pet->GetCharacterMovement()->MovementMode):-1,Pet.IsValid() && Pet->GetActorEnableCollision(),Pet.IsValid() && Pet->IsActorTickEnabled(),Pet.IsValid()?Pet->Health:-1.);}
        UE_LOG(LogTemp,Display,TEXT("CF_PLATFORM_BUILD %s reason=%s part=%d phase=%d material=%d heroTravel=%.1f petTravel=%.1f hero=%s pet=%s feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Reason,Part,Phase,Hero->Inventory->Count(EFrontierItem::Material),HeroTravel,PetTravel,*Hero->GetActorLocation().ToString(),Pet.IsValid()?*Pet->GetActorLocation().ToString():TEXT("无"),*Hero->Feedback);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    bool Photo(const TCHAR* Name)
    {
        if(!Preview || Photos.Contains(Name))return false;Photos.Add(Name);
        if(Site.IsValid())Face(FString(Name)==TEXT("RoofDone")?Site->GetActorLocation():FString(Name)==TEXT("CanopyOccupied")?Pet->GetActorLocation()+FVector(0,0,40):Site->ConstructionAnchor()+FVector(0,0,12));
        if(FString(Name).Contains(TEXT("Pending")))
        {
            if(AHomeModule::TraceConstruction(Hero)!=Site.Get())return End(false,TEXT("preview-aim"));
            UE_LOG(LogTemp,Display,TEXT("CF_PLATFORM_PREVIEW_AIM PASS scene=%s target=1"),Name);
        }
        FScreenshotRequest::RequestScreenshot(ImageRoot/(FString(Name)+TEXT(".png")),false,false);NextPhoto=FPlatformTime::Seconds()+.2;return true;
    }
    bool Bind()
    {
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController())Pet=*It;
        for(TActorIterator<AHomeModule> It(World);It;++It)if(It->GetKind()==EHomeModuleKind::Storage && It->GetActorLocation().Equals(FVector(-900,-900,-30),.01))Box=*It;
        if(!Pet.IsValid() || !Box.IsValid() || Pet->PersistentId!=FGuid(0x1209AAFE,0x4D5C4544,0x1D65AF94,0x1B0435D1) || Pet->Experience!=8 || Pet->BondPoints!=9)return false;
        PreviousPet=Pet->GetActorLocation();PreviousHero=Hero->GetActorLocation();PreviousGameTime=World->GetTimeSeconds();
        PreviousPetVelocity=Pet->GetVelocity();PreviousHeroVelocity=Hero->GetVelocity();
        for(TActorIterator<AWildEncounter> It(World);It;++It)It->SetActorTickEnabled(false);
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(*It!=Pet.Get()){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();}
        return true;
    }
    int32 Unspent() const
    {int32 Result=38;for(int32 Index=0;Index<Part;++Index)Result-=UHomeModuleDefinition::LoadDefault(Kinds[Index])->MaterialCost;return Result;}
    bool Queue()
    {
        ObserveSite();Hero->HomeBuilding->Selected=Kinds[Part];
        const FGuid Parent=Part<2?FGuid():Find(1)->PersistentId;
        Site=Hero->HomeBuilding->QueueAt(FTransform(FRotator(0,Part==2 || Part==3?90:0,0),Places[Part]),Parent);
        if(!Site.IsValid() || Site->PaidMaterials!=0)return false;
        if(Part==4)
        {
            auto* Wall=Find(2);const auto Before=Hero->Inventory->GetStacks();
            if(!Wall || Hero->HomeBuilding->Dismantle(Wall) || !IsValid(Wall) || !Before.OrderIndependentCompareEqual(Hero->Inventory->GetStacks()))return false;
            UE_LOG(LogTemp,Display,TEXT("CF_PLATFORM_ROOF_SUPPORT PASS retained=1 refund=0"));
        }
        Face(Site->ConstructionAnchor()+FVector(0,0,25));
        if(!Pet->Construction->Assign(Site.Get(),Box.Get(),Hero))return false;
        Mid=false;
        UE_LOG(LogTemp,Display,TEXT("CF_PLATFORM_QUEUE PASS part=%d cost=%d supplied=0 paid=0 modules=%d anchor=%s"),Part,Site->Definition->MaterialCost,HomeLayout::Collect(World).Num(),*Site->ConstructionAnchor().ToString());
        return true;
    }
    bool Guards()
    {
        if(Part!=0)return true;
        const auto Layout=HomeLayout::Collect(World);FString Reason;
        Hero->HomeBuilding->Selected=EHomeModuleKind::Wall;
        if(HomeLayout::FindFloor(Places[0],Layout).IsValid() || HomeLayout::SupportsFacility(FTransform(FRotator::ZeroRotator,FVector(-1200,-800,5),FVector(2,1,.7)),Layout) || Hero->HomeBuilding->QueueAt(FTransform(FVector(-1200,-1000,-30)),Site->PersistentId))return false;
        if(!Hero->SaveProgress(TEXT("PlatformBad")))return false;
        auto* Bad=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("PlatformBad"),0));if(!Bad)return false;
        Bad->FormatVersion=36;
        if(!UGameplayStatics::SaveGameToSlot(Bad,TEXT("PlatformBad"),0) || Hero->LoadProgress(TEXT("PlatformBad")) || !Site.IsValid() || !Site->bUnderConstruction)return false;
        UE_LOG(LogTemp,Display,TEXT("CF_PLATFORM_GUARDS PASS pendingSupport=0 facilitySupport=0 bad36Rejected=1 retained=1"));return true;
    }
    bool Tick()
    {
        if(!Pet.IsValid() || !Box.IsValid() || FPlatformTime::Seconds()>Deadline)return End(false,TEXT("lifetime"));
        const auto P=Pet->GetActorLocation(),H=Hero->GetActorLocation();const double Step=FVector::Dist(P,PreviousPet);PetTravel+=Step;
        LastStep=Step;LastDelta=World->GetTimeSeconds()-PreviousGameTime;PreviousGameTime=World->GetTimeSeconds();
        const bool Continuous=MovementContinuous(Pet.Get(),PreviousPet,P,PreviousPetVelocity,LastDelta);PreviousPet=P;PreviousPetVelocity=Pet->GetVelocity();
        if(!Continuous || !Pet->IsActorTickEnabled() || !Pet->GetActorEnableCollision() || Pet->GetCharacterMovement()->MovementMode==MOVE_None || Pet->Health<=0 || Hero->Health<=0)return End(false,TEXT("continuity"));
        if(Phase==4){const double HeroStep=FVector::Dist(H,PreviousHero);const bool HeroContinuous=MovementContinuous(Hero,PreviousHero,H,PreviousHeroVelocity,LastDelta);PreviousHero=H;PreviousHeroVelocity=Hero->GetVelocity();HeroTravel+=HeroStep;if(!HeroContinuous)return End(false,TEXT("hero-continuity"));}
        // 等待截图时仍逐帧核对角色连续性，只延后场景动作，不能把多帧位移当作单帧跳变。
        if(FPlatformTime::Seconds()<NextPhoto)return true;
        if(Phase==0)
        {
            if(Harvested==33)
            {
                if(Hero->Inventory->Count(EFrontierItem::Material)!=38 || !Hero->Inventory->TransferTo(Box->StorageInventory,{{EFrontierItem::Material,38}}) || !Queue() || !Guards())return End(false,TEXT("funding"));
                Phase=1;Photo(TEXT("FloorPending"));return true;
            }
            for(TActorIterator<AResourceNode> It(World);It;++It)if(It->Definition && It->Definition->OutputItem==EFrontierItem::Material && It->Remaining>0 && It->HarvestCooldown<=0)
            {Observer(It->GetActorLocation()+FVector(0,-180,60));if(It->TryHarvest(Hero,Hero)){++Harvested;UE_LOG(LogTemp,Display,TEXT("CF_PLATFORM_HARVEST count=%d material=%d"),Harvested,Hero->Inventory->Count(EFrontierItem::Material));return true;}}
            return true;
        }
        if(Phase==1)
        {
            if(!Site.IsValid())return End(false,TEXT("site"));
            const int32 Total=Hero->Inventory->Count(EFrontierItem::Material)+Box->StorageInventory->Count(EFrontierItem::Material)+Site->StorageInventory->Count(EFrontierItem::Material)+Pet->Cargo->Count();
            if(Total!=Unspent()-(Site->bUnderConstruction?0:Site->Definition->MaterialCost))return End(false,TEXT("conservation"));
            if(!Mid && Pet->Cargo->Count()==3 && (Part==0 || Part==4))
            {
                const auto Slot=Part==0?TEXT("PlatformFloorMid"):TEXT("PlatformRoofMid");
                if(!Hero->SaveProgress(Slot))return End(false,TEXT("mid-save"));Mid=true;
                UE_LOG(LogTemp,Display,TEXT("CF_PLATFORM_MID PASS version=%d part=%d carried=3 paid=0"),FrontierCurrentSaveVersion,Part);
            }
            if(Site->bUnderConstruction)return true;
            if(Site->PaidMaterials!=Site->Definition->MaterialCost || Pet->Construction->GetSite() || Pet->Cargo->Count()!=0)return End(false,TEXT("completion"));
            UE_LOG(LogTemp,Display,TEXT("CF_PLATFORM_PART PASS part=%d paid=%d remaining=%d"),Part,Site->PaidMaterials,Total);
            if(Part==0 && Photo(TEXT("FloorDone"))){Phase=2;return true;}
            if(Part==4)
            {
                if(Photo(TEXT("RoofDone"))){Phase=3;return true;}
                Phase=3;
            }
            else {Phase=2;return true;}
        }
        if(Phase==2)
        {++Part;if(!Queue())return End(false,TEXT("next-queue"));Phase=1;if(Part==4)Photo(TEXT("RoofPending"));return true;}
        if(Phase==3)
        {
            Observer(FVector(-1500,-800,40.15));Face(FVector(-1200,-800,60));ProgressAt=World->GetTimeSeconds();Phase=5;return true;
        }
        if(Phase==5)
        {
            if(World->GetTimeSeconds()-ProgressAt>15)return End(false,TEXT("outside-rejoin"));
            if(P.X>=-1440 || Pet->GetDistanceTo(Hero)>=250)return true;
            DeckWalked=false;PetDeck=false;Inside=false;PetInside=false;HomeEntered=false;PetHomeEntered=false;
            Input=NewObject<UHomeMovementProbe>(Hero);Input->RegisterComponent();PreviousHero=H;PreviousHeroVelocity=Hero->GetVelocity();ProgressAnchor=H;ProgressAt=World->GetTimeSeconds();Phase=4;
            UE_LOG(LogTemp,Display,TEXT("CF_PLATFORM_JOIN PASS outside=1 continuous=1"));return true;
        }
        if(Phase==4)
        {
            DeckWalked|=FMath::Abs(H.X+1200)<150 && FMath::Abs(H.Y+800)<150 && H.Z>55;
            PetDeck|=FMath::Abs(P.X+1200)<150 && FMath::Abs(P.Y+800)<150 && P.Z>55;
            const bool HeroHere=FMath::Abs(H.X+1200)<150 && FMath::Abs(H.Y+400)<150 && H.Z>55;
            const bool PetHere=FMath::Abs(P.X+1200)<150 && FMath::Abs(P.Y+400)<150 && P.Z>55;
            const auto InHome=[](FVector Position){return Position.X>-960 && Position.X<-240 && Position.Y>-960 && Position.Y<-640 && Position.Z>55;};
            Inside|=HeroHere;PetInside|=PetHere;HomeEntered|=InHome(H);PetHomeEntered|=InHome(P);
            if(HeroHere && PetHere)Photo(TEXT("CanopyOccupied"));
            if(FVector::Dist2D(H,ProgressAnchor)>10){ProgressAnchor=H;ProgressAt=World->GetTimeSeconds();}
            if(Point<Route.Num() && FVector::Dist2D(H,Route[Point])<15)
            {Input->Direction=FVector::ZeroVector;if(Pet->GetDistanceTo(Hero)<250 && (Point!=1 || (HeroHere && PetHere)) && (Point!=3 || (InHome(H) && InHome(P)))){++Point;ProgressAt=World->GetTimeSeconds();}}
            if(Point<Route.Num())
            {
                if(World->GetTimeSeconds()-ProgressAt>10){Hero->SaveProgress(TEXT("PlatformBlocked"));return End(false,TEXT("walk-blocked"));}
                Input->Direction=FVector::Dist2D(H,Route[Point])<15?FVector::ZeroVector:(Route[Point]-H).GetSafeNormal2D();
                if(!Input->Direction.IsNearlyZero())Face(H+Input->Direction*500+FVector(0,0,64));return true;
            }
            Input->Direction=FVector::ZeroVector;
            if(!DeckWalked || !PetDeck || !Inside || !PetInside || !HomeEntered || !PetHomeEntered || HeroTravel<1800 || PetTravel<1500 || !Hero->SaveProgress(TEXT("PlatformDone")) || !Hero->LoadProgress(TEXT("PlatformDone")) || !Bind())return End(false,TEXT("life-save"));
            const auto Records=HomeLayout::Collect(World);if(Records.Num()!=24 || Hero->Inventory->Count(EFrontierItem::Material)!=0 || Hero->Inventory->Count(EFrontierItem::Seeds)!=1 || Hero->Inventory->Count(EFrontierItem::Food)!=0 || Box->StorageInventory->Count(EFrontierItem::Material)!=0)return End(false,TEXT("done-state"));
            for(const auto& Old:Original){const auto* Now=Records.FindByPredicate([&](const FHomeModuleRecord& R){return R.Id==Old.Id;});if(!Now || Now->Definition!=Old.Definition || Now->FloorId!=Old.FloorId || !Now->Transform.Equals(Old.Transform,.01) || Now->PaidMaterials!=Old.PaidMaterials)return End(false,TEXT("original-house"));}
            UE_LOG(LogTemp,Display,TEXT("CF_PLATFORM_LIFE PASS version=%d modules=24 retained=19 floor=1 roof=1 hero=1 pet=1 home=1 petHome=1 material=0 seeds=1 food=0 bond=9"),FrontierCurrentSaveVersion);return End(true,TEXT("complete"));
        }
        return true;
    }
};
}
void RunPlatformConstructionScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto State=MakeShared<FPlatformBuildTest>();State->World=World;State->Hero=Hero;State->Deadline=FPlatformTime::Seconds()+300;
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("PlatformInput"),0));
    if(!Source || (Read?(Source->FormatVersion<37 || Source->FormatVersion>FrontierCurrentSaveVersion):Source->FormatVersion!=36) || !Hero->LoadProgress(TEXT("PlatformInput")) || !State->Bind()){State->End(false,TEXT("source"));return;}
    const auto Position=State->Pet->GetActorLocation();const double Speed=State->Pet->GetCharacterMovement()->GetMaxSpeed();
    if(!MovementContinuous(State->Pet.Get(),Position,Position+FVector(Speed*.4*.8,0,0),FVector::ZeroVector,.4) ||
        MovementContinuous(State->Pet.Get(),Position,Position+FVector(100,0,0),FVector::ZeroVector,1./60) ||
        MovementContinuous(State->Pet.Get(),Position,Position+FVector(0,0,200),FVector::ZeroVector,1./60) ||
        MovementContinuous(State->Pet.Get(),Position,Position+FVector(4,0,0),FVector::ZeroVector,0)){State->End(false,TEXT("motion-guards"));return;}
    UE_LOG(LogTemp,Display,TEXT("CF_PLATFORM_MOTION_GUARDS PASS longFrame=1 horizontalJump=0 verticalJump=0 zeroTimeJump=0"));
    State->Original=HomeLayout::Collect(World);
    State->Original.RemoveAll([State](const FHomeModuleRecord& R){for(int32 Index=0;Index<State->Places.Num();++Index)if(R.Definition.Get()->Kind==State->Kinds[Index] && R.Transform.GetLocation().Equals(State->Places[Index],.01))return true;return false;});
    if(State->Original.Num()!=19 || Source->Facilities.Num()!=2 || Hero->Inventory->Count(EFrontierItem::Material)!=(Read?0:5)){State->End(false,TEXT("original"));return;}
    State->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFPlatformPreview"));State->ImageRoot=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("PlatformPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));
    if(State->Preview){IFileManager::Get().MakeDirectory(*State->ImageRoot,true);UE_LOG(LogTemp,Display,TEXT("CF_PLATFORM_PREVIEW_ROOT %s"),*State->ImageRoot);}
    if(Read)
    {
        for(int32 Index=0;Index<State->Places.Num();++Index)if(auto* Module=State->Find(Index);Module && Module->bUnderConstruction){State->Part=Index;State->Site=Module;}
        if(!State->Site.IsValid() || (State->Part!=0 && State->Part!=4) || State->Pet->Construction->GetSite()!=State->Site.Get() || State->Pet->Cargo->Count()!=3){State->End(false,TEXT("restore"));return;}
        State->Phase=1;State->Mid=true;UE_LOG(LogTemp,Display,TEXT("CF_PLATFORM_RESTORE PASS version=%d part=%d carried=3 retained=19"),Source->FormatVersion,State->Part);
        if(State->Preview)State->ObserveSite();
    }
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float){return State->Tick();}));
}

namespace
{
// 从已交付遮棚继续成长；沿用同一伙伴、箱子、资源、楼板和实体楼梯。
struct FUpperDeckBuildTest
{
    UWorld* World;ACFPlayerCharacter* Hero;TWeakObjectPtr<ACreatureCharacter> Pet;
    TWeakObjectPtr<AHomeModule> Box,Site,Base,StairBase,Stair;TWeakObjectPtr<UHomeMovementProbe> Input;
    TArray<FHomeModuleRecord> Original;FGuid PetId,BoxId;int32 Phase=0,Harvested=0,Point=0;bool Mid=false,HeroUp=false,PetUp=false,Preview=false,StairWaiting=false,StairBlocked=false;
    double Deadline=0,PreviousTime=0,Travel=0,HeroTravel=0,ProgressAt=0,NextPhoto=0;FVector PreviousPet,PreviousHero,PetVelocity,HeroVelocity,Anchor;
    FString ImageRoot;TSet<FString> Photos;
    bool StairWork=false,StairMid=false,StairPendingNav=false;
    bool SupplyPause=false,SupplyResumed=false;double SupplyAt=-1;float SupplyStamina=0;
    bool WalkHarvest=false,HarvestHeld=false,HarvestReturning=false;int32 HarvestBefore=0,HarvestNodeBefore=0,HarvestPoint=0;
    TWeakObjectPtr<AResourceNode> HarvestNode;TArray<FVector> HarvestRoute;double HarvestProgressAt=0,HarvestDistance=0;FVector HarvestAnchor;
    bool WalkSupply=false,StoreReturning=false;int32 StoreAmount=0,StoreNext=0,StoreTrips=0;FVector StoreReturn;double StoreDistance=0;
    bool BuildInput=false,PlanningStairs=false,StoreNeedsAssign=false;FVector PlanningAim;double AssignWaitAt=-1;
    const FVector Upper=FVector(-1200,-400,280);
    // 下楼连续走到平地再会合，不把正常阶梯队形的三维间隔误当作卡路。
    const TArray<FVector> Route={FVector(-1200,-1080,40),FVector(-1200,-900,40),FVector(-1200,-350,360),FVector(-1200,-1080,40),FVector(-1500,-1080,40)};
    AHomeModule* Find(EHomeModuleKind Kind,FVector Position) const
    {for(TActorIterator<AHomeModule> It(World);It;++It)if(!It->bPreview && It->GetKind()==Kind && It->GetActorLocation().Equals(Position,.01))return *It;return nullptr;}
    bool Bind()
    {
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController())Pet=*It;
        Box=Find(EHomeModuleKind::Storage,FVector(-900,-900,-30));Base=Find(EHomeModuleKind::Floor,FVector(-1200,-400,-40));StairBase=Find(EHomeModuleKind::Floor,FVector(-1200,-800,-40));
        if(!Pet.IsValid() || !Box.IsValid() || !Base.IsValid() || !StairBase.IsValid() || Pet->Experience!=8 || Pet->BondPoints!=9 ||
            Pet->PersistentId!=FGuid(0x1209AAFE,0x4D5C4544,0x1D65AF94,0x1B0435D1) || Box->PersistentId!=FGuid(0x835350A5,0x417C7BE7,0x7C965A90,0x5CD6402F))return false;
        PetId=Pet->PersistentId;BoxId=Box->PersistentId;PreviousPet=Pet->GetActorLocation();PreviousHero=Hero->GetActorLocation();
        PetVelocity=Pet->GetVelocity();HeroVelocity=Hero->GetVelocity();PreviousTime=World->GetTimeSeconds();
        for(TActorIterator<AWildEncounter> It(World);It;++It)It->SetActorTickEnabled(false);
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(*It!=Pet.Get()){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();}
        return true;
    }
    void Observe(FVector Position){Hero->SetActorLocation(Position);PreviousHero=Position;}
    void FaceHarvest(FVector Target)
    {
        const auto R=(Target-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
        Hero->SetActorRotation(FRotator(0,R.Yaw,0));Hero->FirstPersonCamera->SetRelativeRotation(FRotator::ZeroRotator);
        Hero->GetController()->SetControlRotation(R);Hero->FirstPersonCamera->SetWorldRotation(R);
    }
    bool PlanHarvest(AResourceNode* Node)
    {
        auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);if(!Nav)return false;
        TArray<FVector> Best;double Length=DBL_MAX;
        for(int32 I=0;I<(Node?8:1);++I)
        {
            const double A=I*PI/4;const FVector Candidate=Node?Node->GetActorLocation()+FVector(FMath::Cos(A)*220,FMath::Sin(A)*220,0):FVector(-1500,-800,40);
            FNavLocation Floor;if(!Nav->ProjectPointToNavigation(Candidate,Floor,FVector(60,60,160)))continue;
            const auto* Capsule=Hero->GetCapsuleComponent();const FVector Goal=Floor.Location+FVector(0,0,Capsule->GetScaledCapsuleHalfHeight());
            FCollisionQueryParams Q(SCENE_QUERY_STAT(UpperHarvestStand),false,Hero);
            if(World->OverlapBlockingTestByChannel(Goal,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius()-1,Capsule->GetScaledCapsuleHalfHeight()-1),Q))continue;
            if(Node){FHitResult Hit;if(World->LineTraceSingleByChannel(Hit,Goal,Node->GetActorLocation(),ECC_Visibility,Q) && Hit.GetActor()!=Node)continue;}
            auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Hero->GetActorLocation(),Floor.Location,Hero);
            if(Path && Path->IsValid() && !Path->IsPartial() && Path->PathPoints.Num()>1 && Path->GetPathLength()<Length){Length=Path->GetPathLength();Best=Path->PathPoints;}
        }
        if(Best.IsEmpty())return false;
        HarvestRoute=MoveTemp(Best);HarvestPoint=1;HarvestAnchor=Hero->GetActorLocation();HarvestProgressAt=World->GetTimeSeconds();return true;
    }
    bool TickWalkHarvest()
    {
        if(HarvestRoute.Num())
        {
            const auto Here=Hero->GetActorLocation();
            while(HarvestPoint<HarvestRoute.Num() && FVector::Dist2D(Here,HarvestRoute[HarvestPoint])<16)++HarvestPoint;
            if(HarvestPoint<HarvestRoute.Num())
            {
                const auto Direction=(HarvestRoute[HarvestPoint]-Here).GetSafeNormal2D();Input->Direction=Direction;FaceHarvest(Here+Direction*500+FVector(0,0,64));
                if(FVector::Dist2D(Here,HarvestAnchor)>20){HarvestAnchor=Here;HarvestProgressAt=World->GetTimeSeconds();}
                if(World->GetTimeSeconds()-HarvestProgressAt>5)return End(false,TEXT("harvest-path-blocked"));
                return true;
            }
            Input->Direction=FVector::ZeroVector;
            // 等正常制动，不停止移动组件或改写角色坐标。
            if(Hero->GetVelocity().Size2D()>3)return true;
            HarvestRoute.Reset();
            if(HarvestReturning)
            {
                if(Harvested!=15 || Hero->Inventory->Count(EFrontierItem::Material)!=20 || HarvestDistance<1000)return End(false,TEXT("harvest-return-budget"));
                CFInputQA::Release(Hero,EKeys::LeftShift);
                UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_WALK_HARVEST PASS count=15 material=20 distance=%.1f input=1 returned=1"),HarvestDistance);
                if(BuildInput){Observe(FVector(-1200,-750,60.15));return BeginPlan(false);}
                if(!Queue())return End(false,TEXT("queue"));
                if(WalkSupply)return BeginStore(8,1);
                Phase=1;return true;
            }
        }
        if(HarvestHeld)
        {
            auto* Node=HarvestNode.Get();if(!IsValid(Node))return End(false,TEXT("harvest-target-lost"));
            const int32 Gained=Hero->Inventory->Count(EFrontierItem::Material)-HarvestBefore;
            if(Gained<0 || Node->Remaining!=HarvestNodeBefore-Gained)return End(false,TEXT("harvest-resource-budget"));
            Harvested=Hero->Inventory->Count(EFrontierItem::Material)-5;
            if(Harvested==15 || Node->Remaining==0)
            {
                if(!CFInputQA::Release(Hero,EKeys::M) || Hero->Gathering->IsGathering())return End(false,TEXT("harvest-release"));
                HarvestHeld=false;HarvestNode.Reset();
                UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_HARVEST count=%d material=%d"),Harvested,Hero->Inventory->Count(EFrontierItem::Material));
                if(Harvested==15){HarvestReturning=true;if(!PlanHarvest(nullptr))return End(false,TEXT("harvest-return-path"));}
                return true;
            }
            if(Harvested>15 || !Hero->Gathering->IsGathering())return End(false,TEXT("harvest-input-stopped"));
            return true;
        }
        if(!HarvestNode.IsValid())
        {
            for(TActorIterator<AResourceNode> It(World);It;++It)
                if(It->Definition && It->Definition->OutputItem==EFrontierItem::Material && It->Remaining>0 && PlanHarvest(*It)){HarvestNode=*It;return true;}
            return End(false,TEXT("harvest-no-reachable-source"));
        }
        auto* Node=HarvestNode.Get();FaceHarvest(Node->GetActorLocation());
        HarvestBefore=Hero->Inventory->Count(EFrontierItem::Material);HarvestNodeBefore=Node->Remaining;
        if(Hero->TraceResource()!=Node || !Node->CanReach(Hero,300) || !CFInputQA::Press(Hero,EKeys::M) || !Hero->Gathering->IsGathering())return End(false,TEXT("harvest-input-start"));
        HarvestHeld=true;return true;
    }
    bool BeginStore(int32 Amount,int32 Next)
    {
        StoreNeedsAssign=BuildInput && StoreTrips<2;
        AssignWaitAt=-1;
        if(Pet->Construction->GetSite()!=(StoreNeedsAssign?nullptr:(Next==1?Site.Get():Stair.Get())))return End(false,TEXT("store-start-task"));
        StoreAmount=Amount;StoreNext=Next;StoreReturn=Hero->GetActorLocation();StoreReturning=false;
        // 初始工地观察点比地面高20厘米；返回点使用导航地面，不重设玩家位置。
        auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);if(!Nav)return End(false,TEXT("store-nav"));
        TArray<FVector> Best;double Length=DBL_MAX;
        for(int32 I=0;I<8;++I)
        {
            const double Angle=I*PI/4;FNavLocation Floor;
            if(!Nav->ProjectPointToNavigation(Box->GetActorLocation()+FVector(FMath::Cos(Angle)*190,FMath::Sin(Angle)*190,0),Floor,FVector(60,60,160)))continue;
            const auto* Capsule=Hero->GetCapsuleComponent();const FVector Goal=Floor.Location+FVector(0,0,Capsule->GetScaledCapsuleHalfHeight());
            if(FVector::Dist(Goal,Box->GetActorLocation())>280)continue;
            FCollisionQueryParams Q(SCENE_QUERY_STAT(UpperStoreStand),false,Hero);FHitResult Hit;
            if(World->OverlapBlockingTestByChannel(Goal,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius()-1,Capsule->GetScaledCapsuleHalfHeight()-1),Q))continue;
            if(World->LineTraceSingleByChannel(Hit,Goal+FVector(0,0,64),Box->GetActorLocation()+FVector(0,0,40),ECC_Visibility,Q) && Hit.GetActor()!=Box.Get())continue;
            auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Hero->GetActorLocation(),Floor.Location,Hero);
            if(Path && Path->IsValid() && !Path->IsPartial() && Path->PathPoints.Num()>1 && Path->GetPathLength()<Length){Length=Path->GetPathLength();Best=Path->PathPoints;}
        }
        if(Best.IsEmpty())return End(false,TEXT("store-box-path"));
        HarvestRoute=MoveTemp(Best);HarvestPoint=1;HarvestAnchor=Hero->GetActorLocation();HarvestProgressAt=World->GetTimeSeconds();
        if(!CFInputQA::Press(Hero,EKeys::LeftShift))return End(false,TEXT("store-pace"));Phase=7;return true;
    }
    bool TickStore()
    {
        auto* Expected=StoreNext==1?Site.Get():Stair.Get();
        if(!Expected || (Expected->bUnderConstruction && !StoreNeedsAssign?Pet->Construction->GetSite()!=Expected:Pet->Construction->GetSite()!=nullptr) || Box->PersistentId!=BoxId)return End(false,TEXT("store-original-task"));
        const int32 Budget=Hero->Inventory->Count(EFrontierItem::Material)+Box->StorageInventory->Count(EFrontierItem::Material)+
            Pet->Cargo->Count()+(Site->bUnderConstruction?Site->StorageInventory->Count(EFrontierItem::Material):0)+
            (Stair.IsValid()?(Stair->bUnderConstruction?Stair->StorageInventory->Count(EFrontierItem::Material):Stair->PaidMaterials):0);
        if(Budget!=(Site->bUnderConstruction?20:12))return End(false,TEXT("store-budget"));
        const auto Here=Hero->GetActorLocation();
        while(HarvestPoint<HarvestRoute.Num() && FVector::Dist2D(Here,HarvestRoute[HarvestPoint])<16)++HarvestPoint;
        if(HarvestPoint<HarvestRoute.Num())
        {
            const auto Direction=(HarvestRoute[HarvestPoint]-Here).GetSafeNormal2D();Input->Direction=Direction;FaceHarvest(Here+Direction*500+FVector(0,0,64));
            if(FVector::Dist2D(Here,HarvestAnchor)>20){HarvestAnchor=Here;HarvestProgressAt=World->GetTimeSeconds();}
            if(World->GetTimeSeconds()-HarvestProgressAt>5)return End(false,TEXT("store-path-blocked"));return true;
        }
        Input->Direction=FVector::ZeroVector;if(Hero->GetVelocity().Size2D()>3)return true;
        if(StoreReturning)
        {
            if(StoreNeedsAssign)
            {
                FaceHarvest(Expected->ConstructionAnchor()+FVector(0,0,12));
                if(Hero->GetDeliveryCrate()!=Box.Get())return End(false,TEXT("plan-assign-source"));
                if(AHomeModule::TraceConstruction(Hero)!=Expected)
                {
                    // 转向后给正常跟随队位留出让开射线的时间；不忽略伙伴遮挡，也不重复下达派工。
                    if(AssignWaitAt<0)
                    {
                        AssignWaitAt=World->GetTimeSeconds();FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(PlanAssignSight),false,Hero);
                        const auto Eye=Hero->FirstPersonCamera->GetComponentLocation();World->LineTraceSingleByChannel(Hit,Eye,Eye+Hero->FirstPersonCamera->GetForwardVector()*800,ECC_Visibility,Q);
                        UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_ASSIGN_WAIT actor=%s pet=%d"),*GetNameSafe(Hit.GetActor()),Hit.GetActor()==Pet.Get());
                    }
                    if(World->GetTimeSeconds()-AssignWaitAt>5)return End(false,TEXT("plan-assign-occluded"));return true;
                }
                if(!CFInputQA::Press(Hero,EKeys::E) || Pet->Construction->GetSite()!=Expected)return End(false,TEXT("plan-assign-input"));
                StoreNeedsAssign=false;
                UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_INPUT_ASSIGN PASS part=%s source=1 mapped=1"),StoreNext==1?TEXT("floor"):TEXT("stairs"));
                if(StoreNext==6 && !StairGuards())return false;
            }
            CFInputQA::Release(Hero,EKeys::LeftShift);Phase=StoreNext;ProgressAt=World->GetTimeSeconds();
            UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_STORE_RETURN PASS trip=%d distance=%.1f sameTask=1"),StoreTrips,StoreDistance);return true;
        }
        FaceHarvest(Box->GetActorLocation()+FVector(0,0,40));
        if(Hero->Storage->TraceCrate()!=Box.Get())return End(false,TEXT("store-aim"));
        if(!Hero->Storage->IsOpen())
        {
            if(StoreNeedsAssign && (!CFInputQA::Press(Hero,EKeys::V) || Hero->GetDeliveryCrate()!=Box.Get()))return End(false,TEXT("plan-source-input"));
            if(!CFInputQA::Press(Hero,EKeys::E) || Hero->Storage->GetCrate()!=Box.Get())return End(false,TEXT("store-open-input"));
            if(Photo(*FString::Printf(TEXT("Store%d"),StoreTrips+1),Box->GetActorLocation()+FVector(0,0,40)))return true;
        }
        if(Hero->Storage->GetCrate()!=Box.Get() || Hero->Storage->GetSelection()!=static_cast<int32>(EFrontierItem::Material))return End(false,TEXT("store-selection"));
        if(StoreTrips==2 && !FMath::IsNearlyZero(SupplyStamina-Pet->Stamina,.01f))return End(false,TEXT("store-wait-stamina"));
        const int32 Before=Hero->Inventory->Count(EFrontierItem::Material),Stock=Box->StorageInventory->Count(EFrontierItem::Material);
        auto* Task=Pet->Construction->GetSite();
        for(int32 I=0;I<StoreAmount;++I)if(!CFInputQA::Press(Hero,EKeys::Enter))return End(false,TEXT("store-deposit-input"));
        if(Hero->Inventory->Count(EFrontierItem::Material)!=Before-StoreAmount || Box->StorageInventory->Count(EFrontierItem::Material)!=Stock+StoreAmount ||
            Pet->Construction->GetSite()!=Task || !CFInputQA::Press(Hero,EKeys::E) || Hero->Storage->IsOpen())return End(false,TEXT("store-input-budget"));
        ++StoreTrips;
        if(StoreTrips==1)UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_GUARDS PASS pendingLanding=0 material=12 crate=8 anchorBelow=1"));
        if(StoreTrips==3){SupplyResumed=true;UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_SUPPLY_WAIT PASS noDrain=1 sameTask=1 actualRefill=3 seconds=%.3f"),World->GetTimeSeconds()-SupplyAt);}
        UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_STORE_INPUT PASS trip=%d amount=%d sameTask=1 open=1 closed=1"),StoreTrips,StoreAmount);
        auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);FNavLocation Floor;
        if(!Nav || !Nav->ProjectPointToNavigation(StoreReturn,Floor,FVector(60,60,160)))return End(false,TEXT("store-return-projection"));
        auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Here,Floor.Location,Hero);
        if(!Path || !Path->IsValid() || Path->IsPartial() || Path->PathPoints.Num()<2)return End(false,TEXT("store-return-path"));
        HarvestRoute=Path->PathPoints;HarvestPoint=1;HarvestAnchor=Here;HarvestProgressAt=World->GetTimeSeconds();StoreReturning=true;return true;
    }
    bool BeginPlan(bool Stairs)
    {
        PlanningStairs=Stairs;PlanningAim=Stairs?FVector(-1200,-800,-20):FVector(-1400,-400,200);FaceHarvest(PlanningAim);
        auto* PC=Cast<APlayerController>(Hero->GetController());
        if(!PC || !CFInputQA::Press(Hero,EKeys::B) || !CFInputQA::Press(Hero,EKeys::Eight))return End(false,TEXT("plan-open-input"));
        const auto Kind=Stairs?EHomeModuleKind::Stairs:EHomeModuleKind::Floor;
        for(int32 I=0;Hero->HomeBuilding->Selected!=Kind && I<static_cast<int32>(EHomeModuleKind::Count);++I)
            if(!CFInputQA::Press(Hero,EKeys::V))return End(false,TEXT("plan-cycle-input"));
        if(Hero->HomeBuilding->Selected!=Kind)return End(false,TEXT("plan-selected"));
        PC->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::RightShift,IE_Pressed,1.f,false,FPlatformTime::Cycles64()));
        Hero->HomeBuilding->UpdatePreview();
        if(Stairs)for(int32 I=0;I<4;++I)
        {
            auto* Ghost=Hero->HomeBuilding->GetPreview();if(Ghost && FMath::IsNearlyEqual(Ghost->GetActorRotation().Yaw,90.,.01))break;
            if(!CFInputQA::Press(Hero,EKeys::Nine))return End(false,TEXT("plan-rotate-input"));
        }
        Phase=8;return true;
    }
    bool TickPlan()
    {
        FaceHarvest(PlanningAim);Hero->HomeBuilding->UpdatePreview();auto* Ghost=Hero->HomeBuilding->GetPreview();
        const FVector Position=PlanningStairs?FVector(-1200,-800,-30):Upper;const auto Floor=PlanningStairs?StairBase->PersistentId:Base->PersistentId;
        auto* PC=CastChecked<APlayerController>(Hero->GetController());
        if(!PC->IsInputKeyDown(EKeys::RightShift) || !Ghost || Ghost->IsHidden() || !Ghost->GetActorLocation().Equals(Position,.01) || Ghost->FloorId.IsValid() ||
            (PlanningStairs && !FMath::IsNearlyEqual(Ghost->GetActorRotation().Yaw,90.,.01)))return End(false,TEXT("plan-preview"));
        if(Photo(PlanningStairs?TEXT("StairPlanning"):TEXT("FloorPlanning"),PlanningAim))return true;
        const auto Before=Hero->Inventory->GetStacks();const int32 Count=HomeLayout::Collect(World).Num();
        if(!CFInputQA::Action(Hero,TEXT("Attack"),EKeys::LeftMouseButton))return End(false,TEXT("plan-confirm-input"));
        PC->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::RightShift,IE_Released,0.f,false,FPlatformTime::Cycles64()));
        if(!CFInputQA::Press(Hero,EKeys::B) || Hero->Building->IsActive())return End(false,TEXT("plan-close-input"));
        auto* Planned=Find(PlanningStairs?EHomeModuleKind::Stairs:EHomeModuleKind::Floor,Position);
        if(!Planned || !Planned->bUnderConstruction || Planned->FloorId!=Floor || Planned->PaidMaterials!=0 || !Before.OrderIndependentCompareEqual(Hero->Inventory->GetStacks()) || HomeLayout::Collect(World).Num()!=Count+1)return End(false,TEXT("plan-transaction"));
        UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_INPUT_PLAN PASS part=%s preview=1 mapped=1 paid=0 retained=1"),PlanningStairs?TEXT("stairs"):TEXT("floor"));
        if(PlanningStairs){Stair=Planned;return BeginStore(9,6);}
        Site=Planned;if(!FloorGuards())return End(false,TEXT("plan-floor-guards"));return BeginStore(8,1);
    }
    bool StairGuards()
    {
        const auto Before=Hero->Inventory->GetStacks();
        if(Hero->HomeBuilding->Dismantle(StairBase.Get()) || !StairBase.IsValid() || !Before.OrderIndependentCompareEqual(Hero->Inventory->GetStacks()))return End(false,TEXT("stairs-support"));
        if(!Hero->SaveProgress(TEXT("UpperDeckStairCheck")))return End(false,TEXT("stairs-check-save"));
        auto* Legacy=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("UpperDeckStairCheck"),0));
        if(!Legacy)return End(false,TEXT("stairs-check-load"));Legacy->FormatVersion=37;
        if(!UGameplayStatics::SaveGameToSlot(Legacy,TEXT("UpperDeckStairCheck"),0) || Hero->LoadProgress(TEXT("UpperDeckStairCheck")) || Pet->Construction->GetSite()!=Stair.Get())return End(false,TEXT("stairs-legacy-guard"));
        UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_STAIR_GUARDS PASS supportRetained=1 legacy37Rejected=1 worldRetained=1"));return true;
    }
    bool Photo(const TCHAR* Name,FVector Target)
    {
        if(!Preview || Photos.Contains(Name))return false;Photos.Add(Name);
        const auto Rotation=(Target-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
        Hero->GetController()->SetControlRotation(Rotation);Hero->FirstPersonCamera->SetWorldRotation(Rotation);
        FScreenshotRequest::RequestScreenshot(ImageRoot/(FString(Name)+TEXT(".png")),false,false);NextPhoto=FPlatformTime::Seconds()+.2;return true;
    }
    bool End(bool Pass,const TCHAR* Reason)
    {
        if(Input.IsValid())Input->Direction=FVector::ZeroVector;
        if(BuildInput)if(auto* PC=Cast<APlayerController>(Hero->GetController()))PC->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::RightShift,IE_Released,0.f,false,FPlatformTime::Cycles64()));
        if(WalkHarvest){CFInputQA::Release(Hero,EKeys::M);CFInputQA::Release(Hero,EKeys::LeftShift);}
        const FString Feedback=Hero->Feedback;
        if(!Pass)Hero->SaveProgress(TEXT("UpperDeckBlocked"));
        UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK %s reason=%s phase=%d point=%d heroTravel=%.1f petTravel=%.1f hero=%s pet=%s feedback=%s"),
            Pass?TEXT("PASS"):TEXT("FAIL"),Reason,Phase,Point,HeroTravel,Travel,*Hero->GetActorLocation().ToString(),Pet.IsValid()?*Pet->GetActorLocation().ToString():TEXT("无"),*Feedback);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    bool Queue()
    {
        Observe(FVector(-1200,-750,60.15));Hero->HomeBuilding->Selected=EHomeModuleKind::Floor;
        Site=Hero->HomeBuilding->QueueAt(FTransform(Upper),Base->PersistentId);
        return FloorGuards();
    }
    bool FloorGuards()
    {
        if(!Site.IsValid() || Site->ConstructionAnchor()!=FVector(-1200,-400,-30))return false;
        TGuardValue<EHomeModuleKind> Selected(Hero->HomeBuilding->Selected,EHomeModuleKind::Stairs);
        const auto Before=Hero->Inventory->GetStacks();
        if(Hero->HomeBuilding->BuildAt(FTransform(FRotator(0,90,0),FVector(-1200,-800,-30)),StairBase->PersistentId) ||
            !Before.OrderIndependentCompareEqual(Hero->Inventory->GetStacks()) || HomeLayout::FindFloor(Upper,HomeLayout::Collect(World),280).IsValid())return false;
        if((!WalkSupply && !Hero->Inventory->TransferTo(Box->StorageInventory,{{EFrontierItem::Material,8}})) || (!BuildInput && !Pet->Construction->Assign(Site.Get(),Box.Get(),Hero)))return false;
        if(!WalkSupply)UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_GUARDS PASS pendingLanding=0 material=12 crate=8 anchorBelow=1"));return true;
    }
    bool Tick()
    {
        if(!Pet.IsValid() || !Box.IsValid() || FPlatformTime::Seconds()>Deadline)return End(false,TEXT("lifetime"));
        const auto P=Pet->GetActorLocation(),H=Hero->GetActorLocation();const double Delta=World->GetTimeSeconds()-PreviousTime;
        PreviousTime=World->GetTimeSeconds();Travel+=FVector::Dist(P,PreviousPet);
        const bool Continuous=MovementContinuous(Pet.Get(),PreviousPet,P,PetVelocity,Delta);PreviousPet=P;PetVelocity=Pet->GetVelocity();
        if(!Continuous || !Pet->GetActorEnableCollision() || !Pet->IsActorTickEnabled() || Pet->GetCharacterMovement()->MovementMode==MOVE_None || Pet->Health<=0 || Hero->Health<=0)
        {
            UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_MOTION_DIAG delta=%.6f velocity=%s speed=%.1f step=%.1f collision=%d tick=%d mode=%d"),Delta,*PetVelocity.ToString(),Pet->GetCharacterMovement()->GetMaxSpeed(),Pet->GetCharacterMovement()->MaxStepHeight,Pet->GetActorEnableCollision(),Pet->IsActorTickEnabled(),static_cast<int32>(Pet->GetCharacterMovement()->MovementMode));
            return End(false,TEXT("continuity"));
        }
        if(Phase==3 || Phase==7 || Phase==8 || (WalkHarvest && Phase==0))
        {
            if(WalkHarvest && Phase==0)HarvestDistance+=FVector::Dist(H,PreviousHero);
            if(Phase==7)StoreDistance+=FVector::Dist(H,PreviousHero);
            HeroTravel+=FVector::Dist(H,PreviousHero);const bool HeroContinuous=MovementContinuous(Hero,PreviousHero,H,HeroVelocity,Delta);PreviousHero=H;HeroVelocity=Hero->GetVelocity();
            if(!HeroContinuous)return End(false,TEXT("hero-continuity"));
        }
        // 截图等待只延迟场景动作，仍采样双方的每帧真实位移。
        if(FPlatformTime::Seconds()<NextPhoto)return true;
        if(Phase==8)return TickPlan();
        if(WalkSupply && Phase==7)
        {
            if(!Mid && Site.IsValid() && Pet->Construction->GetSite()==Site.Get() && Pet->Cargo->Count()==3)
            {if(!Hero->SaveProgress(TEXT("UpperDeckMid")))return End(false,TEXT("mid-save"));Mid=true;UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_MID PASS version=%d carried=3"),FrontierCurrentSaveVersion);}
            if(!StairMid && Stair.IsValid() && Pet->Construction->GetSite()==Stair.Get() && Pet->Cargo->Count()==3)
            {if(!Hero->SaveProgress(TEXT("UpperDeckStairMid")))return End(false,TEXT("stairs-mid-save"));StairMid=true;UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_STAIR_MID PASS version=%d carried=3 material=%d"),FrontierCurrentSaveVersion,Hero->Inventory->Count(EFrontierItem::Material));}
            return TickStore();
        }
        if(Phase==0)
        {
            if(WalkHarvest)return TickWalkHarvest();
            if(Harvested==15){if(Hero->Inventory->Count(EFrontierItem::Material)!=20 || !Queue())return End(false,TEXT("queue"));Phase=1;return true;}
            for(TActorIterator<AResourceNode> It(World);It;++It)if(It->Definition && It->Definition->OutputItem==EFrontierItem::Material && It->Remaining>0 && It->HarvestCooldown<=0)
            {Observe(It->GetActorLocation()+FVector(0,-180,60));if(It->TryHarvest(Hero,Hero)){++Harvested;UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_HARVEST count=%d material=%d"),Harvested,Hero->Inventory->Count(EFrontierItem::Material));return true;}}
            return true;
        }
        if(Phase==1)
        {
            if(!Site.IsValid())return End(false,TEXT("site"));
            if(Preview && Site->bUnderConstruction && !Photos.Contains(TEXT("Pending")))
            {
                Photo(TEXT("Pending"),Site->ConstructionAnchor()+FVector(0,0,12));
                if(AHomeModule::TraceConstruction(Hero)!=Site.Get())return End(false,TEXT("preview-aim"));
                UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_PREVIEW_AIM PASS target=1"));return true;
            }
            const int32 Total=Hero->Inventory->Count(EFrontierItem::Material)+Box->StorageInventory->Count(EFrontierItem::Material)+Site->StorageInventory->Count(EFrontierItem::Material)+Pet->Cargo->Count();
            if(Total!=(Site->bUnderConstruction?20:12))return End(false,TEXT("conservation"));
            if(!Mid && Pet->Cargo->Count()==3)
            {if(!Hero->SaveProgress(TEXT("UpperDeckMid")))return End(false,TEXT("mid-save"));Mid=true;UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_MID PASS version=%d carried=3"),FrontierCurrentSaveVersion);}
            if(Site->bUnderConstruction)return true;
            if(!Mid || Site->PaidMaterials!=8 || Pet->Cargo->Count()!=0 || Pet->Construction->GetSite())return End(false,TEXT("completion"));
            Phase=4;Photo(TEXT("FloorDone"),Upper);return true;
        }
        if(Phase==4)
        {
            // 完工后工人仍可能经过楼梯安装位；玩家在外等它正常归队，不能绕过角色占位。
            if(!StairWaiting){Observe(FVector(-1700,-800,40.15));if(!BuildInput)Hero->HomeBuilding->Selected=EHomeModuleKind::Stairs;ProgressAt=World->GetTimeSeconds();StairWaiting=true;}
            FString Reason;
            bool Clear=false;{TGuardValue<EHomeModuleKind> Selected(Hero->HomeBuilding->Selected,EHomeModuleKind::Stairs);Clear=Hero->HomeBuilding->ValidatePlacement(FTransform(FRotator(0,90,0),FVector(-1200,-800,-30)),StairBase->PersistentId,Reason,StairWork);}
            if(!Clear)
            {
                Hero->Feedback=Reason;
                if(!StairBlocked){StairBlocked=true;UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_WAIT reason=%s pet=%s"),*Reason,*P.ToString());}
                if(World->GetTimeSeconds()-ProgressAt>15)return End(false,TEXT("stairs-clearance"));return true;
            }
            if(StairWork)
            {
                if(BuildInput)return BeginPlan(true);
                Stair=Hero->HomeBuilding->QueueAt(FTransform(FRotator(0,90,0),FVector(-1200,-800,-30)),StairBase->PersistentId);
                if(!Stair.IsValid() || (!WalkSupply && !Hero->Inventory->TransferTo(Box->StorageInventory,{{EFrontierItem::Material,SupplyPause?9:12}})) || !Pet->Construction->Assign(Stair.Get(),Box.Get(),Hero))return End(false,TEXT("stairs-queue"));
                if(!StairGuards())return false;
                Hero->Feedback=TEXT("楼梯工地已设置，伙伴正从原箱搬料");
                if(WalkSupply)return BeginStore(9,6);
                ProgressAt=World->GetTimeSeconds();Phase=6;return true;
            }
            if(!Hero->HomeBuilding->BuildAt(FTransform(FRotator(0,90,0),FVector(-1200,-800,-30)),StairBase->PersistentId))return End(false,TEXT("stairs"));
            UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_BUILT PASS modules=25 floor=8 stairs=12 material=0"));
            Phase=5;Photo(TEXT("Stairs"),FVector(-1200,-650,180));return true;
        }
        if(Phase==6)
        {
            if(!Stair.IsValid())return End(false,TEXT("stairs-site"));
            if(!StairPendingNav && Stair->bUnderConstruction && World->GetTimeSeconds()-ProgressAt>2)
            {
                // 实查未完工时的碰撞和导航，不能仅凭临时几何数量宣称不可通行。
                FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(StairPendingTest),false,Hero);
                if(!World->LineTraceSingleByChannel(Hit,FVector(-1200,-650,400),FVector(-1200,-650,-100),ECC_Visibility,Query) || Hit.GetActor()!=StairBase.Get() || Hit.ImpactPoint.Z>-20)return End(false,TEXT("stairs-premature-steps"));
                const auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Pet->GetActorLocation(),FVector(-1200,-400,290),Pet.Get());
                if(Path && Path->IsValid() && !Path->IsPartial())return End(false,TEXT("stairs-premature-navigation"));
                StairPendingNav=true;UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_STAIR_PENDING PASS lowerFloorHit=1 upperPath=0 collision=1"));
            }
            if(Preview && Stair->bUnderConstruction && !Photos.Contains(TEXT("StairsPending")))
            {
                // 从搬料路线侧后方观察，避免截图视线穿过仍在真实移动的工人。
                Observe(FVector(-1700,-1080,40.15));
                Photo(TEXT("StairsPending"),Stair->ConstructionAnchor()+FVector(0,0,12));
                if(AHomeModule::TraceConstruction(Hero)!=Stair.Get())return End(false,TEXT("stairs-preview-aim"));
                UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_STAIR_PREVIEW_AIM PASS target=1"));return true;
            }
            const int32 Total=Hero->Inventory->Count(EFrontierItem::Material)+Box->StorageInventory->Count(EFrontierItem::Material)+Stair->StorageInventory->Count(EFrontierItem::Material)+Pet->Cargo->Count();
            if(Total!=(Stair->bUnderConstruction?12:0))return End(false,TEXT("stairs-conservation"));
            if(SupplyPause && !SupplyResumed && Stair->StorageInventory->Count(EFrontierItem::Material)==9 && Pet->Cargo->Count()==0)
            {
                if(SupplyAt<0){SupplyAt=World->GetTimeSeconds();SupplyStamina=Pet->Stamina;}
                if(World->GetTimeSeconds()-SupplyAt<5)return true;
                const float Loss=SupplyStamina-Pet->Stamina;
                if(!FMath::IsNearlyZero(Loss,.01f) || Pet->Construction->GetSite()!=Stair.Get() || Stair->ConstructionWork!=0 || Box->StorageInventory->Count(EFrontierItem::Material)!=0 || Hero->Inventory->Count(EFrontierItem::Material)!=3)return End(false,TEXT("stairs-supply-wait"));
                if(WalkSupply)return BeginStore(3,6);
                if(!Hero->Inventory->TransferTo(Box->StorageInventory,{{EFrontierItem::Material,3}}))return End(false,TEXT("stairs-supply-transfer"));
                SupplyResumed=true;UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_SUPPLY_WAIT PASS noDrain=1 sameTask=1 actualRefill=3 seconds=%.3f"),World->GetTimeSeconds()-SupplyAt);
            }
            if(!StairMid && Pet->Cargo->Count()==3)
            {
                if(!Hero->SaveProgress(TEXT("UpperDeckStairMid")))return End(false,TEXT("stairs-mid-save"));StairMid=true;
                UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_STAIR_MID PASS version=%d carried=3 material=%d"),FrontierCurrentSaveVersion,Hero->Inventory->Count(EFrontierItem::Material));
            }
            if(Stair->bUnderConstruction)return true;
            if(!StairMid || !StairPendingNav || (SupplyPause && !SupplyResumed) || Stair->PaidMaterials!=12 || Pet->Cargo->Count()!=0 || Pet->Construction->GetSite())return End(false,TEXT("stairs-completion"));
            UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_BUILT PASS modules=25 floor=8 stairs=12 material=0"));
            UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_STAIR_WORK PASS paid=12 carried=0 taskCleared=1"));
            Phase=5;Photo(TEXT("Stairs"),FVector(-1200,-650,180));return true;
        }
        if(Phase==5)
        {
            Observe(FVector(-1500,-1080,40.15));
            // 后续会合断言要求伙伴在西侧；明确玩家朝东，避免前一段行走朝向改变合法跟随队位。
            FaceHarvest(Hero->GetActorLocation()+FVector(500,0,64));
            ProgressAt=World->GetTimeSeconds();Phase=2;return true;
        }
        if(Phase==2)
        {
            if(World->GetTimeSeconds()-ProgressAt>20)return End(false,TEXT("join"));
            if(P.X>-1440 || Pet->GetDistanceTo(Hero)>250)return true;
            if(Input.IsValid())Input->DestroyComponent();
            Input=NewObject<UHomeMovementProbe>(Hero);Input->RegisterComponent();PreviousHero=H;HeroVelocity=Hero->GetVelocity();Anchor=H;ProgressAt=World->GetTimeSeconds();Phase=3;
            UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_JOIN PASS outside=1"));return true;
        }
        if(Phase==3)
        {
            HeroUp|=H.Z>350 && FMath::Abs(H.X+1200)<150 && H.Y>-550 && H.Y<-200;
            PetUp|=P.Z>350 && FMath::Abs(P.X+1200)<150 && P.Y>-550 && P.Y<-200;
            if(FVector::Dist2D(H,Anchor)>10){Anchor=H;ProgressAt=World->GetTimeSeconds();}
            if(Point<Route.Num() && FVector::Dist2D(H,Route[Point])<15)
            {
                Input->Direction=FVector::ZeroVector;
                if(Pet->GetDistanceTo(Hero)<250 && (Point!=2 || (H.Z>350 && P.Z>350)))
                {
                    if(Point==2)
                    {
                        const auto Before=Hero->Inventory->GetStacks();
                        if(Hero->HomeBuilding->Dismantle(Site.Get()) || !Site.IsValid() || !Before.OrderIndependentCompareEqual(Hero->Inventory->GetStacks()) || !Hero->SaveProgress(TEXT("UpperDeckTop")))return End(false,TEXT("top-support"));
                        UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_TOP PASS hero=1 pet=1 supportRetained=1 version=%d"),FrontierCurrentSaveVersion);
                    }
                    ++Point;ProgressAt=World->GetTimeSeconds();
                    if(Point==3 && Photo(TEXT("Occupied"),P+FVector(0,0,35)))return true;
                }
            }
            if(Point<Route.Num())
            {
                if(World->GetTimeSeconds()-ProgressAt>15)return End(false,TEXT("walk-blocked"));
                Input->Direction=FVector::Dist2D(H,Route[Point])<15?FVector::ZeroVector:(Route[Point]-H).GetSafeNormal2D();
                if(!Input->Direction.IsNearlyZero())
                {
                    const auto Rotation=(H+Input->Direction*500+FVector(0,0,64)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
                    Hero->GetController()->SetControlRotation(Rotation);Hero->FirstPersonCamera->SetWorldRotation(Rotation);
                }
                return true;
            }
            Input->Direction=FVector::ZeroVector;
            if(!HeroUp || !PetUp || HeroTravel<1800 || Travel<1800 || H.Z>100 || P.Z>100 || !Hero->SaveProgress(TEXT("UpperDeckDone")) || !Hero->LoadProgress(TEXT("UpperDeckDone")) || !Bind())return End(false,TEXT("life-save"));
            const auto Records=HomeLayout::Collect(World);
            if(Records.Num()!=25 || Hero->Inventory->Count(EFrontierItem::Material)!=0 || Box->StorageInventory->Count(EFrontierItem::Material)!=0 || Hero->Inventory->Count(EFrontierItem::Seeds)!=1 || Hero->Inventory->Count(EFrontierItem::Food)!=0)return End(false,TEXT("done-state"));
            for(const auto& Old:Original){const auto* Now=Records.FindByPredicate([&](const FHomeModuleRecord& R){return R.Id==Old.Id;});if(!Now || Now->Definition!=Old.Definition || Now->FloorId!=Old.FloorId || !Now->Transform.Equals(Old.Transform,.01) || Now->PaidMaterials!=Old.PaidMaterials)return End(false,TEXT("retained"));}
            UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_LIFE PASS version=%d retained=23 modules=25 heroUp=1 petUp=1 returned=1 material=0 seeds=1 food=0 bond=9"),FrontierCurrentSaveVersion);return End(true,TEXT("complete"));
        }
        return true;
    }
};
}
void RunUpperDeckConstructionScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto State=MakeShared<FUpperDeckBuildTest>();State->World=World;State->Hero=Hero;State->Deadline=FPlatformTime::Seconds()+300;
    State->StairWork=FParse::Param(FCommandLine::Get(),TEXT("CFUpperDeckStairWork"));
    State->SupplyPause=FParse::Param(FCommandLine::Get(),TEXT("CFUpperDeckSupplyPause"));
    State->WalkHarvest=FParse::Param(FCommandLine::Get(),TEXT("CFUpperDeckWalkHarvest"));
    State->WalkSupply=FParse::Param(FCommandLine::Get(),TEXT("CFUpperDeckWalkSupply"));
    State->BuildInput=FParse::Param(FCommandLine::Get(),TEXT("CFUpperDeckBuildInput"));
    if(State->BuildInput && !State->WalkSupply){State->End(false,TEXT("build-input-mode"));return;}
    if(State->WalkSupply && (!State->WalkHarvest || !State->StairWork || !State->SupplyPause || Read)){State->End(false,TEXT("walk-supply-mode"));return;}
    if(State->WalkHarvest && Read){State->End(false,TEXT("walk-harvest-read-mode"));return;}
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("UpperDeckInput"),0));
    const bool StairRead=Read && State->StairWork && Source && Source->HomeModules.Num()==25;
    if(!Source || Source->FormatVersion<37 || Source->FormatVersion>FrontierCurrentSaveVersion || Source->HomeModules.Num()!=(StairRead?25:24) || !Hero->LoadProgress(TEXT("UpperDeckInput")) || !State->Bind()) {State->End(false,TEXT("source"));return;}
    State->Original=HomeLayout::Collect(World);
    State->Original.RemoveAll([State](const FHomeModuleRecord& R){return R.Transform.GetLocation().Equals(State->Upper,.01);});
    if(StairRead)State->Original.RemoveAll([](const FHomeModuleRecord& R){return R.Definition.Get() && R.Definition.Get()->Kind==EHomeModuleKind::Stairs && R.Transform.GetLocation().Equals(FVector(-1200,-800,-30),.01);});
    if(State->Original.Num()!=23 || Source->Facilities.Num()!=2 || Hero->Inventory->Count(EFrontierItem::Material)!=(StairRead && State->SupplyPause?3:(Read && !StairRead)?12:0)){State->End(false,TEXT("original"));return;}
    State->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFUpperDeckPreview"));
    if(State->Preview){State->ImageRoot=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("UpperDeckPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*State->ImageRoot,true);UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_PREVIEW_ROOT %s"),*State->ImageRoot);}
    if(StairRead)
    {
        State->Site=State->Find(EHomeModuleKind::Floor,State->Upper);State->Stair=State->Find(EHomeModuleKind::Stairs,FVector(-1200,-800,-30));
        if(!State->Site.IsValid() || State->Site->bUnderConstruction || !State->Stair.IsValid() || !State->Stair->bUnderConstruction || State->Pet->Construction->GetSite()!=State->Stair.Get() || State->Pet->Cargo->Count()!=3){State->End(false,TEXT("stairs-restore"));return;}
        State->Mid=true;State->StairMid=true;State->Phase=6;State->ProgressAt=World->GetTimeSeconds();
        State->Observe(FVector(-1700,-800,40.15));
        UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_STAIR_RESTORE PASS version=%d carried=3 material=%d retained=23"),Source->FormatVersion,Hero->Inventory->Count(EFrontierItem::Material));
        bool PositionPreserved=false;
        for(const auto& Record:Source->Creatures)if(Record.Id==State->PetId)
        {
            UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_STAIR_RESTORE_POSITION saved=%s loaded=%s worldTime=%.6f frameDelta=%.6f"),*Record.Transform.GetLocation().ToString(),*State->PreviousPet.ToString(),World->GetTimeSeconds(),World->GetDeltaSeconds());
            PositionPreserved=Record.Transform.GetLocation().Equals(State->PreviousPet,.01);
        }
        if(!PositionPreserved){State->End(false,TEXT("stairs-restore-position"));return;}
        UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_STAIR_POSITION PASS preserved=1"));
    }
    else if(Read)
    {
        State->Site=State->Find(EHomeModuleKind::Floor,State->Upper);
        if(!State->Site.IsValid() || !State->Site->bUnderConstruction || State->Pet->Construction->GetSite()!=State->Site.Get() || State->Pet->Cargo->Count()!=3){State->End(false,TEXT("restore"));return;}
        State->Mid=true;State->Phase=1;UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_RESTORE PASS version=%d carried=3 material=12 retained=23"),Source->FormatVersion);
        if(State->Preview)State->Observe(FVector(-1200,-750,60.15));
    }
    else
    {
        State->Observe(FVector(-1500,-800,40.15));auto* Roof=State->Find(EHomeModuleKind::Roof,State->Upper);
        if(!Roof || !Hero->HomeBuilding->Dismantle(Roof) || Hero->Inventory->Count(EFrontierItem::Material)!=5){State->End(false,TEXT("roof-refund"));return;}
        UE_LOG(LogTemp,Display,TEXT("CF_UPPER_DECK_REFUND PASS roof=5 retained=23"));
        if(State->WalkHarvest)
        {
            State->Input=NewObject<UHomeMovementProbe>(Hero);State->Input->RegisterComponent();
            State->PreviousHero=Hero->GetActorLocation();State->HeroVelocity=Hero->GetVelocity();
            State->Deadline=FPlatformTime::Seconds()+420;
            if(!CFInputQA::Press(Hero,EKeys::LeftShift)){State->End(false,TEXT("walk-harvest-pace"));return;}
        }
    }
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float){return State->Tick();}));
}
