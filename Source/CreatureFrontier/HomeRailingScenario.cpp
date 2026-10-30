#include "ConstructionScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "HomeScenario.h"
#include "HomeLayout.h"
#include "HomeModule.h"
#include "HomeBuildingComponent.h"
#include "BaseBuildingComponent.h"
#include "CompanionConstructionComponent.h"
#include "CompanionCargoComponent.h"
#include "StorageInteractionComponent.h"
#include "InventoryComponent.h"
#include "FrontierSaveGame.h"
#include "WorkStation.h"
#include "FarmCropComponent.h"
#include "WildEncounter.h"
#include "InputRoutingQA.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/Ticker.h"
#include "InputKeyEventArgs.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

namespace
{
// 从真实加工完成档延续；只经原输入建造与供料，不改库存、碰撞、需求或生态规则。
struct FHomeRailing
{
    UWorld* World;ACFPlayerCharacter* Hero;TWeakObjectPtr<ACreatureCharacter> Pet;
    TWeakObjectPtr<AHomeModule> Box,Floor,Site;TWeakObjectPtr<UHomeMovementProbe> Input;
    TArray<FHomeModuleRecord> House;TArray<FFacilitySaveRecord> Facilities;TArray<FVector> Route;
    TMap<FGuid,FVector> WildPositions;TMap<EFrontierItem,int32> BoxItems;FGuid PetId;FString Nickname,Root;TSet<FString> Photos;
    bool Read=false,Preview=false,Mid=false,Worked=false;int32 Phase=-1,Point=0;
    double Deadline=0,At=0,ShotUntil=0,LastTime=0,ProgressAt=0,HeroTravel=0,PetTravel=0,WildTravel=0;
    FVector LastH,LastP,VelocityH,VelocityP,Anchor,PushStart;const FVector Railing=FVector(-1400,-400,290);
    void Face(FVector Target)
    {const auto R=(Target-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->SetActorRotation(FRotator(0,R.Yaw,0));Hero->FirstPersonCamera->SetRelativeRotation(FRotator::ZeroRotator);Hero->GetController()->SetControlRotation(R);Hero->FirstPersonCamera->SetWorldRotation(R);}
    bool Photo(const TCHAR* Name,FVector Target)
    {if(!Preview || Photos.Contains(Name))return false;Photos.Add(Name);Face(Target);FScreenshotRequest::RequestScreenshot(Root/(FString(Name)+TEXT(".png")),false,false);ShotUntil=FPlatformTime::Seconds()+.2;return true;}
    bool End(bool Pass,const TCHAR* Reason)
    {
        if(Input.IsValid())Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::LeftShift);
        auto* PC=Cast<APlayerController>(Hero->GetController());if(PC)PC->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::RightShift,IE_Released,0.f,false,FPlatformTime::Cycles64()));
        if(!Pass)Hero->SaveProgress(TEXT("HomeRailingBlocked"));
        UE_LOG(LogTemp,Display,TEXT("CF_HOME_RAILING %s reason=%s phase=%d heroTravel=%.1f petTravel=%.1f wildTravel=%.1f hero=%s pet=%s feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Reason,Phase,HeroTravel,PetTravel,WildTravel,*Hero->GetActorLocation().ToString(),Pet.IsValid()?*Pet->GetActorLocation().ToString():TEXT("none"),*Hero->Feedback);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    bool Bind()
    {
        Pet.Reset();Box.Reset();Floor.Reset();Site.Reset();int32 Owned=0;
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController()){Pet=*It;++Owned;}
        for(TActorIterator<AHomeModule> It(World);It;++It)if(!It->bPreview && !It->bStaged)
        {if(It->GetKind()==EHomeModuleKind::Storage)Box=*It;if(It->GetKind()==EHomeModuleKind::Floor && It->GetActorLocation().Equals(FVector(-1200,-400,280),.01))Floor=*It;if(It->GetKind()==EHomeModuleKind::Fence && It->GetActorLocation().Equals(Railing,.01))Site=*It;}
        if(Owned!=1 || !Pet.IsValid() || !Box.IsValid() || !Floor.IsValid() || (PetId.IsValid() && PetId!=Pet->PersistentId))return false;
        PetId=Pet->PersistentId;LastH=Hero->GetActorLocation();LastP=Pet->GetActorLocation();VelocityH=Hero->GetVelocity();VelocityP=Pet->GetVelocity();LastTime=World->GetTimeSeconds();return true;
    }
    bool Plan(FVector Goal,int32 Next)
    {
        auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);FNavLocation Projected;
        if(!Nav || !Nav->ProjectPointToNavigation(Goal,Projected,FVector(50,50,80)))return false;
        auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Hero->GetActorLocation(),Projected.Location,Hero);
        if(!Path || !Path->IsValid() || Path->IsPartial() || Path->PathPoints.Num()<2)return false;
        Route=Path->PathPoints;Point=1;Anchor=Hero->GetActorLocation();ProgressAt=World->GetTimeSeconds();Phase=Next;return CFInputQA::Press(Hero,EKeys::LeftShift);
    }
    bool PlanBox()
    {
        // 与原徒步供箱验证相同，选有净空、视线和完整路径的最近交互点。
        auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);if(!Nav)return false;TArray<FVector> Best;double Length=DBL_MAX;
        for(int32 I=0;I<8;++I)
        {
            const double A=I*PI/4;FNavLocation Ground;
            if(!Nav->ProjectPointToNavigation(Box->GetActorLocation()+FVector(FMath::Cos(A)*190,FMath::Sin(A)*190,0),Ground,FVector(60,60,160))){UE_LOG(LogTemp,Display,TEXT("CF_RAILING_BOX_REJECT index=%d reason=projection"),I);continue;}
            const auto* Capsule=Hero->GetCapsuleComponent();const auto Goal=Ground.Location+FVector(0,0,Capsule->GetScaledCapsuleHalfHeight());
            FCollisionQueryParams Q(SCENE_QUERY_STAT(RailingStoreStand),false,Hero);FHitResult Hit;
            if(FVector::Dist(Goal,Box->GetActorLocation())>280 || World->OverlapBlockingTestByChannel(Goal,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius()-1,Capsule->GetScaledCapsuleHalfHeight()-1),Q)){UE_LOG(LogTemp,Display,TEXT("CF_RAILING_BOX_REJECT index=%d reason=space goal=%s"),I,*Goal.ToString());continue;}
            if(World->LineTraceSingleByChannel(Hit,Goal+FVector(0,0,64),Box->GetActorLocation()+FVector(0,0,40),ECC_Visibility,Q) && Hit.GetActor()!=Box.Get()){UE_LOG(LogTemp,Display,TEXT("CF_RAILING_BOX_REJECT index=%d reason=sight actor=%s goal=%s"),I,*GetNameSafe(Hit.GetActor()),*Goal.ToString());continue;}
            auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Hero->GetActorLocation(),Ground.Location,Hero);
            if(Path && Path->IsValid() && !Path->IsPartial() && Path->PathPoints.Num()>1 && Path->GetPathLength()<Length){Best=Path->PathPoints;Length=Path->GetPathLength();}
            else if(!Path || !Path->IsValid() || Path->IsPartial())UE_LOG(LogTemp,Display,TEXT("CF_RAILING_BOX_REJECT index=%d reason=path valid=%d partial=%d goal=%s"),I,Path && Path->IsValid(),Path && Path->IsPartial(),*Goal.ToString());
        }
        if(Best.IsEmpty())return false;Route=MoveTemp(Best);Point=1;Anchor=Hero->GetActorLocation();ProgressAt=World->GetTimeSeconds();Phase=0;return CFInputQA::Press(Hero,EKeys::LeftShift);
    }
    int32 Move()
    {
        const auto Here=Hero->GetActorLocation();while(Point<Route.Num() && FVector::Dist2D(Here,Route[Point])<16)++Point;
        if(Point==Route.Num()){Input->Direction=FVector::ZeroVector;if(Hero->GetVelocity().Size2D()>3)return 0;CFInputQA::Release(Hero,EKeys::LeftShift);return 1;}
        const auto D=(Route[Point]-Here).GetSafeNormal2D();Input->Direction=D;Face(Here+D*500+FVector(0,0,64));
        if(FVector::Dist2D(Here,Anchor)>20){Anchor=Here;ProgressAt=World->GetTimeSeconds();}return World->GetTimeSeconds()-ProgressAt>5?-1:0;
    }
    bool Motion(const ACharacter* Pawn,FVector Old,FVector PreviousVelocity,double Delta) const
    {
        const auto* M=Pawn->GetCharacterMovement();const auto Now=Pawn->GetActorLocation();
        return M && Delta>=0 && FMath::IsFinite(Delta) && !Now.ContainsNaN() && Pawn->GetActorEnableCollision() && M->MovementMode!=MOVE_None &&
            FVector::Dist2D(Old,Now)<=FMath::Max(static_cast<double>(M->GetMaxSpeed()),PreviousVelocity.Size2D())*Delta+3 &&
            FMath::Abs(Now.Z-Old.Z)<=FMath::Max(FMath::Abs(Pawn->GetVelocity().Z),FMath::Abs(PreviousVelocity.Z))*Delta+.5*FMath::Abs(World->GetGravityZ())*Delta*Delta+M->MaxStepHeight+3;
    }
    bool Invariants() const
    {
        const auto Current=HomeLayout::Collect(World);if(Current.Num()!=House.Num()+(Site.IsValid()?1:0))return false;
        for(const auto& Old:House)
        {
            const auto* Now=Current.FindByPredicate([&](const FHomeModuleRecord& R){return R.Id==Old.Id;});
            if(!Now || Now->Definition!=Old.Definition || Now->FloorId!=Old.FloorId || Now->DoorState!=Old.DoorState || !Now->Transform.Equals(Old.Transform,.01) || Now->PaidMaterials!=Old.PaidMaterials || !Now->PaidExtraItems.OrderIndependentCompareEqual(Old.PaidExtraItems) || Now->bUnderConstruction!=Old.bUnderConstruction)return false;
            if(Old.Id!=Box->PersistentId && !Now->StoredItems.OrderIndependentCompareEqual(Old.StoredItems))return false;
        }
        for(int32 I=1;I<static_cast<int32>(EFrontierItem::Count);++I)if(Box->StorageInventory->Count(static_cast<EFrontierItem>(I))!=BoxItems.FindRef(static_cast<EFrontierItem>(I)))return false;
        int32 Count=0;for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt)
        {++Count;const auto* Old=Facilities.FindByPredicate([&](const FFacilitySaveRecord& R){return R.Id==It->PersistentId;});if(!Old || !It->GetActorTransform().Equals(Old->Transform,.01) || It->StoredFood!=Old->StoredFood || It->RequiredSpecialty!=Old->Specialty || It->Crop->Seeds!=Old->Seeds || It->Crop->bPlanted!=Old->bPlanted || !FMath::IsNearlyEqual(It->Crop->GrowthWork,Old->GrowthWork,.01f))return false;}
        const int32 SiteMaterial=Site.IsValid()?Site->bUnderConstruction?Site->StorageInventory->Count(EFrontierItem::Material):Site->PaidMaterials:0;
        return Count==Facilities.Num() && Pet->Nickname==Nickname && Hero->Inventory->Count(EFrontierItem::Food)==2 && Hero->Inventory->Count(EFrontierItem::Seeds)==1 && Hero->Inventory->Count(EFrontierItem::Fiber)==0 &&
            Hero->Inventory->Count(EFrontierItem::Material)+Box->StorageInventory->Count(EFrontierItem::Material)+Pet->Cargo->Count()+SiteMaterial==4;
    }
    bool Tick()
    {
        if(!Pet.IsValid() || !Box.IsValid() || FPlatformTime::Seconds()>Deadline)return End(false,TEXT("lifetime"));
        const double Delta=World->GetTimeSeconds()-LastTime;
        if(!Motion(Hero,LastH,VelocityH,Delta) || !Motion(Pet.Get(),LastP,VelocityP,Delta) || Hero->Health<=0 || Pet->Health<=0 || !Pet->IsActorTickEnabled())return End(false,TEXT("motion-health"));
        HeroTravel+=FVector::Dist(LastH,Hero->GetActorLocation());PetTravel+=FVector::Dist(LastP,Pet->GetActorLocation());LastH=Hero->GetActorLocation();LastP=Pet->GetActorLocation();VelocityH=Hero->GetVelocity();VelocityP=Pet->GetVelocity();LastTime=World->GetTimeSeconds();
        int32 Encounters=0;for(TActorIterator<AWildEncounter> It(World);It;++It){++Encounters;if(!It->IsActorTickEnabled())return End(false,TEXT("ecology"));}if(Encounters!=3)return End(false,TEXT("ecology-count"));
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Disposition!=ECreatureDisposition::Tamed && It->Health>0)
        {if(!It->IsActorTickEnabled() || !It->GetActorEnableCollision() || It->GetCharacterMovement()->MovementMode==MOVE_None)return End(false,TEXT("wild-disabled"));if(const auto* Old=WildPositions.Find(It->PersistentId))WildTravel+=FVector::Dist(*Old,It->GetActorLocation());WildPositions.Add(It->PersistentId,It->GetActorLocation());}
        if(!Invariants())return End(false,TEXT("budget-home"));if(FPlatformTime::Seconds()<ShotUntil)return true;
        if(Phase==-1)
        {
            if(World->GetTimeSeconds()-At<.2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return true;
            if(Read){Phase=4;At=World->GetTimeSeconds();return true;}
            if(!PlanBox())return End(false,TEXT("box-path"));return true;
        }
        if(Phase==0 || Phase==1 || Phase==3 || Phase==5 || Phase==8)
        {
            const int32 Result=Move();if(Result<0)return End(false,TEXT("walk-blocked"));if(!Result)return true;
            if(Phase==0)
            {
                Face(Box->GetActorLocation()+FVector(0,0,40));
                FHitResult BoxHit;FCollisionQueryParams BoxQuery(SCENE_QUERY_STAT(RailingBoxAim),false,Hero);const auto Eye=Hero->FirstPersonCamera->GetComponentLocation();World->LineTraceSingleByChannel(BoxHit,Eye,Eye+Hero->FirstPersonCamera->GetForwardVector()*800,ECC_Visibility,BoxQuery);
                UE_LOG(LogTemp,Display,TEXT("CF_RAILING_BOX_AIM box=%s distance=%.1f hit=%s point=%s"),*Box->GetActorLocation().ToString(),Hero->GetDistanceTo(Box.Get()),*GetNameSafe(BoxHit.GetActor()),*BoxHit.ImpactPoint.ToString());
                if(!CFInputQA::Press(Hero,EKeys::E) || Hero->Storage->GetCrate()!=Box.Get())return End(false,TEXT("box-open"));
                for(int32 I=0;I<4;++I)if(!CFInputQA::Press(Hero,EKeys::Enter))return End(false,TEXT("box-deposit"));
                if(Hero->Inventory->Count(EFrontierItem::Material)!=0 || Box->StorageInventory->Count(EFrontierItem::Material)!=4)return End(false,TEXT("box-budget"));
                Photo(TEXT("Stored"),Box->GetActorLocation()+FVector(0,0,40));
                if(!CFInputQA::Press(Hero,EKeys::E) || Hero->Storage->IsOpen() || !CFInputQA::Press(Hero,EKeys::V) || Hero->GetDeliveryCrate()!=Box.Get())return End(false,TEXT("box-select"));
                UE_LOG(LogTemp,Display,TEXT("CF_RAILING_STORE PASS amount=4 input=1"));if(!Plan(FVector(-1200,-400,330),1))return End(false,TEXT("up-path"));return true;
            }
            if(Phase==1)
            {
                if(Hero->GetActorLocation().Z<300)return End(false,TEXT("upper-floor"));Face(FVector(-1360,-400,290));
                if(!CFInputQA::Press(Hero,EKeys::B) || !CFInputQA::Press(Hero,EKeys::Eight))return End(false,TEXT("plan-open"));
                for(int32 I=0;Hero->HomeBuilding->Selected!=EHomeModuleKind::Fence && I<static_cast<int32>(EHomeModuleKind::Count);++I)if(!CFInputQA::Press(Hero,EKeys::V))return End(false,TEXT("plan-select"));
                if(!CFInputQA::Press(Hero,EKeys::Nine))return End(false,TEXT("plan-rotate"));
                CastChecked<APlayerController>(Hero->GetController())->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::RightShift,IE_Pressed,1.f,false,FPlatformTime::Cycles64()));Phase=2;At=World->GetTimeSeconds();return true;
            }
            if(Phase==3){Phase=4;At=World->GetTimeSeconds();return true;}
            if(Phase==5){PushStart=Hero->GetActorLocation();At=World->GetTimeSeconds();if(!CFInputQA::Press(Hero,EKeys::LeftShift))return End(false,TEXT("push-input"));Input->Direction=FVector(-1,0,0);Face(Railing+FVector(0,0,70));Phase=6;return true;}
            Phase=9;At=World->GetTimeSeconds();return true;
        }
        if(Phase==2)
        {
            Face(FVector(-1360,-400,290));Hero->HomeBuilding->UpdatePreview();auto* Ghost=Hero->HomeBuilding->GetPreview();
            if(!Ghost || Ghost->IsHidden() || !Ghost->GetActorLocation().Equals(Railing,.01))
            {if(World->GetTimeSeconds()-At>5)return End(false,TEXT("plan-preview"));return true;}
            FString Reason;if(!Hero->HomeBuilding->ValidatePlacement(Ghost->GetActorTransform(),Floor->PersistentId,Reason,true))return End(false,*Reason);
            if(!FMath::IsNearlyEqual(Ghost->GetActorRotation().Yaw,90.,.01))return End(false,TEXT("plan-yaw"));
            if(Photo(TEXT("Preview"),FVector(-1360,-400,290)))return true;
            if(!CFInputQA::Action(Hero,TEXT("Attack"),EKeys::LeftMouseButton))return End(false,TEXT("plan-confirm"));
            CastChecked<APlayerController>(Hero->GetController())->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::RightShift,IE_Released,0.f,false,FPlatformTime::Cycles64()));
            for(TActorIterator<AHomeModule> It(World);It;++It)if(!It->bPreview && It->GetKind()==EHomeModuleKind::Fence && It->GetActorLocation().Equals(Railing,.01))Site=*It;
            if(!Site.IsValid() || !Site->bUnderConstruction || Site->FloorId!=Floor->PersistentId || !CFInputQA::Press(Hero,EKeys::B) || Hero->Building->IsActive() || Hero->GetDeliveryCrate()!=Box.Get())return End(false,TEXT("plan-site"));
            Face(Railing+FVector(0,0,90));if(AHomeModule::TraceConstruction(Hero)!=Site.Get() || !CFInputQA::Press(Hero,EKeys::E) || Pet->Construction->GetSite()!=Site.Get())return End(false,TEXT("assign"));
            UE_LOG(LogTemp,Display,TEXT("CF_RAILING_PLAN PASS input=1 material=0 pending=1"));Photo(TEXT("Planned"),Railing+FVector(0,0,90));if(!Plan(FVector(-1120,-480,380),3))return End(false,TEXT("stand-path"));return true;
        }
        if(Phase==4)
        {
            if(!Site.IsValid())return End(false,TEXT("site-lost"));
            if(!Mid && Pet->Cargo->Count()==3 && Box->StorageInventory->Count(EFrontierItem::Material)==1 && Site->StorageInventory->Count(EFrontierItem::Material)==0)
            {if(!Hero->SaveProgress(TEXT("HomeRailingMid")))return End(false,TEXT("mid-save"));Mid=true;UE_LOG(LogTemp,Display,TEXT("CF_RAILING_MID PASS cargo=3 crate=1 site=0"));Photo(TEXT("Carrying"),Railing+FVector(0,0,90));return true;}
            if(Site->ConstructionWork>0 && !Worked){Worked=true;Photo(TEXT("Working"),Railing+FVector(0,0,90));return true;}
            if(Site->bUnderConstruction){if(World->GetTimeSeconds()-At>150)return End(false,TEXT("construction-timeout"));return true;}
            if(!Mid || !Worked || Site->PaidMaterials!=4 || Pet->Cargo->Count()!=0 || Box->StorageInventory->Count(EFrontierItem::Material)!=0)return End(false,TEXT("completion-budget"));
            UE_LOG(LogTemp,Display,TEXT("CF_RAILING_BUILT PASS paid=4 modules=26"));if(!Plan(FVector(-1270,-400,380),5))return End(false,TEXT("collision-path"));return true;
        }
        if(Phase==6)
        {
            if(Hero->GetActorLocation().X<-1370 || Hero->GetActorLocation().Z<300)return End(false,TEXT("collision-crossed"));if(World->GetTimeSeconds()-At<2)return true;
            Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::LeftShift);Phase=7;return true;
        }
        if(Phase==7)
        {
            if(Hero->GetVelocity().Size2D()>3)return true;if(PushStart.X-Hero->GetActorLocation().X<50 || Hero->GetActorLocation().X<-1370)return End(false,TEXT("collision-no-contact"));
            UE_LOG(LogTemp,Display,TEXT("CF_RAILING_COLLISION PASS walkInput=1 advance=%.1f x=%.2f z=%.2f"),PushStart.X-Hero->GetActorLocation().X,Hero->GetActorLocation().X,Hero->GetActorLocation().Z);Photo(TEXT("Collision"),Railing+FVector(0,0,70));
            if(!Plan(FVector(-1490,-1080,40),8))return End(false,TEXT("down-path"));return true;
        }
        if(Phase==9)
        {
            if(Pet->GetDistanceTo(Hero)>250 || Pet->Activity!=ECreatureActivity::Follow || Pet->GetActorLocation().Z>100)
            {if(World->GetTimeSeconds()-At>25)return End(false,TEXT("return-companion"));return true;}
            if(Photo(TEXT("Home"),Pet->GetActorLocation()+FVector(0,0,20)))return true;
            const auto H=Hero->GetActorLocation(),P=Pet->GetActorLocation();const auto HP=Hero->Health,PHP=Pet->Health;
            if(HeroTravel<(Read?700:1500) || WildTravel<50 || !Hero->SaveProgress(TEXT("HomeRailingDone")) || !Hero->LoadProgress(TEXT("HomeRailingDone")) || !Bind() || !Hero->GetActorLocation().Equals(H,.01) || !Pet->GetActorLocation().Equals(P,.01) || Hero->Health!=HP || Pet->Health!=PHP || !Invariants() || !Site.IsValid() || Site->bUnderConstruction)return End(false,TEXT("done-save"));
            UE_LOG(LogTemp,Display,TEXT("CF_RAILING_LIFE PASS version=38 ecology=3 positionRetained=1 material=0 food=2 seeds=1"));return End(true,TEXT("complete"));
        }
        return true;
    }
};
}
void RunHomeRailingScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto S=MakeShared<FHomeRailing>();S->World=World;S->Hero=Hero;S->Read=Read;S->Deadline=FPlatformTime::Seconds()+240;
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("HomeRailingInput"),0));
    if(!Source || Source->FormatVersion!=38 || Source->HomeModules.Num()!=(Read?26:25) || Source->Facilities.Num()!=2 || !Hero->LoadProgress(TEXT("HomeRailingInput")) || !S->Bind()) {S->End(false,TEXT("source"));return;}
    const auto* Partner=Source->Creatures.FindByPredicate([&](const FCreatureSaveRecord& R){return R.bOwned && R.Id==S->PetId;});
    if(!Partner || !Hero->GetActorLocation().Equals(Source->PlayerTransform.GetLocation(),.01) || !S->Pet->GetActorLocation().Equals(Partner->Transform.GetLocation(),.01) || Hero->Inventory->Count(EFrontierItem::Material)!=(Read?0:4) || S->Box->StorageInventory->Count(EFrontierItem::Material)!=(Read?1:0) || S->Pet->Cargo->Count()!=(Read?3:0) || (Read?(!S->Site.IsValid() || !S->Site->bUnderConstruction || S->Pet->Construction->GetSite()!=S->Site.Get()):S->Site.IsValid())) {S->End(false,TEXT("source-state"));return;}
    S->House=HomeLayout::Collect(World);if(Read)S->House.RemoveAll([&](const FHomeModuleRecord& R){return R.Id==S->Site->PersistentId;});
    S->Facilities=Source->Facilities;S->BoxItems=S->Box->StorageInventory->GetStacks();S->Nickname=S->Pet->Nickname;S->Mid=Read;
    S->At=World->GetTimeSeconds();S->Input=NewObject<UHomeMovementProbe>(Hero);S->Input->RegisterComponent();S->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFHomeRailingPreview"));
    if(S->Preview){S->Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("HomeRailingPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*S->Root,true);UE_LOG(LogTemp,Display,TEXT("CF_RAILING_PREVIEW_ROOT %s"),*S->Root);}
    UE_LOG(LogTemp,Display,TEXT("CF_RAILING_SOURCE PASS read=%d positionRetained=1 pet=%s"),Read,*S->PetId.ToString());
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){return S->Tick();}));
}
