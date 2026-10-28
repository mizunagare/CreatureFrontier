#include "FieldWorkScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "ResourceNode.h"
#include "ResourceDefinition.h"
#include "FrontierSaveGame.h"
#include "InventoryComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace CFFieldQA
{
const FGuid CrystalId(0xCF170000,0,0,7);
AResourceNode* FieldCrystal(UWorld* World)
{
    for(TActorIterator<AResourceNode> It(World);It;++It) if(It->PersistentId==CrystalId) return *It;
    return nullptr;
}
ACreatureCharacter* Owned(UWorld* World,ACFPlayerCharacter* Hero)
{
    for(TActorIterator<ACreatureCharacter> It(World);It;++It) if(It->Tamer==Hero->GetController()) return *It;
    return nullptr;
}
void Finish(bool Pass,int32 Mode)
{
    const TCHAR* Label=Mode==1?TEXT("CF_FIELD_WRITE"):Mode==2?TEXT("CF_FIELD_READ"):TEXT("CF_FIELD");
    UE_LOG(LogTemp,Display,TEXT("%s %s"),Label,Pass?TEXT("PASS"):TEXT("FAIL"));
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}
}

void RunFieldWorkScenario(UWorld* World,ACFPlayerCharacter* Hero,int32 Mode)
{
    using namespace CFFieldQA;
    if(Mode==2)
    {
        const bool Loaded=Hero->LoadProgress(TEXT("FieldCross"));
        auto* Pet=Owned(World,Hero); auto* Node=FieldCrystal(World);
        UE_LOG(LogTemp,Display,TEXT("CF_FIELD_READ_STATE loaded=%d pet=%d node=%d target=%d site=%d activity=%d stock=%d crystal=%d feedback=%s"),
            Loaded,Pet!=nullptr,Node!=nullptr,Pet && Pet->GetFieldResource()==Node,Pet && Pet->WorkSite!=nullptr,
            Pet?static_cast<int32>(Pet->Activity):-1,Node?Node->Remaining:-1,Hero->Inventory->Count(EFrontierItem::Crystal),*Hero->Feedback);
        Finish(Loaded && Pet && Node && Pet->GetFieldResource()==Node && !Pet->WorkSite &&
            Pet->Activity==ECreatureActivity::Work && Node->Remaining==0 && Hero->Inventory->Count(EFrontierItem::Crystal)==1,Mode);
        return;
    }
    UCreatureDefinition* Leaf=nullptr;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)
    { if(It->Definition && It->Definition->Form==ECreatureForm::Leaf) Leaf=It->Definition; It->Destroy(); }
    auto* Node=FieldCrystal(World);
    if(!Leaf || !Node) { Finish(false,Mode); return; }
    const FTransform Transform(Node->GetActorLocation()+FVector(160,0,90));
    auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    Pet->Definition=Leaf; Pet->FinishSpawning(Transform); Pet->Tame(Hero->GetController());
    Pet->SetActorTickEnabled(false); Pet->GetCharacterMovement()->DisableMovement();
    Hero->GetCharacterMovement()->DisableMovement(); Hero->Inventory->Restore({{EFrontierItem::Crystal,999}});
    Hero->SetActorLocation(Node->GetActorLocation()+FVector(0,-500,90));
    Hero->FirstPersonCamera->SetWorldRotation((Node->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());
    Hero->CommandWork();
    bool Baseline=Pet->GetFieldResource()==Node && !Pet->WorkSite && Pet->Activity==ECreatureActivity::Work;
    const int32 Stock=Node->Remaining;
    Pet->Tick(10); Baseline &= Node->Remaining==Stock && Pet->Experience==0;
    Hero->Inventory->Restore({{EFrontierItem::Crystal,0}});
    Pet->Tick(.1f); Baseline &= Node->Remaining==Stock-1 && Pet->Experience==1 && Hero->Inventory->Count(EFrontierItem::Crystal)==1;
    Node->Tick(1);
    // Leaf 没有采矿专长：5.8秒相当于2.9/3工作进度；重新分配后若保留，会在0.3秒后错误结算。
    Pet->Tick(5.8f); Pet->AssignFieldResource(Node); Pet->Tick(.3f);
    Baseline &= Node->Remaining==Stock-1; // 重新分配相同工作也会重置未完成进度。
    Pet->Stamina=0; Pet->Tick(.1f);
    Baseline &= Pet->Activity==ECreatureActivity::Rest && Pet->bResumeWorkAfterRest && Pet->GetFieldResource()==Node;
    Baseline &= Hero->SaveProgress(TEXT("FieldLocal")) && Hero->LoadProgress(TEXT("FieldLocal"));
    Pet=Owned(World,Hero); Node=FieldCrystal(World);
    if(!Pet || !Node) { Finish(false,Mode); return; }
    Pet->SetActorTickEnabled(false); Pet->GetCharacterMovement()->DisableMovement();
    Baseline &= Pet->Activity==ECreatureActivity::Rest && Pet->bResumeWorkAfterRest && Pet->GetFieldResource()==Node;
    Pet->Stamina=80; Pet->Tick(.1f); Baseline &= Pet->Activity==ECreatureActivity::Work;
    auto* Bad=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("FieldLocal"),0));
    if(!Bad) { Finish(false,Mode); return; }
    for(auto& Record:Bad->Creatures) if(Record.bOwned) Record.FieldResourceId=FGuid::NewGuid();
    UGameplayStatics::SaveGameToSlot(Bad,TEXT("FieldBad"),0);
    Baseline &= !Hero->LoadProgress(TEXT("FieldBad")) && Owned(World,Hero)==Pet && FieldCrystal(World)==Node;
    for(int32 Case=0;Case<4;++Case)
    {
        auto* Invalid=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("FieldLocal"),0));
        for(auto& Record:Invalid->Creatures) if(Record.bOwned)
        {
            if(Case==0) Record.WorkSiteName=TEXT("ConflictingPost");
            if(Case==1) Record.bResumeWorkAfterRest=false;
            if(Case==2) Record.bOwned=false;
            if(Case==3) Record.Disposition=ECreatureDisposition::Wild;
        }
        UGameplayStatics::SaveGameToSlot(Invalid,TEXT("FieldBad"),0);
        Baseline &= !Hero->LoadProgress(TEXT("FieldBad")) && Owned(World,Hero)==Pet && FieldCrystal(World)==Node;
    }
    Bad=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("FieldLocal"),0));
    Bad->FormatVersion=19; UGameplayStatics::SaveGameToSlot(Bad,TEXT("FieldLegacy"),0);
    Baseline &= Hero->LoadProgress(TEXT("FieldLegacy"));
    Pet=Owned(World,Hero); Node=FieldCrystal(World);
    Baseline &= Pet && !Pet->GetFieldResource() && Node && Node->Remaining==Stock-1;
    if(!Pet || !Node) { Finish(false,Mode); return; }
    Pet->SetActorTickEnabled(false);
    Pet->AssignFieldResource(Node); Pet->SetActivity(ECreatureActivity::Follow);
    Baseline &= !Pet->GetFieldResource();
    Pet->AssignFieldResource(Node); Pet->Health=0; Pet->Tick(.1f);
    // A downed worker may never settle shared stock, even if externally modified health bypasses TakeDamage.
    Baseline &= !Node->TryHarvest(Hero,Pet); Pet->Health=Pet->GetMaxHealth();
    Pet->AssignFieldResource(Node); Pet->Nutrition=0; Pet->Tick(.1f);
    Baseline &= Pet->Activity==ECreatureActivity::Rest && Pet->bResumeWorkAfterRest;
    Pet->Nutrition=100; Pet->Stamina=100; Pet->Tick(.1f);
    Node->Remaining=1; Node->HarvestCooldown=0; Node->Refresh();
    Hero->Inventory->Restore({{EFrontierItem::Crystal,0}});
    Pet->SetActorLocation(Node->GetActorLocation()+FVector(-650,-200,90));
    Pet->GetCharacterMovement()->SetMovementMode(MOVE_Walking); Pet->SetActorTickEnabled(true);
    const FVector Start=Pet->GetActorLocation(); const double Started=World->GetTimeSeconds();
    TSharedRef<FTimerHandle> Poll=MakeShared<FTimerHandle>();
    World->GetTimerManager().SetTimer(*Poll,[World,Hero,Pet,Node,Start,Started,Poll,Baseline,Mode,Harvested=-1.0]() mutable
    {
        if(Node->Remaining==0 && Harvested<0) Harvested=World->GetTimeSeconds();
        if(Harvested<0 && World->GetTimeSeconds()-Started<18) return;
        if(Harvested>=0 && World->GetTimeSeconds()-Harvested<2) return;
        bool Pass=Baseline && Harvested>=0 && FVector::Dist2D(Start,Pet->GetActorLocation())>350 &&
            Hero->Inventory->Count(EFrontierItem::Crystal)==1 && Node->Remaining==0 && Pet->GetFieldResource()==Node && !Pet->WorkSite;
        Pass &= Hero->SaveProgress(TEXT("FieldCross"));
        UE_LOG(LogTemp,Display,TEXT("CF_FIELD checks baseline=%d moved=%.1f stock=%d crystal=%d"),Baseline,FVector::Dist2D(Start,Pet->GetActorLocation()),Node->Remaining,Hero->Inventory->Count(EFrontierItem::Crystal));
        FTimerHandle Active=*Poll;
        if(Pass && FParse::Param(FCommandLine::Get(),TEXT("CFFieldPreview")))
        {
            Hero->GetCharacterMovement()->DisableMovement(); Hero->SetActorLocation(Node->GetActorLocation()+FVector(0,-550,180));
            Hero->FirstPersonCamera->bUsePawnControlRotation=false;
            Hero->FirstPersonCamera->SetWorldRotation((Node->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());
            FTimerHandle Shot,Done;
            World->GetTimerManager().SetTimer(Shot,[](){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("FieldWorkPreview.png"),false,false);},1.f,false);
            World->GetTimerManager().SetTimer(Done,[Mode](){Finish(true,Mode);},2.f,false);
        }
        else Finish(Pass,Mode);
        World->GetTimerManager().ClearTimer(Active);
    },.1f,true);
}
