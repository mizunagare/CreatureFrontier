#include "ExpeditionScenario.h"
#include "CFPlayerCharacter.h"
#include "InventoryComponent.h"
#include "CraftingCatalog.h"
#include "ResearchComponent.h"
#include "HomeBuildingComponent.h"
#include "HomeModule.h"
#include "HomeLayout.h"
#include "HomeScenario.h"
#include "BaseBuildingComponent.h"
#include "CaptureComponent.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "PlayerEquipmentComponent.h"
#include "ResourceNode.h"
#include "ResourceDefinition.h"
#include "WildEncounter.h"
#include "WorkStation.h"
#include "FrontierSaveGame.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/BoxComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
AResourceNode* Crystal(UWorld* World)
{ for(TActorIterator<AResourceNode> It(World);It;++It) if(It->PersistentId==FGuid(0xCF170000,0,0,7)) return *It; return nullptr; }
AHomeModule* Lantern(UWorld* World)
{ for(TActorIterator<AHomeModule> It(World);It;++It) if(!It->bPreview && It->GetKind()==EHomeModuleKind::Lantern) return *It; return nullptr; }
int32 NodeCount(UWorld* World) { int32 Count=0; for(TActorIterator<AResourceNode> It(World);It;++It) ++Count; return Count; }
bool SameBag(ACFPlayerCharacter* Hero,const TMap<EFrontierItem,int32>& Bag)
{ for(int32 Item=0;Item<static_cast<int32>(EFrontierItem::Count);++Item) if(Hero->Inventory->Count(static_cast<EFrontierItem>(Item))!=Bag.FindRef(static_cast<EFrontierItem>(Item))) return false; return true; }
void Aim(ACFPlayerCharacter* Hero,const FVector& Point)
{
    const auto Rotation=(Point-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
    Hero->GetController()->SetControlRotation(Rotation); Hero->FirstPersonCamera->SetWorldRotation(Rotation);
}
bool Plan(UWorld* World,ACFPlayerCharacter* Hero,const FVector& Destination,TArray<FVector>& Points)
{
    auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Hero->GetActorLocation(),Destination,Hero);
    if(!Path || !Path->IsValid() || Path->IsPartial()) return false;
    Points=Path->PathPoints; return !Points.IsEmpty();
}
bool Drive(ACFPlayerCharacter* Hero,UHomeMovementProbe* Input,TArray<FVector>& Points)
{
    while(!Points.IsEmpty() && FVector::Dist2D(Hero->GetActorLocation(),Points[0])<35) Points.RemoveAt(0);
    if(Points.IsEmpty()) { Input->Direction=FVector::ZeroVector; Hero->GetCharacterMovement()->StopMovementImmediately(); return true; }
    Input->Direction=(Points[0]-Hero->GetActorLocation()).GetSafeNormal2D();
    Hero->GetController()->SetControlRotation(Input->Direction.Rotation()); return false;
}
bool BuildRoom(UWorld* World,ACFPlayerCharacter* Hero)
{
    Hero->HomeBuilding->Selected=EHomeModuleKind::Floor;
    auto* Floor=Hero->HomeBuilding->BuildAt(FTransform(FVector(0,-800,-40))); if(!Floor) return false;
    const FGuid Id=Floor->PersistentId;
    struct FPart { EHomeModuleKind Kind; FVector Position; double Yaw; };
    const FPart Parts[]={{EHomeModuleKind::Doorway,FVector(0,-1000,-30),0},{EHomeModuleKind::Wall,FVector(0,-600,-30),0},
        {EHomeModuleKind::Wall,FVector(-200,-800,-30),90},{EHomeModuleKind::Wall,FVector(200,-800,-30),90},{EHomeModuleKind::Roof,FVector(0,-800,280),0}};
    for(const auto& Part:Parts) { Hero->HomeBuilding->Selected=Part.Kind; if(!Hero->HomeBuilding->BuildAt(FTransform(FRotator(0,Part.Yaw,0),Part.Position),Id)) return false; }
    Hero->Building->Selected=EWorkSpecialty::Gathering;
    return Hero->Building->BuildAt(FTransform(FRotator::ZeroRotator,FVector(0,-800,5),FVector(2,1,.7)))!=nullptr;
}
bool CheckSnapshot(UWorld* World,ACFPlayerCharacter* Hero,const FString& Slot)
{
    auto* Saved=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0));
    auto* Light=Lantern(World); auto* Node=Crystal(World);
    if(!Saved || !Light || !Node || !Light->LanternLight->IsVisible() || Light->PaidExtraItems.FindRef(EFrontierItem::Crystal)!=2 ||
        Node->Remaining!=4 || NodeCount(World)!=11 || !Hero->Research->Has(EFrontierResearch::CrystalStudy) || !SameBag(Hero,Saved->InventoryStacks)) return false;
    const auto Layout=HomeLayout::Collect(World); if(Layout.Num()!=7 || Saved->HomeModules.Num()!=7) return false;
    for(const auto& Record:Saved->HomeModules)
        if(!Layout.ContainsByPredicate([&Record](const FHomeModuleRecord& Live){return Live.Id==Record.Id && Live.FloorId==Record.FloorId && Live.Transform.Equals(Record.Transform,.001) && Live.PaidMaterials==Record.PaidMaterials && Live.PaidExtraItems.OrderIndependentCompareEqual(Record.PaidExtraItems);})) return false;
    return true;
}
bool FinishProgress(UWorld* World,ACFPlayerCharacter* Hero,FGuid FloorId)
{
    bool Pass=Hero->Research->Has(EFrontierResearch::CrystalSample) && !Hero->Research->Has(EFrontierResearch::CrystalStudy);
    // 上文已经实际行走返程；事务检查使用附近无遮挡的工作台位置。
    Hero->SetActorLocation(FVector(-450,0,40)); Hero->GetCharacterMovement()->DisableMovement();
    auto Before=Hero->Inventory->GetStacks();
    Pass &= Hero->CraftRecipe(TEXT("StudyCrystal")) && Hero->Research->Has(EFrontierResearch::CrystalStudy) &&
        Hero->Inventory->Count(EFrontierItem::Crystal)==3 && Hero->Inventory->Count(EFrontierItem::Material)==Before.FindRef(EFrontierItem::Material)-2;
    Before=Hero->Inventory->GetStacks(); Pass &= !Hero->CraftRecipe(TEXT("StudyCrystal")) && SameBag(Hero,Before);
    auto Full=Before; Full.FindOrAdd(EFrontierItem::ResonanceDevice)=999; Hero->Inventory->Restore(Full);
    Pass &= !Hero->CraftRecipe(TEXT("ResonanceDevice")) && SameBag(Hero,Full); Hero->Inventory->Restore(Before);
    Pass &= Hero->CraftRecipe(TEXT("ResonanceDevice")) && Hero->Inventory->Count(EFrontierItem::ResonanceDevice)==1 && Hero->Inventory->Count(EFrontierItem::Crystal)==2;
    Hero->SetActorLocation(FVector(-350,-750,40)); Hero->HomeBuilding->Selected=EHomeModuleKind::Lantern;
    const FTransform Position(FVector(100,-900,-30));
    auto* Light=Hero->HomeBuilding->BuildAt(Position,FloorId);
    Pass &= Light && Light->LanternLight->IsVisible() && Hero->Inventory->Count(EFrontierItem::Crystal)==0;
    if(!Light) return false;
    const FString Slot=TEXT("FrontierExpeditionAutomation");
    Pass &= Hero->SaveProgress(Slot) && Hero->LoadProgress(Slot) && CheckSnapshot(World,Hero,Slot) && Hero->LoadProgress(Slot) && CheckSnapshot(World,Hero,Slot);
    Light=Lantern(World); Before=Hero->Inventory->GetStacks(); Full=Before; Full.FindOrAdd(EFrontierItem::Crystal)=999; Hero->Inventory->Restore(Full);
    Pass &= !Hero->HomeBuilding->Dismantle(Light) && IsValid(Light) && SameBag(Hero,Full);
    Hero->Inventory->Restore(Before);
    Pass &= Hero->HomeBuilding->Dismantle(Light) && Hero->Inventory->Count(EFrontierItem::Material)==Before.FindRef(EFrontierItem::Material)+2 && Hero->Inventory->Count(EFrontierItem::Crystal)==1;
    Pass &= !Hero->HomeBuilding->Dismantle(Light) && Hero->LoadProgress(Slot) && CheckSnapshot(World,Hero,Slot);
    for(int32 Case=0;Case<4;++Case)
    {
        auto* Bad=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0)); auto* Live=Lantern(World);
        if(Case==0) Bad->Research.bCrystalSample=false;
        if(Case==1) { Bad->Research=FResearchProgress(); }
        if(Case==2) Bad->InventoryStacks.Add(static_cast<EFrontierItem>(250),1);
        if(Case==3) for(auto& Record:Bad->HomeModules) if(Record.Definition.Get() && Record.Definition.Get()->Kind==EHomeModuleKind::Lantern) Record.PaidExtraItems.Add(EFrontierItem::Crystal,1000);
        UGameplayStatics::SaveGameToSlot(Bad,TEXT("FrontierExpeditionInvalid"),0);
        Pass &= !Hero->LoadProgress(TEXT("FrontierExpeditionInvalid")) && IsValid(Live) && CheckSnapshot(World,Hero,Slot);
    }
    auto* Legacy=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0)); Legacy->FormatVersion=18;
    Legacy->InventoryStacks.Remove(EFrontierItem::Crystal); Legacy->InventoryStacks.Remove(EFrontierItem::ResonanceDevice);
    Legacy->HomeModules.RemoveAll([](const FHomeModuleRecord& Record){return Record.Definition.Get() && Record.Definition.Get()->Kind==EHomeModuleKind::Lantern;});
    Legacy->ResourceNodes.RemoveAll([](const FResourceSaveRecord& Record){return Record.Id==FGuid(0xCF170000,0,0,7);});
    for(auto& Record:Legacy->ResourceNodes) if(Record.Id==FGuid(0xCF170000,0,0,1)) { Record.Remaining=0; Record.RegrowRemaining=123; Record.HarvestCooldown=.5f; }
    UGameplayStatics::SaveGameToSlot(Legacy,TEXT("FrontierExpeditionLegacy"),0);
    for(int32 Repeat=0;Repeat<2;++Repeat)
    {
        Pass &= Hero->LoadProgress(TEXT("FrontierExpeditionLegacy")) && NodeCount(World)==11 && Crystal(World)->Remaining==8 && !Hero->Research->Has(EFrontierResearch::CrystalSample) && !Lantern(World) && SameBag(Hero,Legacy->InventoryStacks);
        for(TActorIterator<AResourceNode> It(World);It;++It) if(It->PersistentId==FGuid(0xCF170000,0,0,1)) Pass &= It->Remaining==0 && It->RegrowRemaining==123 && It->HarvestCooldown==.5f;
    }
    Pass &= Hero->LoadProgress(Slot) && CheckSnapshot(World,Hero,Slot) && !Hero->CaptureComponent->UsesResonance();
    return Pass;
}
}

