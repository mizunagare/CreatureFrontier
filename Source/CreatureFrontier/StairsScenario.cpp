#include "StairsScenario.h"
#include "UpperFarmScenario.h"
#include "HomeScenario.h"
#include "HomeModule.h"
#include "HomeLayout.h"
#include "HomeBuildingComponent.h"
#include "BaseBuildingComponent.h"
#include "CFPlayerCharacter.h"
#include "CFHUD.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "InventoryComponent.h"
#include "StorageInteractionComponent.h"
#include "PlayerPaceComponent.h"
#include "FrontierSaveGame.h"
#include "InputRoutingQA.h"
#include "WildEncounter.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "NavMesh/RecastNavMesh.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Containers/Ticker.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

namespace
{
bool SameModules(const TArray<FHomeModuleRecord>& Left,const TArray<FHomeModuleRecord>& Right)
{
    if(Left.Num()!=Right.Num())return false;
    for(const auto& Record:Left)
    {
        const auto* Other=Right.FindByPredicate([&](const FHomeModuleRecord& Value){return Value.Id==Record.Id;});
        if(!Other || Other->FloorId!=Record.FloorId || Other->Definition!=Record.Definition || !Other->Transform.Equals(Record.Transform,.01) ||
            Other->DoorState!=Record.DoorState || Other->PaidMaterials!=Record.PaidMaterials || !Other->StoredItems.OrderIndependentCompareEqual(Record.StoredItems) ||
            !Other->PaidExtraItems.OrderIndependentCompareEqual(Record.PaidExtraItems))return false;
    }
    return true;
}
struct FStairsTest
{
    UWorld* World;ACFPlayerCharacter* Hero;int32 Mode=0,Phase=0,Point=0;FGuid PetId;
    TWeakObjectPtr<ACreatureCharacter> Pet;TWeakObjectPtr<AHomeModule> Stair,Box,Upper;
    TWeakObjectPtr<UHomeMovementProbe> Input;TArray<FVector> ReturnPoints;
    FVector PreviousHero,PreviousPet,Anchor;double At=0,Deadline=0,ProgressAt=0,HeroTravel=0,PetTravel=0,HeroMax=0,PetMax=0;
    bool HeroStepUp=false,PetStepUp=false,HeroStepDown=false,PetStepDown=false,OccupiedGuard=false,OccupiedShot=false;double OccupiedAt=0;
    FString Root;TSharedPtr<bool> Shot;int32 Shots=0;
    void Face(FVector Target)
    {
        const auto Rotation=(Target-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
        Hero->SetActorRotation(FRotator(0,Rotation.Yaw,0));Hero->FirstPersonCamera->SetRelativeRotation(FRotator::ZeroRotator);
        Hero->GetController()->SetControlRotation(Rotation);Hero->FirstPersonCamera->SetWorldRotation(Rotation);
    }
    void Stop(){if(Input.IsValid())Input->Direction=FVector::ZeroVector;Hero->GetCharacterMovement()->StopMovementImmediately();Hero->ConsumeMovementInputVector();}
    bool Finish(bool Pass,const TCHAR* Reason)
    {
        Stop();if(Input.IsValid())Input->DestroyComponent();CFInputQA::Release(Hero,EKeys::LeftShift);
        UE_LOG(LogTemp,Display,TEXT("CF_STAIRS %s mode=%d phase=%d reason=%s heroTravel=%.1f petTravel=%.1f heroMax=%.1f petMax=%.1f hero=%s partner=%s feedback=%s"),
            Pass?TEXT("PASS"):TEXT("FAIL"),Mode,Phase,Reason,HeroTravel,PetTravel,HeroMax,PetMax,*Hero->GetActorLocation().ToString(),Pet.IsValid()?*Pet->GetActorLocation().ToString():TEXT("无"),*Hero->Feedback);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    void Advance(int32 Next){Phase=Next;At=World->GetTimeSeconds();ProgressAt=At;Anchor=Hero->GetActorLocation();Shot.Reset();}
    bool Picture(const TCHAR* Scene)
    {
        if(Root.IsEmpty())return true;
        if(!Shot)
        {
            auto* PC=Cast<APlayerController>(Hero->GetController());auto* HUD=PC?Cast<ACFHUD>(PC->GetHUD()):nullptr;
            if(!HUD || HUD->GetLayoutAudit().Frame==0)return false;
            Shot=MakeShared<bool>(false);const auto Done=Shot;const auto Handle=MakeShared<FDelegateHandle>();
            *Handle=FScreenshotRequest::OnScreenshotRequestProcessed().AddLambda([Done,Handle](){*Done=true;FScreenshotRequest::OnScreenshotRequestProcessed().Remove(*Handle);});
            FScreenshotRequest::RequestScreenshot(Root/(FString(Scene)+TEXT(".png")),false,false);
            UE_LOG(LogTemp,Display,TEXT("CF_STAIRS_SHOT scene=%s viewport=%.0fx%.0f frame=%llu"),Scene,HUD->GetLayoutAudit().Viewport.X,HUD->GetLayoutAudit().Viewport.Y,HUD->GetLayoutAudit().Frame);
        }
        if(!*Shot)return false;++Shots;return true;
    }
    AHomeModule* Build(EHomeModuleKind Kind,FVector Position,FGuid Support=FGuid(),double Yaw=0)
    {Hero->HomeBuilding->Selected=Kind;return Hero->HomeBuilding->BuildAt(FTransform(FRotator(0,Yaw,0),Position),Support);}
    bool FindActors()
    {
        Box.Reset();Stair.Reset();Upper.Reset();Pet.Reset();
        for(TActorIterator<AHomeModule> It(World);It;++It)if(!It->bPreview)
        {if(It->GetKind()==EHomeModuleKind::Stairs)Stair=*It;if(It->GetKind()==EHomeModuleKind::Storage)Box=*It;if(It->GetKind()==EHomeModuleKind::Floor && It->GetActorLocation().Z>0)Upper=*It;}
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController()) {if(Pet.IsValid())return false;Pet=*It;}
        return Box.IsValid() && Stair.IsValid() && Upper.IsValid() && Pet.IsValid();
    }
    bool Snapshot()
    {
        const auto* Saved=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("StairsAutomation"),0));
        if(!Saved || (Saved->FormatVersion<34 || Saved->FormatVersion>FrontierCurrentSaveVersion) || Saved->HomeModules.Num()!=12 || !SameModules(Saved->HomeModules,HomeLayout::Collect(World)) ||
            !Hero->Inventory->GetStacks().OrderIndependentCompareEqual(Saved->InventoryStacks) || Hero->Inventory->Count(EFrontierItem::Material)!=109 || !FindActors())return false;
        const auto* Record=Saved->Creatures.FindByPredicate([this](const FCreatureSaveRecord& Value){return Value.bOwned && Value.Id==Pet->PersistentId;});
        return Record && ( !PetId.IsValid() || Record->Id==PetId ) && Pet->Activity==ECreatureActivity::Follow && Pet->GetActorLocation().Z>350 &&
            Pet->GetActorTransform().Equals(Record->Transform,.01) && FMath::IsNearlyEqual(Pet->Nutrition,Record->Nutrition,.01f) &&
            FMath::IsNearlyEqual(Pet->Stamina,Record->Stamina,.01f) && Hero->GetActorTransform().Equals(Saved->PlayerTransform,.01) &&
            Box->StorageInventory->Count(EFrontierItem::Material)==3 && Upper->FloorId.IsValid() && Stair->FloorId.IsValid();
    }
    bool Init()
    {
        // 只有固定建造观察站位与初始物资／伙伴是夹具；正式行走阶段不再改双方位置。
        Hero->GetCharacterMovement()->DisableMovement();Hero->SetActorLocation(FVector(0,-1200,200));Hero->Inventory->Restore({{EFrontierItem::Material,200}});
        if(Build(EHomeModuleKind::Floor,FVector(0,-800,280)))return false;
        auto* A=Build(EHomeModuleKind::Floor,FVector(-400,-800,-40));auto* B=Build(EHomeModuleKind::Floor,FVector(0,-800,-40));
        if(!A || !B)return false;
        for(auto* Floor:{A,B})for(double Side:{-200.,200.})if(!Build(EHomeModuleKind::Wall,Floor->GetActorLocation()+FVector(0,Side,10),Floor->PersistentId))return false;
        const int32 Before=Hero->Inventory->Count(EFrontierItem::Material);
        if(Build(EHomeModuleKind::Stairs,FVector(-400,-800,-30),A->PersistentId) || Hero->Inventory->Count(EFrontierItem::Material)!=Before)return false;
        if(!CFInputQA::Press(Hero,EKeys::B) || !CFInputQA::Press(Hero,EKeys::Eight) ||
            !CFHomeInputQA::Place(World,Hero,EHomeModuleKind::Floor,FVector(0,-1200,200),FVector(0,-1000,240),FVector(0,-800,280)))return false;
        for(TActorIterator<AHomeModule> It(World);It;++It)if(!It->bPreview && It->GetKind()==EHomeModuleKind::Floor && It->GetActorLocation().Z>0)Upper=*It;
        if(!Upper.IsValid() || !Build(EHomeModuleKind::Wall,FVector(0,-1000,290),Upper->PersistentId) || !Build(EHomeModuleKind::Wall,FVector(0,-600,290),Upper->PersistentId) ||
            !Build(EHomeModuleKind::Roof,FVector(0,-800,600),Upper->PersistentId))return false;
        Box=Build(EHomeModuleKind::Storage,FVector(100,-900,290),Upper->PersistentId);if(!Box.IsValid())return false;
        Hero->SetActorLocation(FVector(-750,-1100,200));Face(FVector(-400,-800,-25));
        for(int32 Index=0;Hero->HomeBuilding->Selected!=EHomeModuleKind::Stairs && Index<static_cast<int32>(EHomeModuleKind::Count);++Index)if(!CFInputQA::Press(Hero,EKeys::V))return false;
        Hero->HomeBuilding->UpdatePreview();
        for(int32 Index=1;Index<=4;++Index)
        {
            if(!CFInputQA::Press(Hero,EKeys::Nine))return false;auto* Preview=Hero->HomeBuilding->GetPreview();
            if(!Preview || !FMath::IsNearlyEqual(Preview->GetActorRotation().Yaw,FMath::UnwindDegrees(Index*90.),.01))return false;
        }
        return Hero->HomeBuilding->GetPreview() && Hero->HomeBuilding->GetPreview()->GetActorLocation().Equals(FVector(-400,-800,-30),.01) && Hero->HomeBuilding->GetHint().Contains(TEXT("木楼梯"));
    }
    bool MoveTo(FVector Goal)
    {
        const auto Now=Hero->GetActorLocation();const auto Direction=(Goal-Now).GetSafeNormal2D();Face(Now+Direction*500+FVector(0,0,64));Input->Direction=Direction;
        if(FVector::Dist2D(Now,Anchor)>20){Anchor=Now;ProgressAt=World->GetTimeSeconds();}
        return World->GetTimeSeconds()-ProgressAt<=5;
    }
    bool Tick()
    {
        if(FPlatformTime::Seconds()>Deadline)return Finish(false,TEXT("deadline"));
        const double Elapsed=World->GetTimeSeconds()-At;
        if(Phase==0)
        {
            if(Elapsed<.5 || !Picture(TEXT("Preview")))return true;
            if(!CFInputQA::Action(Hero,TEXT("Attack"),EKeys::LeftMouseButton))return Finish(false,TEXT("confirm"));
            for(TActorIterator<AHomeModule> It(World);It;++It)if(!It->bPreview && It->GetKind()==EHomeModuleKind::Stairs)Stair=*It;
            if(!Stair.IsValid() || Hero->Inventory->Count(EFrontierItem::Material)!=112 || HomeLayout::Collect(World).Num()!=12)return Finish(false,TEXT("input-budget"));
            const bool Guard=!Hero->HomeBuilding->Dismantle(Upper.Get()) && !Build(EHomeModuleKind::Roof,FVector(-400,-800,280),Stair->FloorId) && Hero->Feedback.Contains(TEXT("净空")) && Hero->Inventory->Count(EFrontierItem::Material)==112;
            if(!Guard || !CFInputQA::Action(Hero,TEXT("Capture"),EKeys::RightMouseButton))return Finish(false,TEXT("support-or-headroom"));
            UE_LOG(LogTemp,Display,TEXT("CF_STAIRS_INPUT PASS modules=12 material=112 upper=1 stairs=1 rotate4=1 support=1 headroom=1"));
            Hero->SetActorLocation(FVector(0,-1200,200));Hero->Building->Selected=EWorkSpecialty::Farming;FString FacilityReason;
            const FTransform Facility(FRotator::ZeroRotator,FVector(0,-700,325),FVector(2,1,.7));
            if(!Hero->Building->ValidatePlacement(Facility,FacilityReason) || !HomeLayout::SupportsFacility(Facility,HomeLayout::Collect(World)))return Finish(false,*FacilityReason);
            UE_LOG(LogTemp,Display,TEXT("CF_STAIRS_FACILITY_SPACE PASS valid=1 support=1 noBuild=1"));
            if(FParse::Param(FCommandLine::Get(),TEXT("CFStairsWork"))) {RunUpperFarmScenario(World,Hero,false);return false;}
            Hero->SetActorLocation(FVector(-780,-800,40));Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);Face(FVector(0,-800,104));
            const FTransform Spawn(FVector(-950,-800,40));auto* Partner=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),Spawn,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            Partner->Definition=LoadObject<UCreatureDefinition>(nullptr,TEXT("/Game/Creatures/DA_Mosskip.DA_Mosskip"));Partner->FinishSpawning(Spawn);Partner->Tame(Hero->GetController());Pet=Partner;PetId=Partner->PersistentId;
            auto* Movement=NewObject<UHomeMovementProbe>(Hero);Movement->RegisterComponent();Input=Movement;
            CFInputQA::Press(Hero,EKeys::LeftShift);PreviousHero=Hero->GetActorLocation();PreviousPet=Partner->GetActorLocation();Advance(1);return true;
        }
        auto* Partner=Pet.Get();if(!Partner || (Phase<6 && !Stair.IsValid()) || !Input.IsValid())return Finish(false,TEXT("actors"));
        const auto Now=Hero->GetActorLocation(),PetNow=Partner->GetActorLocation();
        const double HeroStep=FVector::Dist(Now,PreviousHero),PetStep=FVector::Dist(PetNow,PreviousPet);
        HeroTravel+=FVector::Dist2D(Now,PreviousHero);PetTravel+=FVector::Dist2D(PetNow,PreviousPet);PreviousHero=Now;PreviousPet=PetNow;
        HeroMax=FMath::Max(HeroMax,Now.Z);PetMax=FMath::Max(PetMax,PetNow.Z);
        if(HeroStep>=80 || PetStep>=120 || Hero->Health<=0 || Partner->Health<=0 || !Hero->GetActorEnableCollision() || !Partner->GetActorEnableCollision() ||
            !Partner->IsActorTickEnabled() || !Hero->Pace->IsSlowWalking() || Hero->GetCharacterMovement()->MovementMode!=MOVE_Walking || Partner->GetCharacterMovement()->MovementMode==MOVE_None)return Finish(false,TEXT("continuous"));
        if(Phase<=3){HeroStepUp |= Now.Z>110 && Now.Z<330;PetStepUp |= PetNow.Z>110 && PetNow.Z<330;}
        else {HeroStepDown |= Now.Z>110 && Now.Z<330;PetStepDown |= PetNow.Z>110 && PetNow.Z<330;}
        if(Phase==1)
        {
            if(Elapsed<2)return true;auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,PetNow,FVector(-70,-800,290),Partner);
            if(!Path || !Path->IsValid() || Path->IsPartial())
            {
                if(Elapsed<8)return true;
                auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);const auto* Data=Nav?Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)):nullptr;
                UE_LOG(LogTemp,Display,TEXT("CF_STAIRS_NAV_DIAG valid=%d partial=%d points=%d data=%s cell=%.1f height=%.1f step=%.1f slope=%.1f"),Path && Path->IsValid(),Path && Path->IsPartial(),Path?Path->PathPoints.Num():0,*GetNameSafe(Data),Data?Data->GetCellSize(ENavigationDataResolution::Default):-1,Data?Data->GetCellHeight(ENavigationDataResolution::Default):-1,Data?Data->GetAgentMaxStepHeight(ENavigationDataResolution::Default):-1,Data?Data->AgentMaxSlope:-1);
                TArray<UStaticMeshComponent*> Meshes;Stair->GetComponents(Meshes);int32 Navigable=0;for(const auto* Mesh:Meshes)Navigable+=Mesh->CanEverAffectNavigation();
                UE_LOG(LogTemp,Display,TEXT("CF_STAIRS_MESH_DIAG count=%d navigable=%d"),Meshes.Num(),Navigable);
                const auto Geometry=AHomeModule::Parts(EHomeModuleKind::Stairs);
                for(int32 Index=0;Index<Geometry.Num();++Index)
                {
                    const FVector Sample=Stair->GetActorTransform().TransformPosition(Geometry[Index].Center+FVector(0,0,Geometry[Index].Extent.Z));FNavLocation Projected;FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(StairSurface),false,Hero);Query.AddIgnoredActor(Partner);
                    const bool Found=Nav && Nav->ProjectPointToNavigation(Sample,Projected,FVector(10,10,45));
                    const bool Solid=World->LineTraceSingleByChannel(Hit,Sample+FVector(0,0,80),Sample-FVector(0,0,50),ECC_Visibility,Query);
                    UE_LOG(LogTemp,Display,TEXT("CF_STAIRS_SAMPLE index=%d expected=%s projected=%d point=%s solid=%d hit=%s z=%.1f"),Index,*Sample.ToString(),Found,*Projected.Location.ToString(),Solid,*GetNameSafe(Hit.GetActor()),Hit.ImpactPoint.Z);
                }
                return Finish(false,TEXT("navigation"));
            }
            UE_LOG(LogTemp,Display,TEXT("CF_STAIRS_NAV PASS partial=0 points=%d length=%.1f"),Path->PathPoints.Num(),Path->GetPathLength());Advance(2);return true;
        }
        if(Phase==2)
        {
            if(FVector::Dist2D(Now,FVector(100,-800,380))>10){if(!MoveTo(FVector(100,-800,380)))return Finish(false,TEXT("ascend-blocked"));return true;}
            Stop();Advance(3);return true;
        }
        if(Phase==3)
        {
            if(Elapsed>15)return Finish(false,TEXT("upstairs-follow"));
            if(Partner->GetActorLocation().Z<350 || PetNow.X<-150 || FVector::Dist2D(PetNow,Partner->GetFollowDestination())>65)return true;
            if(Now.Z<350 || !HeroStepUp || !PetStepUp)return Finish(false,TEXT("stairs-not-used"));
            Face(Box->GetActorLocation()+FVector(0,0,40));
            if(!Hero->Storage->IsOpen() && Box->StorageInventory->Count(EFrontierItem::Material)==0)
            {
                if(!CFInputQA::Press(Hero,EKeys::E) || !Hero->Storage->IsOpen())return Finish(false,TEXT("upstairs-box"));
                for(int32 Count=0;Count<3;++Count)if(!CFInputQA::Press(Hero,EKeys::Enter))return Finish(false,TEXT("box-input"));
                if(!CFInputQA::Press(Hero,EKeys::E) || Hero->Storage->IsOpen())return Finish(false,TEXT("box-close"));
            }
            Face(Now+FVector(500,0,64));
            if(Elapsed<.5 || !Picture(TEXT("Upper")))return true;
            if(Box->StorageInventory->Count(EFrontierItem::Material)!=3 || Hero->Inventory->Count(EFrontierItem::Material)!=109)return Finish(false,TEXT("box-budget"));
            UE_LOG(LogTemp,Display,TEXT("CF_STAIRS_UP PASS heroZ=%.1f petZ=%.1f heroX=%.1f petX=%.1f landed=1 actualSteps=1 stored=3 material=109 pet=%s"),Now.Z,PetNow.Z,Now.X,PetNow.X,*PetId.ToString());
            if(!Hero->SaveProgress(TEXT("StairsAutomation")))return Finish(false,TEXT("save"));
            auto* Bad=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("StairsAutomation"),0));Bad->FormatVersion=33;
            const auto Before=HomeLayout::Collect(World);
            if(!UGameplayStatics::SaveGameToSlot(Bad,TEXT("StairsInvalid"),0) || Hero->LoadProgress(TEXT("StairsInvalid")) || !SameModules(Before,HomeLayout::Collect(World)) || Hero->Inventory->Count(EFrontierItem::Material)!=109)return Finish(false,TEXT("old-version-guard"));
            if(!Hero->LoadProgress(TEXT("StairsAutomation")) || !Snapshot())return Finish(false,TEXT("snapshot"));
            UE_LOG(LogTemp,Display,TEXT("CF_STAIRS_SAVE PASS version=%d identity=1 inventory=1 upper=1 bad33=1 pet=%s"),FrontierCurrentSaveVersion,*PetId.ToString());
            // 正常读档会释放持续输入，须重新按下慢行；不能沿用读档前的测试按住状态。
            if(!CFInputQA::Press(Hero,EKeys::LeftShift) || !Hero->Pace->IsSlowWalking())return Finish(false,TEXT("resume-input"));
            UE_LOG(LogTemp,Display,TEXT("CF_STAIRS_RESUME_INPUT PASS slow=1"));
            PreviousHero=Hero->GetActorLocation();PreviousPet=Pet->GetActorLocation();BeginReturn();return true;
        }
        if(Phase==4)
        {
            if(!OccupiedGuard && PetNow.X<-240 && PetNow.X>-540 && PetNow.Z>120 && PetNow.Z<360 && Hero->GetDistanceTo(Stair.Get())<580)
            {
                if(Hero->HomeBuilding->Dismantle(Stair.Get()) || !Hero->Feedback.Contains(TEXT("楼梯上有生物")) || Hero->Inventory->Count(EFrontierItem::Material)!=109)return Finish(false,TEXT("occupied-dismantle"));
                OccupiedGuard=true;OccupiedAt=World->GetTimeSeconds();Stop();
                UE_LOG(LogTemp,Display,TEXT("CF_STAIRS_OCCUPIED PASS rejected=1 material=109 pet=%s"),*PetId.ToString());
            }
            if(OccupiedGuard && !OccupiedShot)
            {
                if(!Root.IsEmpty() && (World->GetTimeSeconds()-OccupiedAt<.3 || !Picture(TEXT("Occupied"))))return true;
                OccupiedShot=true;Shot.Reset();Anchor=Now;ProgressAt=World->GetTimeSeconds();
            }
            while(Point<ReturnPoints.Num() && FVector::Dist2D(Now,ReturnPoints[Point])<12)++Point;
            if(Point<ReturnPoints.Num()){if(!MoveTo(ReturnPoints[Point]))return Finish(false,TEXT("descend-blocked"));return true;}
            Stop();Advance(5);return true;
        }
        if(Phase==5)
        {
            if(Elapsed>15)return Finish(false,TEXT("downstairs-follow"));
            if(PetNow.Z>100 || PetNow.X>-650 || FVector::Dist2D(PetNow,Partner->GetFollowDestination())>65)return true;
            if(Now.Z>100 || !OccupiedGuard || !HeroStepDown || !PetStepDown || HeroMax<350 || PetMax<350 || PetTravel<600)return Finish(false,TEXT("downstairs-state"));
            UE_LOG(LogTemp,Display,TEXT("CF_STAIRS_DOWN PASS continuous=1 heroZ=%.1f petZ=%.1f heroTravel=%.1f petTravel=%.1f pet=%s"),Now.Z,PetNow.Z,HeroTravel,PetTravel,*PetId.ToString());
            if(!Hero->HomeBuilding->Dismantle(Stair.Get()) || Hero->Inventory->Count(EFrontierItem::Material)!=115)return Finish(false,TEXT("dismantle"));Advance(6);return true;
        }
        if(Elapsed<2)return true;
        auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,PetNow,FVector(-70,-800,290),Partner);
        if(Path && Path->IsValid() && !Path->IsPartial()){if(Elapsed<8)return true;return Finish(false,TEXT("removed-navigation"));}
        UE_LOG(LogTemp,Display,TEXT("CF_STAIRS_DISMANTLE PASS refund=6 blocked=1 modules=11 material=115"));return Finish(true,TEXT("complete"));
    }
    void BeginReturn()
    {ReturnPoints={FVector(10,-800,380),FVector(10,-920,380),FVector(-150,-920,380),FVector(-400,-800,220),FVector(-950,-800,40)};Point=0;Advance(4);}
};
}
void RunStairsScenario(UWorld* World,ACFPlayerCharacter* Hero,int32 Mode)
{
    if(FParse::Param(FCommandLine::Get(),TEXT("CFStairsWork")) && (Mode==2 || FParse::Param(FCommandLine::Get(),TEXT("CFStairsPreview"))))
    {UE_LOG(LogTemp,Error,TEXT("CF_UPPER_FARM FAIL reason=invalid-mode"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    if(FParse::Param(FCommandLine::Get(),TEXT("CFStairsWork")) && Mode==1) {RunUpperFarmScenario(World,Hero,true);return;}
    const auto State=MakeShared<FStairsTest>();State->World=World;State->Hero=Hero;State->Mode=Mode;State->At=World->GetTimeSeconds();State->Deadline=FPlatformTime::Seconds()+90;
    if(Mode==2)
    {
        const auto* Old=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("StairsAutomation"),0));
        bool Pass=Old && Old->FormatVersion==33 && Old->HomeModules.Num()==14 && Hero->LoadProgress(TEXT("StairsAutomation")) && SameModules(Old->HomeModules,HomeLayout::Collect(World)) && Hero->Inventory->GetStacks().OrderIndependentCompareEqual(Old->InventoryStacks);
        if(Pass)for(const auto& Record:Old->Creatures)if(Record.bOwned)
        {ACreatureCharacter* Live=nullptr;for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->PersistentId==Record.Id)Live=*It;Pass &= Live && Live->Tamer==Hero->GetController() && Live->Nickname==Record.Nickname && Live->Activity==Record.Activity && Live->BondPoints==Record.BondPoints && Live->GetActorTransform().Equals(Record.Transform,.01);}
        UE_LOG(LogTemp,Display,TEXT("CF_STAIRS_LEGACY %s version=33 modules=14 identity=1 inventory=1"),Pass?TEXT("PASS"):TEXT("FAIL"));FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return;
    }
    if(Mode==1)
    {
        if(!Hero->LoadProgress(TEXT("StairsAutomation")) || !State->Snapshot()){State->Finish(false,TEXT("cold-snapshot"));return;}
        State->PetId=State->Pet->PersistentId;Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        auto* Input=NewObject<UHomeMovementProbe>(Hero);Input->RegisterComponent();State->Input=Input;CFInputQA::Press(Hero,EKeys::LeftShift);
        State->PreviousHero=Hero->GetActorLocation();State->PreviousPet=State->Pet->GetActorLocation();State->HeroMax=State->PreviousHero.Z;State->PetMax=State->PreviousPet.Z;
        UE_LOG(LogTemp,Display,TEXT("CF_STAIRS_RESTORE PASS version=%d modules=12 stored=3 material=109 pet=%s"),Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("StairsAutomation"),0))->FormatVersion,*State->PetId.ToString());State->BeginReturn();
    }
    else
    {
        for(TActorIterator<AWildEncounter> It(World);It;++It)It->SetActorTickEnabled(false);
        for(TActorIterator<ACreatureCharacter> It(World);It;++It){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();}
        if(FParse::Param(FCommandLine::Get(),TEXT("CFStairsPreview")))
        {State->Root=FPaths::ProjectSavedDir()/TEXT("StairsPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits);IFileManager::Get().MakeDirectory(*State->Root,true);UE_LOG(LogTemp,Display,TEXT("CF_STAIRS_PREVIEW_ROOT %s"),*State->Root);}
        if(!State->Init()){State->Finish(false,TEXT("construction"));return;}
    }
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float){return State->Tick();}));
}
