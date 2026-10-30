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
                UE_LOG(LogTemp,Display,TEXT("CF_PLATFORM_MID PASS version=37 part=%d carried=3 paid=0"),Part);
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
            UE_LOG(LogTemp,Display,TEXT("CF_PLATFORM_LIFE PASS version=37 modules=24 retained=19 floor=1 roof=1 hero=1 pet=1 home=1 petHome=1 material=0 seeds=1 food=0 bond=9"));return End(true,TEXT("complete"));
        }
        return true;
    }
};
}
void RunPlatformConstructionScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto State=MakeShared<FPlatformBuildTest>();State->World=World;State->Hero=Hero;State->Deadline=FPlatformTime::Seconds()+300;
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("PlatformInput"),0));
    if(!Source || Source->FormatVersion!=(Read?37:36) || !Hero->LoadProgress(TEXT("PlatformInput")) || !State->Bind()){State->End(false,TEXT("source"));return;}
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
        State->Phase=1;State->Mid=true;UE_LOG(LogTemp,Display,TEXT("CF_PLATFORM_RESTORE PASS version=37 part=%d carried=3 retained=19"),State->Part);
        if(State->Preview)State->ObserveSite();
    }
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float){return State->Tick();}));
}
