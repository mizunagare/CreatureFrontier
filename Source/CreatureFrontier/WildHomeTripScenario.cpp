#include "ConstructionScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "HomeScenario.h"
#include "HomeLayout.h"
#include "FrontierSaveGame.h"
#include "InventoryComponent.h"
#include "ResourceNode.h"
#include "ResourceDefinition.h"
#include "PlayerGatheringComponent.h"
#include "WildEncounter.h"
#include "WorkStation.h"
#include "FarmCropComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/Ticker.h"
#include "InputRoutingQA.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

namespace
{
// 与原家园夹具分开：不关闭遭遇槽或任何生物的更新、移动、碰撞和感知。
struct FWildHomeTrip
{
    UWorld* World;ACFPlayerCharacter* Hero;TWeakObjectPtr<ACreatureCharacter> Pet;TWeakObjectPtr<AResourceNode> Node;
    TWeakObjectPtr<UHomeMovementProbe> Input;TArray<FVector> Route;TArray<FHomeModuleRecord> House;TArray<FFacilitySaveRecord> Facilities;
    TMap<FGuid,FVector> WildPositions;FGuid PetId;FString Nickname,ImageRoot;TSet<FString> Photos;
    bool Read=false,Preview=false,Held=false,Crafted=false;int32 Phase=0,Point=0,NodeBefore=0;
    double Deadline=0,At=0,ShotUntil=0,PreviousTime=0,ProgressAt=0,HeroTravel=0,PetTravel=0,WildTravel=0;
    FVector PreviousHero,PreviousPet,HeroVelocity,PetVelocity,Anchor;
    bool Bind()
    {
        Pet.Reset();Node.Reset();int32 Owned=0;
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController()){Pet=*It;++Owned;}
        for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,4))Node=*It;
        if(Owned!=1 || !Pet.IsValid() || !Node.IsValid() || (PetId.IsValid() && PetId!=Pet->PersistentId))return false;
        PetId=Pet->PersistentId;PreviousHero=Hero->GetActorLocation();PreviousPet=Pet->GetActorLocation();HeroVelocity=Hero->GetVelocity();PetVelocity=Pet->GetVelocity();PreviousTime=World->GetTimeSeconds();return true;
    }
    bool End(bool Pass,const TCHAR* Reason)
    {
        if(Input.IsValid())Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::LeftShift);if(Held)CFInputQA::Release(Hero,EKeys::M);
        const auto Feedback=Hero->Feedback;if(!Pass)Hero->SaveProgress(TEXT("WildHomeBlocked"));
        UE_LOG(LogTemp,Display,TEXT("CF_WILD_HOME %s reason=%s phase=%d heroTravel=%.1f petTravel=%.1f wildTravel=%.1f health=%.1f petHealth=%.1f feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Reason,Phase,HeroTravel,PetTravel,WildTravel,Hero->Health,Pet.IsValid()?Pet->Health:-1.f,*Feedback);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    void Face(FVector Target)
    {const auto R=(Target-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->SetActorRotation(FRotator(0,R.Yaw,0));Hero->FirstPersonCamera->SetRelativeRotation(FRotator::ZeroRotator);Hero->GetController()->SetControlRotation(R);Hero->FirstPersonCamera->SetWorldRotation(R);}
    bool Photo(const TCHAR* Name,FVector Target)
    {
        if(!Preview || Photos.Contains(Name))return false;Photos.Add(Name);Face(Target);
        FScreenshotRequest::RequestScreenshot(ImageRoot/(FString(Name)+TEXT(".png")),false,false);ShotUntil=FPlatformTime::Seconds()+.2;return true;
    }
    bool Plan(bool Return)
    {
        auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);if(!Nav)return false;TArray<FVector> Best;double Length=DBL_MAX;
        for(int32 I=0;I<(Return?1:8);++I)
        {
            const double A=I*PI/4;const auto Goal=Return?FVector(-1490,-1080,40):Node->GetActorLocation()+FVector(FMath::Cos(A)*220,FMath::Sin(A)*220,0);
            FNavLocation Floor;if(!Nav->ProjectPointToNavigation(Goal,Floor,FVector(60,60,160)))continue;
            const auto* Shape=Hero->GetCapsuleComponent();const auto Center=Floor.Location+FVector(0,0,Shape->GetScaledCapsuleHalfHeight());
            FCollisionQueryParams Q(SCENE_QUERY_STAT(WildHomeStand),false,Hero);FHitResult Hit;
            if(World->OverlapBlockingTestByChannel(Center,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Shape->GetScaledCapsuleRadius()-1,Shape->GetScaledCapsuleHalfHeight()-1),Q))continue;
            if(!Return && World->LineTraceSingleByChannel(Hit,Center,Node->GetActorLocation(),ECC_Visibility,Q) && Hit.GetActor()!=Node.Get())continue;
            auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Hero->GetActorLocation(),Floor.Location,Hero);
            if(Path && Path->IsValid() && !Path->IsPartial() && Path->PathPoints.Num()>1 && Path->GetPathLength()<Length){Best=Path->PathPoints;Length=Path->GetPathLength();}
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
    bool SameHome() const
    {
        const auto Current=HomeLayout::Collect(World);if(Current.Num()!=House.Num())return false;
        for(const auto& Old:House){const auto* Now=Current.FindByPredicate([&](const FHomeModuleRecord& R){return R.Id==Old.Id;});
            if(!Now || Now->Definition!=Old.Definition || Now->FloorId!=Old.FloorId || Now->DoorState!=Old.DoorState || !Now->Transform.Equals(Old.Transform,.01) || Now->PaidMaterials!=Old.PaidMaterials || !Now->StoredItems.OrderIndependentCompareEqual(Old.StoredItems) || !Now->PaidExtraItems.OrderIndependentCompareEqual(Old.PaidExtraItems))return false;}
        int32 Count=0;for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt)
        {++Count;const auto* Old=Facilities.FindByPredicate([&](const FFacilitySaveRecord& R){return R.Id==It->PersistentId;});
            if(!Old || !It->GetActorTransform().Equals(Old->Transform,.01) || It->RequiredSpecialty!=Old->Specialty || It->StoredFood!=Old->StoredFood || It->Crop->Seeds!=Old->Seeds || It->Crop->bPlanted!=Old->bPlanted || !FMath::IsNearlyEqual(It->Crop->GrowthWork,Old->GrowthWork,.01f))return false;}
        return Count==Facilities.Num() && Pet->Nickname==Nickname;
    }
    bool Motion(const ACharacter* Pawn,FVector Before,FVector Position,FVector Velocity,double Delta) const
    {
        const auto* M=Pawn->GetCharacterMovement();return M && Delta>=0 && FMath::IsFinite(Delta) && !Position.ContainsNaN() &&
            FVector::Dist2D(Before,Position)<=FMath::Max(static_cast<double>(M->GetMaxSpeed()),Velocity.Size2D())*Delta+3 &&
            FMath::Abs(Position.Z-Before.Z)<=FMath::Max(FMath::Abs(Pawn->GetVelocity().Z),FMath::Abs(Velocity.Z))*Delta+.5*FMath::Abs(World->GetGravityZ())*Delta*Delta+M->MaxStepHeight+3;
    }
    bool Tick()
    {
        if(!Pet.IsValid() || !Node.IsValid() || FPlatformTime::Seconds()>Deadline)return End(false,TEXT("lifetime"));
        const auto H=Hero->GetActorLocation(),P=Pet->GetActorLocation();const double Delta=World->GetTimeSeconds()-PreviousTime;
        if(!Motion(Hero,PreviousHero,H,HeroVelocity,Delta) || !Motion(Pet.Get(),PreviousPet,P,PetVelocity,Delta) || !Pet->IsActorTickEnabled() || !Pet->GetActorEnableCollision() || !Hero->GetActorEnableCollision() || Hero->Health<=0 || Pet->Health<=0)return End(false,TEXT("motion-health"));
        HeroTravel+=FVector::Dist(H,PreviousHero);PetTravel+=FVector::Dist(P,PreviousPet);PreviousHero=H;PreviousPet=P;HeroVelocity=Hero->GetVelocity();PetVelocity=Pet->GetVelocity();PreviousTime=World->GetTimeSeconds();
        int32 Encounters=0;
        for(TActorIterator<AWildEncounter> It(World);It;++It){++Encounters;if(!It->IsActorTickEnabled())return End(false,TEXT("encounter-disabled"));}
        if(Encounters!=3)return End(false,TEXT("encounter-count"));
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Disposition!=ECreatureDisposition::Tamed && It->Health>0)
        {
            if(!It->IsActorTickEnabled() || !It->GetActorEnableCollision() || It->GetCharacterMovement()->MovementMode==MOVE_None)return End(false,TEXT("wild-disabled"));
            if(const auto* Old=WildPositions.Find(It->PersistentId))WildTravel+=FVector::Dist(*Old,It->GetActorLocation());WildPositions.Add(It->PersistentId,It->GetActorLocation());
        }
        if(!SameHome() || Hero->Inventory->Count(EFrontierItem::Seeds)!=1 || Hero->Inventory->Count(EFrontierItem::Food)!=(Crafted?2:1))return End(false,TEXT("budget-home"));
        if(FPlatformTime::Seconds()<ShotUntil)return true;
        if(Phase==-1 || Phase==-2)
        {
            if(World->GetTimeSeconds()-At<.1 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;
            const bool Returning=Phase==-2;if(!Plan(Returning))return End(false,TEXT("outbound-path"));Phase=Returning?2:0;return true;
        }
        if(Phase==0 || Phase==2)
        {
            const int32 Result=Move();if(Result<0)return End(false,TEXT("walk-blocked"));if(!Result)return true;
            if(Phase==2){Phase=3;At=World->GetTimeSeconds();return true;}
            Face(Node->GetActorLocation());if(Photo(TEXT("Meadow"),Node->GetActorLocation()))return true;
            NodeBefore=Node->Remaining;
            if(Hero->TraceResource()!=Node.Get() || NodeBefore<2 || !CFInputQA::Press(Hero,EKeys::M) || !Hero->Gathering->IsGathering())return End(false,TEXT("gather-input"));Held=true;Phase=1;return true;
        }
        if(Phase==1)
        {
            const int32 Materials=Hero->Inventory->Count(EFrontierItem::Material);
            if(Materials<0 || Materials>2 || Node->Remaining!=NodeBefore-Materials)return End(false,TEXT("node-budget"));
            if(Materials<2){if(!Hero->Gathering->IsGathering())return End(false,TEXT("gather-interrupted"));return true;}
            if(!CFInputQA::Release(Hero,EKeys::M) || Hero->Gathering->IsGathering())return End(false,TEXT("gather-release"));Held=false;
            if(!Hero->SaveProgress(TEXT("WildHomeMid")))return End(false,TEXT("mid-save"));
            UE_LOG(LogTemp,Display,TEXT("CF_WILD_HOME_GATHER PASS input=1 material=2 nodePaid=2 ecology=3 wildTravel=%.1f"),WildTravel);
            Phase=2;if(!Plan(true))return End(false,TEXT("return-path"));return true;
        }
        if(Pet->GetDistanceTo(Hero)>250 || Pet->Activity!=ECreatureActivity::Follow)
        {if(World->GetTimeSeconds()-At>20)return End(false,TEXT("companion-return"));return true;}
        if(!Crafted)
        {
            if(Hero->Inventory->Count(EFrontierItem::Material)!=2 || !CFInputQA::Press(Hero,EKeys::Three) || Hero->Inventory->Count(EFrontierItem::Material)!=0 || Hero->Inventory->Count(EFrontierItem::Food)!=2)return End(false,TEXT("home-craft"));Crafted=true;
            UE_LOG(LogTemp,Display,TEXT("CF_WILD_HOME_CRAFT PASS material=0 food=2 seeds=1 input=1"));if(Photo(TEXT("Home"),P+FVector(0,0,20)))return true;
        }
        const auto HP=Hero->Health,PetHP=Pet->Health;const auto SavedH=H,SavedP=P;
        if(WildTravel<50 || HeroTravel<(Read?1000:3000) || !Hero->SaveProgress(TEXT("WildHomeDone")) || !Hero->LoadProgress(TEXT("WildHomeDone")) || !Bind() || !Hero->GetActorLocation().Equals(SavedH,.01) || !Pet->GetActorLocation().Equals(SavedP,.01) || Hero->Health!=HP || Pet->Health!=PetHP || !SameHome() || Hero->Inventory->Count(EFrontierItem::Food)!=2 || Hero->Inventory->Count(EFrontierItem::Seeds)!=1 || Hero->Inventory->Count(EFrontierItem::Material)!=0)return End(false,TEXT("done-save"));
        UE_LOG(LogTemp,Display,TEXT("CF_WILD_HOME_LIFE PASS version=38 ecology=3 wildActive=1 material=0 food=2 seeds=1 positionRetained=1"));return End(true,TEXT("complete"));
    }
};
}
void RunWildHomeTripScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto State=MakeShared<FWildHomeTrip>();State->World=World;State->Hero=Hero;State->Read=Read;State->Deadline=FPlatformTime::Seconds()+150;
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("WildHomeInput"),0));
    if(!Source || Source->FormatVersion!=38 || Source->HomeModules.Num()!=25 || Source->Facilities.Num()!=2 || !Hero->LoadProgress(TEXT("WildHomeInput")) || !State->Bind() || Hero->Inventory->Count(EFrontierItem::Food)!=1 || Hero->Inventory->Count(EFrontierItem::Seeds)!=1 || Hero->Inventory->Count(EFrontierItem::Material)!=(Read?2:0)){State->End(false,TEXT("source"));return;}
    const auto* Partner=Source->Creatures.FindByPredicate([&](const FCreatureSaveRecord& R){return R.bOwned && R.Id==State->PetId;});
    if(!Partner || !Hero->GetActorLocation().Equals(Source->PlayerTransform.GetLocation(),.01) || !State->Pet->GetActorLocation().Equals(Partner->Transform.GetLocation(),.01)){State->End(false,TEXT("source-position"));return;}
    UE_LOG(LogTemp,Display,TEXT("CF_WILD_HOME_SOURCE PASS positionRetained=1 pet=%s"),*State->PetId.ToString());
    State->House=HomeLayout::Collect(World);State->Facilities=Source->Facilities;State->Nickname=State->Pet->Nickname;
    State->Input=NewObject<UHomeMovementProbe>(Hero);State->Input->RegisterComponent();State->At=World->GetTimeSeconds();State->Phase=-1;
    if(Read){State->Phase=-2;UE_LOG(LogTemp,Display,TEXT("CF_WILD_HOME_RESTORE PASS material=2 food=1 seeds=1"));}
    State->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFWildHomePreview"));
    if(State->Preview){State->ImageRoot=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("WildHomePreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*State->ImageRoot,true);UE_LOG(LogTemp,Display,TEXT("CF_WILD_HOME_PREVIEW_ROOT %s"),*State->ImageRoot);}
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float){return State->Tick();}));
}
