#include "FieldRecoveryScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "ResourceNode.h"
#include "InventoryComponent.h"
#include "ResearchComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

namespace CFFieldRecoveryQA
{
const FGuid CrystalId(0xCF170000,0,0,7);
AResourceNode* FindCrystal(UWorld* World)
{
    for(TActorIterator<AResourceNode> It(World);It;++It) if(It->PersistentId==CrystalId) return *It;
    return nullptr;
}
ACreatureCharacter* FindWorker(UWorld* World,ACFPlayerCharacter* Hero)
{
    for(TActorIterator<ACreatureCharacter> It(World);It;++It) if(It->Tamer==Hero->GetController()) return *It;
    return nullptr;
}
AActor* Wall(UWorld* World,const FVector& Position,const FVector& Extent)
{
    auto* Actor=World->SpawnActor<AActor>();
    auto* Box=NewObject<UBoxComponent>(Actor); Actor->AddInstanceComponent(Box); Actor->SetRootComponent(Box);
    Box->SetBoxExtent(Extent); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->SetCanEverAffectNavigation(true);
    Box->RegisterComponent(); Actor->SetActorLocation(Position);
    auto* Visual=NewObject<UStaticMeshComponent>(Actor); Actor->AddInstanceComponent(Visual); Visual->SetupAttachment(Box);
    Visual->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Visual->SetRelativeScale3D(Extent/50); Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision); Visual->SetCanEverAffectNavigation(false); Visual->RegisterComponent();
    return Actor;
}
bool PathOpen(UWorld* World,ACreatureCharacter* Pet,AResourceNode* Node)
{
    auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Pet->GetActorLocation(),Node->GetActorLocation(),Pet);
    return Path && Path->IsValid() && !Path->IsPartial();
}
bool InventoryAndKnowledge(ACFPlayerCharacter* Hero,int32 Crystals)
{
    for(uint8 Index=0;Index<static_cast<uint8>(EFrontierItem::Count);++Index)
    {
        const auto Item=static_cast<EFrontierItem>(Index);
        if(Hero->Inventory->Count(Item)!=(Item==EFrontierItem::Crystal?Crystals:0)) return false;
    }
    return Hero->Research->Has(EFrontierResearch::CrystalSample)==(Crystals>0);
}
void ExitCase(bool Pass,int32 Mode)
{
    const TCHAR* Label=Mode==1?TEXT("CF_FIELD_RECOVERY_WRITE"):Mode==2?TEXT("CF_FIELD_RECOVERY_READ"):TEXT("CF_FIELD_RECOVERY");
    UE_LOG(LogTemp,Display,TEXT("%s %s"),Label,Pass?TEXT("PASS"):TEXT("FAIL"));
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}
struct FRecoveryRun
{
    TWeakObjectPtr<ACreatureCharacter> Pet;
    TWeakObjectPtr<AResourceNode> Node;
    TWeakObjectPtr<AActor> Gate;
    FVector Start;
    int32 Phase=0;
    double Started=0;
    bool Pass=true, SawRetry=false, ClosedPath=false, OpenPath=false, BlockedShot=false, OwnerReleased=false;
};
}