void RunExpeditionScenario(UWorld* World,ACFPlayerCharacter* Hero,int32 Mode)
{
    const FString Cross=TEXT("FrontierExpeditionCrossProcess");
    if(Mode==2)
    {
        const bool Pass=Hero->LoadProgress(Cross) && CheckSnapshot(World,Hero,Cross) && Hero->Inventory->Count(EFrontierItem::ResonanceDevice)==1 && Hero->LoadProgress(Cross) && CheckSnapshot(World,Hero,Cross);
        UE_LOG(LogTemp,Display,TEXT("CF_EXPEDITION_READ %s"),Pass?TEXT("PASS"):TEXT("FAIL")); FPlatformMisc::RequestExitWithStatus(false,Pass?0:1); return;
    }
    UCreatureDefinition* Leaf=nullptr; AWildEncounter* Camp=nullptr;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It) { if(It->Definition && It->Definition->Form==ECreatureForm::Leaf) Leaf=It->Definition; It->Destroy(); }
    for(TActorIterator<AWildEncounter> It(World);It;++It) if(It->EncounterId==TEXT("CrystalRise")) Camp=*It;
    auto* Catalog=UCraftingCatalog::Load(); auto* Node=Crystal(World);
    if(!Leaf || !Camp || !Node || !Catalog || !Catalog->Validate()) { UE_LOG(LogTemp,Error,TEXT("CF_EXPEDITION FAIL fixture/catalog")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Hero->SetActorLocation(FVector(-350,-1050,40)); Hero->GetCharacterMovement()->DisableMovement();
    Hero->Inventory->Restore({{EFrontierItem::Material,100},{EFrontierItem::CaptureDevice,2},{EFrontierItem::Medicine,2}}); Hero->Equipment->Restore({}); Hero->Research->Restore({});
    bool Setup=BuildRoom(World,Hero); FGuid FloorId;
    for(const auto& Record:HomeLayout::Collect(World)) if(Record.Definition.Get()->Kind==EHomeModuleKind::Floor) FloorId=Record.Id;
    Hero->SetActorLocation(FVector(-450,0,40)); Hero->CraftSpear(); Hero->CraftVest(); Setup &= Hero->Equipment->GetState().bOwnVest;
    auto Bag=Hero->Inventory->GetStacks(); Bag.FindOrAdd(EFrontierItem::Crystal)=4; Hero->Inventory->Restore(Bag);
    Setup &= !Hero->CraftRecipe(TEXT("StudyCrystal")) && SameBag(Hero,Bag);
    Hero->SetActorLocation(FVector(-350,-750,40)); Hero->HomeBuilding->Selected=EHomeModuleKind::Lantern;
    Setup &= !Hero->HomeBuilding->BuildAt(FTransform(FVector(100,-900,-30)),FloorId) && SameBag(Hero,Bag);
    Bag.Remove(EFrontierItem::Crystal); Hero->Inventory->Restore(Bag); Hero->SetActorLocation(FVector(-450,-250,40));
    Camp->RestorePopulation(0); Camp->Tick(0); auto* Boss=Camp->GetResident();
    const FTransform Transform(FVector(-650,-600,40));
    auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    Pet->Definition=Leaf; Pet->Level=4; Pet->FinishSpawning(Transform); Pet->Tame(Hero->GetController()); Pet->SetActivity(ECreatureActivity::Follow);
    Setup &= Boss && Boss->bAlpha && Boss->GetDistanceTo(Node)<500 && Node->Remaining==8;
    if(!Setup) { UE_LOG(LogTemp,Error,TEXT("CF_EXPEDITION FAIL setup %s"),*Hero->Feedback); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Hero->GetCharacterMovement()->MaxWalkSpeed=300; Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    auto* Input=NewObject<UHomeMovementProbe>(Hero); Input->RegisterComponent();
    const bool Preview=FParse::Param(FCommandLine::Get(),TEXT("CFExpeditionPreview"));
    TSharedRef<FTimerHandle> Poll=MakeShared<FTimerHandle>();
    World->GetTimerManager().SetTimer(*Poll,[World,Hero,Pet,Boss,Node,FloorId,Input,Preview,Mode,Cross,Poll,Phase=0,Started=World->GetTimeSeconds(),Points=TArray<FVector>(),Pass=true,Ordered=false,Target=static_cast<ACreatureCharacter*>(nullptr),Blocker=static_cast<AActor*>(nullptr),NormalChance=0.f]() mutable
    {
        const float Elapsed=World->GetTimeSeconds()-Started;
        auto Advance=[&](int32 Next){Phase=Next;Started=World->GetTimeSeconds();};
        if(Phase==0 && Elapsed>=2) { Pass &= Plan(World,Hero,Node->GetActorLocation()+FVector(-150,0,40),Points); Advance(1); }
        if(Phase==1)
        {
            if(!Ordered && Hero->GetDistanceTo(Boss)<900) { Ordered=Pet->OrderAttack(Boss); if(Ordered) Pet->RequestSpecial(Boss); }
            if(!Drive(Hero,Input,Points) && Elapsed<14 && Hero->Health>0) return;
            Pass &= FVector::Dist2D(Hero->GetActorLocation(),Node->GetActorLocation())<220 && Hero->Health>0; Advance(2); return;
        }
        if(Phase==2)
        {
            Aim(Hero,Node->GetActorLocation()); if(Node->HarvestCooldown<=0 && Hero->Inventory->Count(EFrontierItem::Crystal)<4) Hero->GatherAimedResource();
            if(Hero->Inventory->Count(EFrontierItem::Crystal)<4 && Elapsed<8 && Hero->Health>0) return;
            const auto Before=Hero->Inventory->GetStacks();
            Pass &= Hero->Inventory->Count(EFrontierItem::Crystal)==4 && Node->Remaining==4 && Hero->Research->Has(EFrontierResearch::CrystalSample) && !Hero->CraftRecipe(TEXT("StudyCrystal")) && SameBag(Hero,Before);
            Pass &= Ordered && Boss->Health<Boss->GetMaxHealth() && (Hero->Health<100 || Pet->Health<Pet->GetMaxHealth()) && Pet->Health>0 && Hero->Health>0;
            Hero->CommandFollow(); Pass &= Plan(World,Hero,FVector(-450,-250,40),Points); Advance(3); return;
        }
        if(Phase==3)
        {
            if(!Drive(Hero,Input,Points) && Elapsed<14 && Hero->Health>0) return;
            Input->Direction=FVector::ZeroVector;
            Pass &= FVector::Dist2D(Hero->GetActorLocation(),FVector(-450,-250,40))<100 && Hero->Health>0 && FinishProgress(World,Hero,FloorId);
            UE_LOG(LogTemp,Display,TEXT("CF_EXPEDITION progress=%d ordered=%d"),Pass,Ordered);
            if(Mode==1)
            {
                const bool Written=Pass && Hero->SaveProgress(Cross) && CheckSnapshot(World,Hero,Cross);
                UE_LOG(LogTemp,Display,TEXT("CF_EXPEDITION_WRITE %s"),Written?TEXT("PASS"):TEXT("FAIL")); FTimerHandle Active=*Poll;
                FPlatformMisc::RequestExitWithStatus(false,Written?0:1); World->GetTimerManager().ClearTimer(Active); return;
            }
            // 读取快照会替换生物；此后绝不能继续持有旧实体指针。
            Hero->GetCharacterMovement()->DisableMovement(); Hero->SetActorLocation(FVector(-450,-250,40));
            for(TActorIterator<ACreatureCharacter> It(World);It;++It) if(It->Tamer==Hero->GetController()) { It->SetActivity(ECreatureActivity::Rest); Target=*It; break; }
            Hero->CaptureComponent->ToggleDevice(); Hero->CaptureComponent->TryCapture(Target);
            Pass &= Hero->Inventory->Count(EFrontierItem::ResonanceDevice)==1; Advance(4); return;
        }
        if(Phase==4 && Elapsed>=.9f)
        {
            Target=World->SpawnActor<ACreatureCharacter>(FVector(-250,-250,40),FRotator::ZeroRotator); Target->SetActorTickEnabled(false); Target->GetCharacterMovement()->DisableMovement();
            Hero->CaptureComponent->ResetSelection(); NormalChance=Hero->CaptureComponent->CalculateCaptureChance(Target); Hero->CaptureComponent->ToggleDevice();
            Pass &= FMath::IsNearlyEqual(Hero->CaptureComponent->CalculateCaptureChance(Target),FMath::Min(.95f,NormalChance*1.5f),.001f);
            Hero->CaptureComponent->CapturePower=100; Pass &= Hero->CaptureComponent->CalculateCaptureChance(Target)==.95f; Hero->CaptureComponent->CapturePower=1;
            Blocker=World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Blocker); Blocker->SetRootComponent(Box);
            Box->SetBoxExtent(FVector(20,100,150)); Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Box->SetCollisionResponseToAllChannels(ECR_Ignore); Box->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block); Box->SetCanEverAffectNavigation(false); Box->RegisterComponent(); Blocker->SetActorLocation(FVector(-350,-250,60));
            Hero->CaptureComponent->TryCapture(Target); Pass &= Hero->Inventory->Count(EFrontierItem::ResonanceDevice)==1; Blocker->Destroy(); Advance(5); return;
        }
        if(Phase==5 && Elapsed>=.9f)
        {
            const float Chance=Hero->CaptureComponent->CalculateCaptureChance(Target); int32 Seed=1;
            for(;Seed<1000;++Seed) { FMath::RandInit(Seed); if(FMath::FRand()<Chance) break; } FMath::RandInit(Seed);
            Hero->CaptureComponent->TryCapture(Target); Pass &= Target->Tamer==Hero->GetController() && Hero->Inventory->Count(EFrontierItem::ResonanceDevice)==0 && Hero->Inventory->Count(EFrontierItem::CaptureDevice)==2; Advance(6); return;
        }
        if(Phase==6 && Elapsed>=.9f)
        {
            Target->Destroy(); Target=World->SpawnActor<ACreatureCharacter>(FVector(-250,-250,40),FRotator::ZeroRotator); Target->SetActorTickEnabled(false);
            Hero->CaptureComponent->TryCapture(Target); Pass &= !Target->Tamer && Hero->Inventory->Count(EFrontierItem::CaptureDevice)==2 && Hero->Inventory->Count(EFrontierItem::ResonanceDevice)==0;
            Target->Destroy(); UE_LOG(LogTemp,Display,TEXT("CF_EXPEDITION %s expedition_research_lantern_capture=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Pass);
            FTimerHandle Active=*Poll;
            if(Preview && Pass)
            {
                Hero->FirstPersonCamera->bUsePawnControlRotation=false; Hero->FirstPersonCamera->SetAbsolute(true,true,false); Hero->FirstPersonCamera->SetWorldLocation(FVector(-150,-900,150)); Hero->FirstPersonCamera->SetWorldRotation((FVector(70,-800,60)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());
                auto* Light=Lantern(World); Light->LanternLight->SetVisibility(false); FTimerHandle Dark,On,Lit,Menu,Done;
                UE_LOG(LogTemp,Display,TEXT("CF_LANTERN registered=%d affects_world=%d lumens=%.0f location=%s"),Light->LanternLight->IsRegistered(),Light->LanternLight->bAffectsWorld,Light->LanternLight->Intensity,*Light->LanternLight->GetComponentLocation().ToString());
                World->GetTimerManager().SetTimer(Dark,[](){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("ExpeditionDark.png"),false,false);},2.f,false);
                World->GetTimerManager().SetTimer(On,[Light](){Light->LanternLight->SetVisibility(true);},3.f,false);
                World->GetTimerManager().SetTimer(Lit,[](){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("ExpeditionLit.png"),false,false);},5.f,false);
                World->GetTimerManager().SetTimer(Menu,[Hero](){Hero->ToggleBackpack(); for(int32 Step=0;Step<5;++Step) Hero->StepBackpackRecipe(1); FTimerHandle Shot; Hero->GetWorldTimerManager().SetTimer(Shot,[](){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("ExpeditionRecipes.png"),false,false);},.5f,false);},6.f,false);
                World->GetTimerManager().SetTimer(Done,[](){FPlatformMisc::RequestExitWithStatus(false,0);},8.f,false);
            }
            else FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
            World->GetTimerManager().ClearTimer(Active);
        }
    },.05f,true);
}
