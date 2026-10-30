#include "ConstructionScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "Components/CapsuleComponent.h"
#include "CompanionConstructionComponent.h"
#include "CompanionCargoComponent.h"
#include "CompanionClearance.h"
#include "HomeBuildingComponent.h"
#include "HomeModule.h"
#include "HomeLayout.h"
#include "HomeScenario.h"
#include "InventoryComponent.h"
#include "FrontierSaveGame.h"
#include "ResourceNode.h"
#include "ResourceDefinition.h"
#include "WildEncounter.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputKeyEventArgs.h"
#include "InputRoutingQA.h"
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
bool VerifyYieldStepDown(ACreatureCharacter* Pet,AHomeModule* Box)
{
    // 只读几何单元：使用现有实体地板和箱子，不改角色位置、碰撞或移动规则。
    FCollisionQueryParams Query(SCENE_QUERY_STAT(YieldStepFixture),false,Pet);FHitResult Hit;bool Step=false;
    const FVector Start(-930,-700,60.15),End(-1120,-700,40.15);
    const bool Down=FrontierFollowPath::ClearYieldSegment(Pet,Start,End,Query,Hit,Step) && Step;
    const bool High=!FrontierFollowPath::ClearYieldSegment(Pet,Start,FVector(End.X,End.Y,Start.Z-Pet->GetCharacterMovement()->MaxStepHeight-1),Query,Hit,Step) && !Step;
    const bool BoxClear=FrontierFollowPath::ClearYieldSegment(Pet,FVector(-900,-780,60.15),FVector(-900,-1050,40.15),Query,Hit,Step);
    const bool Blocked=!BoxClear && !Step && Hit.GetActor()==Box;
    if(!Blocked)UE_LOG(LogTemp,Display,TEXT("CF_OWNER_YIELD_BOX_DIAGNOSTIC clear=%d step=%d actor=%s expected=%s"),BoxClear,Step,*GetNameSafe(Hit.GetActor()),*GetNameSafe(Box));
    const bool Penetrating=!FrontierFollowPath::ClearYieldSegment(Pet,FVector(-900,-900,60.15),FVector(-1120,-900,40.15),Query,Hit,Step) && !Step && Hit.bStartPenetrating;
    UE_LOG(LogTemp,Display,TEXT("CF_OWNER_YIELD_CLEARANCE %s stepDown=%d highDrop=%d box=%d penetrating=%d"),Down && High && Blocked && Penetrating?TEXT("PASS"):TEXT("FAIL"),Down,High,Blocked,Penetrating);
    return Down && High && Blocked && Penetrating;
}
struct FRenovationTest
{
    UWorld* World;ACFPlayerCharacter* Hero;TWeakObjectPtr<ACreatureCharacter> Pet;TWeakObjectPtr<AHomeModule> Site,Box;
    FGuid PetId,SiteId,BoxId;TArray<FHomeModuleRecord> Original;int32 Phase=0,Harvested=0,Point=0;bool Mid=false,Preview=false;
    double Deadline=0,At=0,PetTravel=0,HeroTravel=0,NextShot=0,NextSample=0;FVector PreviousPet,PreviousHero;TWeakObjectPtr<UHomeMovementProbe> Input;
    FString ShotRoot;TSet<FString> Shots;TArray<FVector> Route;bool Entered=false,PetEntered=false,Exited=false,PetExited=false,QueuePrompted=false,BlockedProbe=false;
    FVector ProgressAnchor;double ProgressAt=0;
    void Face(FVector Target){const auto Aim=(Target-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(Aim);Hero->FirstPersonCamera->SetWorldRotation(Aim);}
    bool Bind()
    {
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController()){Pet=*It;PetId=It->PersistentId;}
        for(TActorIterator<AHomeModule> It(World);It;++It){if(It->GetActorLocation().Equals(FVector(-1000,-800,-30),.01)){Site=*It;SiteId=It->PersistentId;}if(It->GetKind()==EHomeModuleKind::Storage){Box=*It;BoxId=It->PersistentId;}}
        if(!Pet.IsValid() || !Site.IsValid() || !Box.IsValid())return false;
        for(TActorIterator<AWildEncounter> It(World);It;++It)It->SetActorTickEnabled(false);
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(*It!=Pet.Get()){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();}
        PreviousPet=Pet->GetActorLocation();PreviousHero=Hero->GetActorLocation();return true;
    }
    bool Finish(bool Pass,const TCHAR* Reason)
    {
        if(Input.IsValid())Input->Direction=FVector::ZeroVector;
        UE_LOG(LogTemp,Display,TEXT("CF_RENOVATION %s reason=%s phase=%d petTravel=%.1f heroTravel=%.1f material=%d food=%d seed=%d pet=%s site=%s hero=%s partner=%s feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Reason,Phase,PetTravel,HeroTravel,Hero->Inventory->Count(EFrontierItem::Material),Hero->Inventory->Count(EFrontierItem::Food),Hero->Inventory->Count(EFrontierItem::Seeds),*PetId.ToString(),*SiteId.ToString(),*Hero->GetActorLocation().ToString(),Pet.IsValid()?*Pet->GetActorLocation().ToString():TEXT("无"),*Hero->Feedback);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    void Observer(FVector Position){Hero->SetActorLocation(Position);PreviousHero=Hero->GetActorLocation();}
    bool Photo(const TCHAR* Scene)
    {
        if(!Preview || Shots.Contains(Scene))return false;Shots.Add(Scene);Face(Site->GetActorLocation()+FVector(0,-150,150));
        FScreenshotRequest::RequestScreenshot(ShotRoot/(FString(Scene)+TEXT(".png")),false,false);NextShot=FPlatformTime::Seconds()+.2;return true;
    }
    bool BeginQueue()
    {
        Hero->HomeBuilding->Selected=EHomeModuleKind::Doorway;Observer(FVector(-1250,-800,60));Face(Site->GetActorLocation()+FVector(0,-150,150));
        if(!CFInputQA::Press(Hero,EKeys::B) || !CFInputQA::Press(Hero,EKeys::Eight))return false;
        auto* PC=CastChecked<APlayerController>(Hero->GetController());
        PC->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::RightShift,IE_Pressed,1,false,FPlatformTime::Cycles64()));
        QueuePrompted=false;NextShot=FPlatformTime::Seconds()+.1;return true;
    }
    int32 FinishQueue()
    {
        auto* PC=CastChecked<APlayerController>(Hero->GetController());
        if(!PC->IsInputKeyDown(EKeys::RightShift) || !Site.IsValid())return -1;
        if(!QueuePrompted)
        {
            if(!CFInputQA::Press(Hero,EKeys::Insert) || !Site.IsValid() || Site->bUnderConstruction || !Hero->Feedback.Contains(TEXT("伙伴改建")))return -1;
            QueuePrompted=true;if(Photo(TEXT("Planning")))return 0;
        }
        const bool Confirm=CFInputQA::Press(Hero,EKeys::Insert) && Site.IsValid() && Site->bUnderConstruction && Site->ConstructionOriginalDefinition && Site->PaidMaterials==10;
        PC->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::RightShift,IE_Released,0,false,FPlatformTime::Cycles64()));CFInputQA::Press(Hero,EKeys::B);
        FString Reason;return Confirm && HomeLayout::Validate(HomeLayout::Collect(World),Reason) && Hero->Inventory->Count(EFrontierItem::Material)==8?1:-1;
    }
    bool Cancellation()
    {
        if(!Hero->Inventory->TransferTo(Site->StorageInventory,{{EFrontierItem::Material,3}}))return false;
        const auto Bag=Hero->Inventory->GetStacks();Hero->Inventory->AddItem(EFrontierItem::Material,994);
        const bool Kept=!Hero->HomeBuilding->Dismantle(Site.Get()) && Site->bUnderConstruction && Site->StorageInventory->Count(EFrontierItem::Material)==3;
        Hero->Inventory->Restore(Bag);
        if(!Kept || !Hero->HomeBuilding->Dismantle(Site.Get()) || Site->GetKind()!=EHomeModuleKind::Window || Site->PaidMaterials!=10 || Site->ConstructionOriginalDefinition ||
            Hero->Inventory->Count(EFrontierItem::Material)!=8 || !Hero->SaveProgress(TEXT("RenovationCancel")) || !Hero->LoadProgress(TEXT("RenovationCancel")) || !Bind() || Site->GetKind()!=EHomeModuleKind::Window)return false;
        return true;
    }
    bool FailurePaths()
    {
        if(!Hero->SaveProgress(TEXT("RenovationBad")))return false;
        for(int32 Case=0;Case<7;++Case)
        {
            auto* Bad=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("RenovationBad"),0));if(!Bad)return false;
            auto* R=Bad->HomeModules.FindByPredicate([this](const FHomeModuleRecord& Value){return Value.Id==SiteId;});if(!R)return false;
            if(Case==0)Bad->FormatVersion=35;
            if(Case==1)R->ConstructionOriginalDefinition=UHomeModuleDefinition::LoadDefault(EHomeModuleKind::Storage);
            if(Case==2)R->ConstructionOriginalDefinition=R->Definition;
            if(Case==3)R->bUnderConstruction=false;
            if(Case==4)R->PaidMaterials=0;
            if(Case==5)R->DoorState=1;
            if(Case==6)R->Definition=UHomeModuleDefinition::LoadDefault(EHomeModuleKind::Fence);
            if(!UGameplayStatics::SaveGameToSlot(Bad,TEXT("RenovationCancel"),0) || Hero->LoadProgress(TEXT("RenovationCancel")) || !Site.IsValid() || !Site->bUnderConstruction || Site->PaidMaterials!=10 || Hero->Inventory->Count(EFrontierItem::Material)!=8)return false;
        }
        UE_LOG(LogTemp,Display,TEXT("CF_RENOVATION_FAILURES PASS cases=7 cancel=1 fullBagRetained=1 oldCost=10 inventory=8 support=1"));return true;
    }
    bool Setup()
    {
        if(!FailurePaths())return false;
        for(TActorIterator<AHomeModule> It(World);It;++It)if(It->GetKind()==EHomeModuleKind::Door && It->GetActorLocation().Equals(FVector(-400,-1000,-30),.01) && It->DoorState==1)
        {Observer(FVector(-400,-1250,60));Face(It->GetActorLocation()+FVector(0,0,125));if(!CFInputQA::Press(Hero,EKeys::E) || It->DoorState!=2)return false;}
        Observer(FVector(-750,-850,60));
        if(!Hero->Inventory->TransferTo(Box->StorageInventory,{{EFrontierItem::Material,8}}))return false;
        Face(Box->GetActorLocation()+FVector(0,0,40));if(!CFInputQA::Press(Hero,EKeys::V))return false;
        Face(Site->GetActorLocation()+FVector(0,-150,150));if(!CFInputQA::Press(Hero,EKeys::E) || Pet->Construction->GetSite()!=Site.Get())return false;
        Observer(FVector(-1250,-800,60));Face(Site->GetActorLocation()+FVector(0,0,150));
        UE_LOG(LogTemp,Display,TEXT("CF_RENOVATION_QUEUED PASS modules=19 paid=10 supplied=0 stock=8 material=0 support=1 mapped=1"));return true;
    }
    bool Tick()
    {
        if(FPlatformTime::Seconds()<NextShot)return true;
        if(!Pet.IsValid() || !Site.IsValid() || !Box.IsValid() || FPlatformTime::Seconds()>Deadline)return Finish(false,TEXT("lifetime"));
        const auto P=Pet->GetActorLocation(),H=Hero->GetActorLocation();const double Step=FVector::Dist(P,PreviousPet);PetTravel+=Step;
        if(Phase==3 || Phase==5)
        {
            Entered|=PreviousHero.X<-1000 && H.X>=-1000 && FMath::Abs(H.Y+800)<100;
            PetEntered|=PreviousPet.X<-1000 && P.X>=-1000 && FMath::Abs(P.Y+800)<100;
            Exited|=Phase==5 && PreviousHero.X>-1000 && H.X<=-1000 && FMath::Abs(H.Y+800)<100;
            PetExited|=Phase==5 && PreviousPet.X>-1000 && P.X<=-1000 && FMath::Abs(P.Y+800)<100;
        }
        PreviousPet=P;
        if(Phase>=2 && Phase<=5){const double HeroStep=FVector::Dist(H,PreviousHero);HeroTravel+=HeroStep;if(HeroStep>100)return Finish(false,TEXT("hero-continuity"));}PreviousHero=H;
        if(Phase>=2 && Phase<=5 && World->GetTimeSeconds()>=NextSample)
        {NextSample=World->GetTimeSeconds()+2;UE_LOG(LogTemp,Display,TEXT("CF_RENOVATION_SAMPLE phase=%d point=%d entered=%d petEntered=%d exited=%d petExited=%d hero=%s pet=%s"),Phase,Point,Entered,PetEntered,Exited,PetExited,*H.ToString(),*P.ToString());}
        if(Step>120 || !Pet->IsActorTickEnabled() || !Pet->GetActorEnableCollision() || Pet->GetCharacterMovement()->MovementMode==MOVE_None || Hero->Health<=0 || Pet->Health<=0)return Finish(false,TEXT("continuity"));
        if(Phase==0)
        {
            if(Harvested==6){if(!BeginQueue())return Finish(false,TEXT("begin-queue"));Phase=10;return true;}
            for(TActorIterator<AResourceNode> It(World);It;++It)if(It->Definition && It->Definition->OutputItem==EFrontierItem::Material && It->Remaining>0 && It->HarvestCooldown<=0)
            {
                Observer(It->GetActorLocation()+FVector(0,-180,60));
                if(It->TryHarvest(Hero,Hero)){++Harvested;UE_LOG(LogTemp,Display,TEXT("CF_RENOVATION_HARVEST count=%d material=%d source=%s"),Harvested,Hero->Inventory->Count(EFrontierItem::Material),*It->PersistentId.ToString());return true;}
            }
            return true;
        }
        if(Phase==10 || Phase==11)
        {
            const int32 Queued=FinishQueue();if(Queued<0)return Finish(false,TEXT("queue-input"));if(!Queued)return true;
            if(Phase==10){if(!Cancellation() || !BeginQueue())return Finish(false,TEXT("cancel-queue"));Phase=11;return true;}
            if(!Setup())return Finish(false,TEXT("setup"));Phase=1;return true;
        }
        const int32 Total=Hero->Inventory->Count(EFrontierItem::Material)+Box->StorageInventory->Count(EFrontierItem::Material)+Site->StorageInventory->Count(EFrontierItem::Material)+Pet->Cargo->Count();
        if(Total!=(Site->bUnderConstruction?8:5))return Finish(false,TEXT("conservation"));
        if(Phase==1)
        {
            if(Photo(TEXT("Retained")))return true;
            if(!Mid && Pet->Cargo->Count()==3){if(!Hero->SaveProgress(TEXT("RenovationMid")))return Finish(false,TEXT("mid-save"));Mid=true;UE_LOG(LogTemp,Display,TEXT("CF_RENOVATION_MID PASS version=%d material=0 stock=5 carried=3 retained=1"),FrontierCurrentSaveVersion);}
            if(Site->bUnderConstruction){FString Reason;if(!Site->ConstructionOriginalDefinition || Site->PaidMaterials!=10 || !HomeLayout::Validate(HomeLayout::Collect(World),Reason))return Finish(false,TEXT("support"));return true;}
            if(!Mid || Site->GetKind()!=EHomeModuleKind::Doorway || Site->PaidMaterials!=8 || Site->ConstructionOriginalDefinition || Pet->Construction->GetSite() || Pet->Cargo->Count()!=0 || Pet->Activity!=ECreatureActivity::Follow)return Finish(false,TEXT("completion"));
            UE_LOG(LogTemp,Display,TEXT("CF_RENOVATION_COMPLETED PASS version=%d modules=19 material=5 cost=8 refund=5 sameId=1 support=1"),FrontierCurrentSaveVersion);Phase=2;At=World->GetTimeSeconds();
            if(Photo(TEXT("Completed")))return true;return true;
        }
        if(Phase==2)
        {
            if(World->GetTimeSeconds()-At>15)return Finish(false,TEXT("rejoin-new-door"));
            if(World->GetTimeSeconds()-At<1 || P.X>-1100 || Pet->GetDistanceTo(Hero)>250)return true;
            // 原农圃位于(-800,-700)，进入后沿箱子与农圃之间的真实通道继续走，不把目标放进设施碰撞边缘。
            Input=NewObject<UHomeMovementProbe>(Hero);Input->RegisterComponent();Route={FVector(-950,-800,40),FVector(-650,-800,40)};Point=0;ProgressAnchor=H;ProgressAt=World->GetTimeSeconds();Phase=3;return true;
        }
        if(Phase==3 || Phase==5)
        {
            while(Point<Route.Num() && FVector::Dist2D(H,Route[Point])<15)++Point;
            if(FVector::Dist2D(H,ProgressAnchor)>10){ProgressAnchor=H;ProgressAt=World->GetTimeSeconds();}
            if(Point<Route.Num() && World->GetTimeSeconds()-ProgressAt>5)
            {const bool Saved=Hero->SaveProgress(TEXT("RenovationBlocked"));UE_LOG(LogTemp,Display,TEXT("CF_RENOVATION_BLOCK_CAPTURE saved=%d hero=%s pet=%s"),Saved,*H.ToString(),*P.ToString());return Finish(false,TEXT("player-route-blocked"));}
            if(Point<Route.Num()){Input->Direction=(Route[Point]-H).GetSafeNormal2D();return true;}
            Input->Direction=FVector::ZeroVector;
            if(Phase==3){if(!Entered || !PetEntered || Pet->GetDistanceTo(Hero)>250)return true;Face(P+FVector(0,0,20));if(!Hero->FeedCompanion(Pet.Get()) || Hero->Inventory->Count(EFrontierItem::Food)!=0 || Pet->BondPoints!=9)return Finish(false,TEXT("feed"));Phase=4;At=World->GetTimeSeconds();return true;}
            if(!Exited || !PetExited || Pet->GetDistanceTo(Hero)>300)return true;
            const bool EnoughTravel=BlockedProbe?PetTravel>100 && HeroTravel>300:PetTravel>1000 && HeroTravel>700;
            if(!EnoughTravel || !Hero->SaveProgress(TEXT("RenovationDone")) || !Hero->LoadProgress(TEXT("RenovationDone")) || !Bind())return Finish(false,TEXT("life-save"));
            const auto Records=HomeLayout::Collect(World);if(Records.Num()!=19 || Pet->Experience!=8 || Pet->BondPoints!=9 || Hero->Inventory->Count(EFrontierItem::Seeds)!=1)return Finish(false,TEXT("life-state"));
            for(const auto& Old:Original)if(Old.Id!=SiteId){const auto* Now=Records.FindByPredicate([&](const FHomeModuleRecord& R){return R.Id==Old.Id;});if(!Now || Now->Definition!=Old.Definition || Now->FloorId!=Old.FloorId || !Now->Transform.Equals(Old.Transform,.01) || Now->PaidMaterials!=Old.PaidMaterials)return Finish(false,TEXT("retained-house"));}
            if(BlockedProbe){UE_LOG(LogTemp,Display,TEXT("CF_RENOVATION_EXIT_REPRO PASS moved=1 exited=1 petExited=1 material=5 food=0 seed=1 bond=9"));}
            else{UE_LOG(LogTemp,Display,TEXT("CF_RENOVATION_LIFE PASS entered=1 petEntered=1 exited=1 petExited=1 food=0 seed=1 bond=9 material=5 retained=18"));}return Finish(true,TEXT("complete"));
        }
        if(Phase==4){if(World->GetTimeSeconds()-At<.5)return true;Route={FVector(-950,-800,40),FVector(-1250,-800,40)};Point=0;ProgressAnchor=H;ProgressAt=World->GetTimeSeconds();Phase=5;return true;}
        return true;
    }
};
}
void RunRenovationScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto State=MakeShared<FRenovationTest>();State->World=World;State->Hero=Hero;State->Deadline=FPlatformTime::Seconds()+160;
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("RenovationInput"),0));
    if(!Source || (Read?(Source->FormatVersion<36 || Source->FormatVersion>FrontierCurrentSaveVersion):Source->FormatVersion!=34) || !Hero->LoadProgress(TEXT("RenovationInput")) || !State->Bind() || Source->HomeModules.Num()!=19 || Source->Facilities.Num()!=2 || State->PetId!=FGuid(0x1209AAFE,0x4D5C4544,0x1D65AF94,0x1B0435D1)) {State->Finish(false,TEXT("source"));return;}
    State->Original=HomeLayout::Collect(World);State->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFRenovationPreview"));
    State->ShotRoot=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("RenovationPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));
    if(State->Preview){IFileManager::Get().MakeDirectory(*State->ShotRoot,true);UE_LOG(LogTemp,Display,TEXT("CF_RENOVATION_PREVIEW_ROOT %s"),*State->ShotRoot);}
    State->BlockedProbe=FParse::Param(FCommandLine::Get(),TEXT("CFRenovationBlockedRead"));
    if(State->BlockedProbe)
    {
        if(State->Site->bUnderConstruction || State->Site->GetKind()!=EHomeModuleKind::Doorway || State->Pet->Activity!=ECreatureActivity::Follow || State->Pet->BondPoints!=9 || Hero->Inventory->Count(EFrontierItem::Material)!=5 || Hero->Inventory->Count(EFrontierItem::Food)!=0){State->Finish(false,TEXT("blocked-source"));return;}
        State->Phase=5;State->Entered=true;State->PetEntered=true;State->Input=NewObject<UHomeMovementProbe>(Hero);State->Input->RegisterComponent();State->Route={FVector(-950,-800,40),FVector(-1250,-800,40)};State->ProgressAnchor=Hero->GetActorLocation();State->ProgressAt=World->GetTimeSeconds();
        // 仅复现真实失败快照的退出段，不把已设定的进入标志计为本进程重新进入的证据。
        UE_LOG(LogTemp,Display,TEXT("CF_RENOVATION_BLOCK_RESTORE PASS version=%d phase=exitOnly hero=%s pet=%s"),Source->FormatVersion,*Hero->GetActorLocation().ToString(),*State->Pet->GetActorLocation().ToString());
    }
    else if(Read){if(!State->Site->ConstructionOriginalDefinition || State->Pet->Construction->GetSite()!=State->Site.Get() || State->Pet->Cargo->Count()!=3){State->Finish(false,TEXT("restore"));return;}State->Phase=1;State->Mid=true;UE_LOG(LogTemp,Display,TEXT("CF_RENOVATION_RESTORE PASS version=%d stock=5 carried=3 retained=1"),Source->FormatVersion);}
    else if(State->Site->GetKind()!=EHomeModuleKind::Window || State->Site->PaidMaterials!=10 || Hero->Inventory->Count(EFrontierItem::Material)!=2 || Hero->Inventory->Count(EFrontierItem::Food)!=1 || Hero->Inventory->Count(EFrontierItem::Seeds)!=1){State->Finish(false,TEXT("origin"));return;}
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float){return State->Tick();}));
}
void RunRenovationOccupancyScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    // 独立占位夹具：伙伴只在合法门洞中初始生成，之后不设置其位置，不关闭其碰撞／移动／Tick。
    auto End=[](bool Pass,const TCHAR* Reason){UE_LOG(LogTemp,Display,TEXT("CF_RENOVATION_OCCUPANCY %s reason=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Reason);FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;};
    Hero->SetActorLocation(FVector(-600,-1200,60));Hero->Inventory->Restore({{EFrontierItem::Material,100}});
    Hero->HomeBuilding->Selected=EHomeModuleKind::Floor;
    auto* Floor=Hero->HomeBuilding->BuildAt(FTransform(FVector(-400,-800,-40)));auto* West=Hero->HomeBuilding->BuildAt(FTransform(FVector(-800,-800,-40)));
    if(!Floor || !West){End(false,TEXT("floors"));return;}
    Hero->HomeBuilding->Selected=EHomeModuleKind::Storage;auto* Box=Hero->HomeBuilding->BuildAt(FTransform(FVector(-900,-900,-30)),West->PersistentId);
    Hero->HomeBuilding->Selected=EHomeModuleKind::Doorway;auto* Site=Hero->HomeBuilding->BuildAt(FTransform(FVector(-400,-600,-30)),Floor->PersistentId);
    Hero->SetActorLocation(FVector(-800,-850,60));Hero->HomeBuilding->Selected=EHomeModuleKind::Wall;
    if(!Box || !Site || !Hero->HomeBuilding->QueueWallReplacement(Site) || Hero->Inventory->Count(EFrontierItem::Material)!=70 || !Hero->Inventory->TransferTo(Site->StorageInventory,{{EFrontierItem::Material,6}})){End(false,TEXT("queue"));return;}
    // 仅退款容量单元显式设满背包／已完成工作量，检查后还原，不把夹具物资或进度投入实际施工。
    const auto Bag=Hero->Inventory->GetStacks();Hero->Inventory->AddItem(EFrontierItem::Material,935);Site->ConstructionWork=6;
    const bool Capacity=!Hero->HomeBuilding->CompleteConstruction(Site) && Hero->Feedback.Contains(TEXT("接收旧墙一半材料")) && Site->bUnderConstruction &&
        Site->ConstructionOriginalDefinition && Site->PaidMaterials==8 && Site->StorageInventory->Count(EFrontierItem::Material)==6 && Hero->Inventory->Count(EFrontierItem::Material)==999;
    Hero->Inventory->Restore(Bag);Site->ConstructionWork=0;if(!Capacity){End(false,TEXT("refund-capacity"));return;}
    UE_LOG(LogTemp,Display,TEXT("CF_RENOVATION_REFUND_CAPACITY PASS stock=6 oldCost=8 bag=64 retained=1"));
    for(TActorIterator<ACreatureCharacter> It(World);It;++It){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();}
    for(TActorIterator<AWildEncounter> It(World);It;++It)It->SetActorTickEnabled(false);
    const FTransform Start(FVector(-400,-600,60));auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),Start,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    Pet->Definition=LoadObject<UCreatureDefinition>(nullptr,TEXT("/Game/Creatures/DA_Mosskip.DA_Mosskip"));Pet->FinishSpawning(Start);Pet->Tame(Hero->GetController());
    FCollisionQueryParams Query(SCENE_QUERY_STAT(RenovationFixture),false,Pet);const auto* Shape=Pet->GetCapsuleComponent();
    if(World->OverlapBlockingTestByChannel(Pet->GetActorLocation(),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Shape->GetScaledCapsuleRadius()-1,Shape->GetScaledCapsuleHalfHeight()-1),Query) || !Pet->Construction->Assign(Site,Box,Hero)){End(false,TEXT("legal-spawn"));return;}
    if(!VerifyYieldStepDown(Pet,Box)){End(false,TEXT("yield-clearance"));return;}
    auto Previous=MakeShared<FVector>(Pet->GetActorLocation());auto Travel=MakeShared<double>(0);const double Deadline=FPlatformTime::Seconds()+15;const auto Id=Site->PersistentId;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([=](float)
    {
        if(!IsValid(Pet) || !IsValid(Site))return End(false,TEXT("lifetime"));
        const double Step=FVector::Dist(Pet->GetActorLocation(),*Previous);*Previous=Pet->GetActorLocation();*Travel+=Step;
        if(Step>120 || !Pet->IsActorTickEnabled() || !Pet->GetActorEnableCollision() || Pet->GetCharacterMovement()->MovementMode==MOVE_None)return End(false,TEXT("continuity"));
        if(!Site->bUnderConstruction){const bool Valid=*Travel>50 && Site->PersistentId==Id && Site->PaidMaterials==6 && !Site->ConstructionOriginalDefinition && Hero->Inventory->Count(EFrontierItem::Material)==68 && Site->StorageInventory->Count(EFrontierItem::Material)==0 && Pet->Activity==ECreatureActivity::Follow && !Hero->HomeBuilding->CompleteConstruction(Site) && Hero->Inventory->Count(EFrontierItem::Material)==68;
            UE_LOG(LogTemp,Display,TEXT("CF_RENOVATION_OCCUPANCY_DONE %s travel=%.1f material=68 paid=6 sameId=1"),Valid?TEXT("PASS"):TEXT("FAIL"),*Travel);return End(Valid,TEXT("complete"));}
        if(FPlatformTime::Seconds()>Deadline){UE_LOG(LogTemp,Display,TEXT("CF_RENOVATION_OCCUPANCY_BLOCKED stock=%d work=%.2f travel=%.1f material=%d pet=%s feedback=%s"),Site->StorageInventory->Count(EFrontierItem::Material),Site->ConstructionWork,*Travel,Hero->Inventory->Count(EFrontierItem::Material),*Pet->GetActorLocation().ToString(),*Hero->Feedback);return End(false,TEXT("deadline"));}
        return true;
    }));
}