void RunFieldRecoveryScenario(UWorld* World,ACFPlayerCharacter* Hero,int32 Mode)
{
    using namespace CFFieldRecoveryQA;
    UCreatureDefinition* Leaf=nullptr;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)
    { if(It->Definition && It->Definition->Form==ECreatureForm::Leaf) Leaf=It->Definition; It->Destroy(); }
    auto* Node=FindCrystal(World);
    if(!Leaf || !Node) { ExitCase(false,Mode); return; }
    const FVector Center=Node->GetActorLocation();
    // 每个进程重建相同的固定地形夹具，它不是玩家存档中的建筑。
    Wall(World,Center+FVector(350,0,150),FVector(20,350,200));
    Wall(World,Center+FVector(-235,-350,150),FVector(115,20,200));
    Wall(World,Center+FVector(235,-350,150),FVector(115,20,200));
    Wall(World,Center+FVector(0,350,150),FVector(350,20,200));
    Wall(World,Center+FVector(-350,0,150),FVector(20,350,200));
    auto Run=MakeShared<FRecoveryRun>();
    Hero->GetCharacterMovement()->DisableMovement();
    if(Mode==2)
    {
        Run->Pass=Hero->LoadProgress(TEXT("FieldRecoveryCross"));
        Node=FindCrystal(World); Run->Pet=FindWorker(World,Hero);
        Hero->GetCharacterMovement()->DisableMovement();
    }
    else
    {
        Node->Remaining=2; Node->HarvestCooldown=0; Node->RegrowRemaining=0; Node->Refresh();
        Hero->Inventory->Restore({{EFrontierItem::Crystal,0}});
        Hero->SetActorLocation(Center+FVector(200,-700,90));
        const FTransform Transform(Center+FVector(50,-650,40));
        auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Pet->Definition=Leaf; Pet->FinishSpawning(Transform); Pet->Tame(Hero->GetController()); Pet->SetActorTickEnabled(false);
        const FRotator Aim=(Center-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
        Hero->GetController()->SetControlRotation(Aim); Hero->FirstPersonCamera->SetWorldRotation(Aim);
        Hero->CommandWork(); Run->Pet=Pet;
        Run->Pass &= Pet->GetFieldResource()==Node && !Pet->WorkSite;
        Pet->SetActorTickEnabled(true);
    }
    Run->Node=Node;
    auto* Pet=Run->Pet.Get();
    if(!Pet || !Node) { ExitCase(false,Mode); return; }
    Run->Pass &= Pet->Activity==ECreatureActivity::Work && Pet->GetFieldResource()==Node && Node->Remaining==2 && InventoryAndKnowledge(Hero,0) && Pet->Experience==0;
    Run->Start=Pet->GetActorLocation();
    Run->Gate=Wall(World,Center+FVector(0,-350,150),FVector(120,20,200));
    if(auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)) Nav->Build();
    const bool Preview=FParse::Param(FCommandLine::Get(),TEXT("CFFieldRecoveryPreview"));
    if(Preview)
    {
        Hero->FirstPersonCamera->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform); Hero->FirstPersonCamera->bUsePawnControlRotation=false;
        const FVector Camera=Center+FVector(-650,-650,1000);
        Hero->FirstPersonCamera->SetWorldLocationAndRotation(Camera,(Center-Camera).Rotation());
    }
    const FString ImageRoot=FPaths::ProjectSavedDir()/TEXT("FieldRecovery");
    if(Preview) IFileManager::Get().MakeDirectory(*ImageRoot,true);
    Run->Started=World->GetTimeSeconds();
    TSharedRef<FTimerHandle> Poll=MakeShared<FTimerHandle>();
    World->GetTimerManager().SetTimer(*Poll,[World,Hero,Run,Poll,Mode,Preview,ImageRoot]()
    {
        auto* Worker=Run->Pet.Get(); auto* Resource=Run->Node.Get();
        const float Elapsed=World->GetTimeSeconds()-Run->Started;
        auto Advance=[&](int32 Phase){Run->Phase=Phase;Run->Started=World->GetTimeSeconds();};
        auto Fail=[&]()
        {
            UE_LOG(LogTemp,Error,TEXT("CF_FIELD_RECOVERY failure phase=%d pass=%d retry=%d closed=%d open=%d"),Run->Phase,Run->Pass,Run->SawRetry,Run->ClosedPath,Run->OpenPath);
            if(Worker) UE_LOG(LogTemp,Display,TEXT("CF_FIELD_RECOVERY worker=%s status=%s xp=%d crystals=%d"),*Worker->GetActorLocation().ToString(),*Worker->GetGatheringStatus(),Worker->Experience,Hero->Inventory->Count(EFrontierItem::Crystal));
            if(Resource) UE_LOG(LogTemp,Display,TEXT("CF_FIELD_RECOVERY node=%s stock=%d cooldown=%.2f"),*Resource->GetActorLocation().ToString(),Resource->Remaining,Resource->HarvestCooldown);
            FTimerHandle Active=*Poll; ExitCase(false,Mode); World->GetTimerManager().ClearTimer(Active);
        };
        if(!Worker || (Run->Phase<4 && !Resource)) { Fail(); return; }
        if(Run->Phase==0)
        {
            Run->Pass &= Resource->Remaining==2 && Resource->HarvestCooldown==0 && Worker->Experience==0 && InventoryAndKnowledge(Hero,0);
            Run->SawRetry |= Worker->GetGatheringStatus().Contains(TEXT("路径超时"));
            if(Elapsed>=2) Run->ClosedPath |= !PathOpen(World,Worker,Resource);
            if(Preview && Run->SawRetry && !Run->BlockedShot)
            { Hero->Feedback=Worker->GetGatheringStatus(); FScreenshotRequest::RequestScreenshot(ImageRoot/TEXT("Blocked.png"),false,false); Run->BlockedShot=true; }
            if(Elapsed<10.8f) return;
            Run->Pass &= Run->SawRetry && Run->ClosedPath;
            if(Mode==1)
            {
                Run->Pass &= Hero->SaveProgress(TEXT("FieldRecoveryCross"));
                FTimerHandle Active=*Poll; ExitCase(Run->Pass,Mode); World->GetTimerManager().ClearTimer(Active); return;
            }
            Run->Pass &= Hero->SaveProgress(TEXT("FieldRecoveryLocal")) && Hero->LoadProgress(TEXT("FieldRecoveryLocal"));
            Run->Pet=FindWorker(World,Hero); Run->Node=FindCrystal(World); Hero->GetCharacterMovement()->DisableMovement();
            Advance(1); return;
        }
        if(Run->Phase==1)
        {
            Run->Pass &= Resource->Remaining==2 && Worker->Experience==0 && InventoryAndKnowledge(Hero,0) && Worker->GetFieldResource()==Resource;
            if(Elapsed<2) return;
            if(!Run->Pass) { Fail(); return; }
            Run->Start=Worker->GetActorLocation(); Run->Gate->SetActorEnableCollision(false); Run->Gate->Destroy();
            if(auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)) Nav->Build();
            Hero->Health=0; // 检查组件级归属与存活门槛；完整死亡/复活另有回归。
            Advance(2); return;
        }
        if(Run->Phase==2)
        {
            if(!Run->OwnerReleased)
            {
                Run->Pass &= Resource->Remaining==2 && Worker->Experience==0 && InventoryAndKnowledge(Hero,0) &&
                    Worker->GetFieldResource()==Resource && Worker->GetGatheringStatus().Contains(TEXT("主人倒下")) && FVector::Dist2D(Run->Start,Worker->GetActorLocation())<1;
                if(Elapsed<1.5f) return;
                Hero->Health=100; Run->OwnerReleased=true; Advance(2); return;
            }
            Run->OpenPath |= PathOpen(World,Worker,Resource);
            if(Resource->Remaining==2 && Elapsed<18) return;
            Run->Pass &= Run->OpenPath && Resource->Remaining==1 && InventoryAndKnowledge(Hero,1) && Worker->Experience==1 &&
                FVector::Dist2D(Run->Start,Worker->GetActorLocation())>300 && Resource->CanReach(Worker,220);
            if(!Run->Pass) { Fail(); return; }
            if(Preview) { Hero->Feedback=TEXT("路线恢复：实际抵达采集，获得水晶×1、经验×1"); FScreenshotRequest::RequestScreenshot(ImageRoot/TEXT("Recovered.png"),false,false); }
            Advance(3); return;
        }
        if(Run->Phase==3)
        {
            if(Elapsed<.4f) return;
            Run->Pass &= Resource->Remaining==1 && InventoryAndKnowledge(Hero,1) && Worker->Experience==1;
            Resource->Destroy(); Advance(4); return;
        }
        if(Run->Phase==4)
        {
            if(Elapsed<.5f) return;
            Run->Pass &= !FindCrystal(World) && !Worker->GetFieldResource() && Worker->Activity==ECreatureActivity::Follow && !Worker->bResumeWorkAfterRest &&
                InventoryAndKnowledge(Hero,1) && Worker->Experience==1;
            Run->Pass &= Hero->SaveProgress(TEXT("FieldRecoveryDeleted")) && Hero->LoadProgress(TEXT("FieldRecoveryDeleted"));
            Worker=FindWorker(World,Hero);
            Run->Pass &= Worker && !FindCrystal(World) && Worker->Activity==ECreatureActivity::Follow && !Worker->GetFieldResource();
            Run->Pass &= Hero->LoadProgress(TEXT("FieldRecoveryLocal"));
            Worker=FindWorker(World,Hero); Resource=FindCrystal(World); Run->Pet=Worker; Run->Node=Resource;
            Hero->GetCharacterMovement()->DisableMovement();
            if(!Worker || !Resource) { Fail(); return; }
            Worker->Stamina=0; Worker->Tick(.1f);
            Run->Pass &= Worker->Activity==ECreatureActivity::Rest && Worker->bResumeWorkAfterRest && Worker->GetFieldResource()==Resource;
            Resource->Destroy(); Advance(5); return;
        }
        if(Elapsed<.5f) return;
        Run->Pass &= !FindCrystal(World) && Worker->Activity==ECreatureActivity::Follow && !Worker->GetFieldResource() && !Worker->bResumeWorkAfterRest &&
            InventoryAndKnowledge(Hero,0) && Worker->Experience==0;
        UE_LOG(LogTemp,Display,TEXT("CF_FIELD_RECOVERY checks retry=%d closed=%d reopened=%d pass=%d"),Run->SawRetry,Run->ClosedPath,Run->OpenPath,Run->Pass);
        FTimerHandle Active=*Poll; ExitCase(Run->Pass,Mode); World->GetTimerManager().ClearTimer(Active);
    },.1f,true);
}
