#include "RescueScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "InventoryComponent.h"
#include "InputRoutingQA.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "HollowScenario.h"
#include "HomeScenario.h"
#include "CreatureDefinition.h"
#include "WildEncounter.h"
#include "ResourceNode.h"
#include "FrontierSaveGame.h"
#include "PlayerPaceComponent.h"
#include "Containers/Ticker.h"
#include "GameFramework/Controller.h"

void RunRescueGateScenario(UWorld* World, ACFPlayerCharacter* Hero)
{
    // 隔离验证输入/消耗事务；直接伤害夹具不是自然战斗证据。
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)It->Destroy();
    Hero->SetActorLocation(FVector(-450,0,40));Hero->GetCharacterMovement()->DisableMovement();
    auto* Pet=World->SpawnActor<ACreatureCharacter>(FVector(-450,180,40),FRotator::ZeroRotator);
    if(!Pet){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    Pet->Tame(Hero->GetController());Pet->SetActorTickEnabled(false);Pet->GetCharacterMovement()->DisableMovement();
    UGameplayStatics::ApplyDamage(Pet,1000,nullptr,Hero,nullptr);
    const FGuid Id=Pet->PersistentId;const FVector Position=Pet->GetActorLocation();
    auto Aim=[&](){const auto View=(Pet->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);};
    auto Bag=[&](int32 Medicine){Hero->Inventory->Restore({{EFrontierItem::Medicine,Medicine},{EFrontierItem::Material,7}});};
    auto Press=[&](){return CFInputQA::Press(Hero,EKeys::P);};
    Aim();Bag(0);
    const bool NoMedicine=Hero->TraceCreature()==Pet && Press() && Pet->Health==0 && Hero->Inventory->Count(EFrontierItem::Medicine)==0;
    Bag(3);Hero->Health=0;
    const bool OwnerDown=Press() && Pet->Health==0 && Hero->Inventory->Count(EFrontierItem::Medicine)==3;Hero->Health=100;
    // 实体墙阻挡真实交互射线，不用篡改目标返回值。
    auto* Wall=World->SpawnActor<AActor>();auto* Mesh=NewObject<UStaticMeshComponent>(Wall);Wall->SetRootComponent(Mesh);Mesh->RegisterComponent();
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Mesh->SetCollisionProfileName(TEXT("BlockAll"));Wall->SetActorLocation(FVector(-450,90,70));Wall->SetActorScale3D(FVector(2,.2,2));
    const bool Cover=Hero->TraceCreature()!=Pet && Press() && Pet->Health==0 && Hero->Inventory->Count(EFrontierItem::Medicine)==3;
    Wall->Destroy();
    Pet->Tamer=nullptr;
    const bool Ownership=Press() && Pet->Health==0 && Hero->Inventory->Count(EFrontierItem::Medicine)==3;Pet->Tamer=Hero->GetController();
    const bool Saved=Hero->SaveProgress(TEXT("RescueGateDead")) && Hero->LoadProgress(TEXT("RescueGateDead"));
    Pet=nullptr;for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->PersistentId==Id)Pet=*It;
    if(!Pet){UE_LOG(LogTemp,Error,TEXT("CF_RESCUE_GATES FAIL restore"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    Pet->SetActorTickEnabled(false);Pet->GetCharacterMovement()->DisableMovement();Aim();
    const bool Restored=Saved && Pet->Health==0 && Pet->GetActorLocation().Equals(Position,.1f) && Hero->Inventory->Count(EFrontierItem::Medicine)==3;
    const bool Treatment=Hero->TraceCreature()==Pet && Press() && Pet->Health==40 && Pet->PersistentId==Id && Pet->GetActorLocation().Equals(Position,.1f) && Hero->Inventory->Count(EFrontierItem::Medicine)==2 && Hero->Inventory->Count(EFrontierItem::Material)==7;
    Pet->Health=Pet->GetMaxHealth();
    const bool Full=Press() && Pet->Health==Pet->GetMaxHealth() && Hero->Inventory->Count(EFrontierItem::Medicine)==2;
    const bool Pass=NoMedicine && OwnerDown && Cover && Ownership && Restored && Treatment && Full;
    UE_LOG(LogTemp,Display,TEXT("CF_RESCUE_GATES %s noMedicine=%d ownerDown=%d cover=%d ownership=%d restored=%d treatment=%d full=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),NoMedicine,OwnerDown,Cover,Ownership,Restored,Treatment,Full);
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}

void RunNaturalRescueCheckpoint(UWorld* World, ACFPlayerCharacter* Hero)
{
    AWildEncounter* Camp=nullptr;for(TActorIterator<AWildEncounter> It(World);It;++It)if(It->EncounterId==TEXT("EmberHollow"))Camp=*It;
    auto* Enemy=Camp?Camp->GetResident():nullptr;auto* Definition=LoadObject<UCreatureDefinition>(nullptr,TEXT("/Game/Creatures/DA_Craglet.DA_Craglet"));
    if(!Enemy || !Definition || Enemy->Level!=3){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    // 仅出发时设为已负伤伙伴；途中完全使用自然住民的AI与伤害。
    Hero->SetActorLocation(FVector(-250,-400,40));Hero->Inventory->Restore({{EFrontierItem::Medicine,1},{EFrontierItem::Material,7},{EFrontierItem::Food,9}});
    const FTransform At(Hero->GetActorLocation()+FVector(0,180,0));auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),At);
    if(!Pet){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    Pet->Definition=Definition;Pet->FinishSpawning(At);Pet->Tame(Hero->GetController());Pet->Health=20;
    auto* Input=NewObject<UHomeMovementProbe>(Hero);Input->RegisterComponent();
    auto Points=MakeHollowRoutePoints();const double Deadline=FPlatformTime::Seconds()+100;
    if(!CFInputQA::Press(Hero,EKeys::LeftShift)){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([World,Hero,Pet,Enemy,Camp,Input,Points,Deadline,Index=0,Previous=Hero->GetActorLocation(),Travel=0.f,Engaged=false](float) mutable
    {
        const FVector Now=Hero->GetActorLocation();const float Step=FVector::Dist2D(Now,Previous);Travel+=Step;Previous=Now;
        if(FPlatformTime::Seconds()>Deadline || Hero->Health<=0 || Step>160 || !Enemy->IsActorTickEnabled() || !Pet->IsActorTickEnabled() || Camp->GetResident()!=Enemy)
        {UE_LOG(LogTemp,Error,TEXT("CF_RESCUE_NATURAL_WRITE FAIL index=%d heroHP=%.1f petHP=%.1f enemyHP=%.1f"),Index,Hero->Health,Pet->Health,Enemy->Health);FPlatformMisc::RequestExitWithStatus(false,1);return false;}
        if(Pet->Health<=0)
        {
            Input->Direction=FVector::ZeroVector;CFInputQA::Release(Hero,EKeys::LeftShift);
            AResourceNode* Cache=nullptr;for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,11))Cache=*It;
            const bool Pass=Engaged && Travel>2000 && Now.Y<-2700 && Enemy->Health>0 && Enemy->Health<Enemy->GetMaxHealth() && Cache && Cache->Remaining==6 && Hero->Inventory->Count(EFrontierItem::Medicine)==1 && Hero->Inventory->Count(EFrontierItem::Material)==7 && Hero->SaveProgress(TEXT("RescueNaturalDowned"));
            UE_LOG(LogTemp,Display,TEXT("CF_RESCUE_NATURAL_WRITE %s travel=%.1f heroHP=%.1f petHP=%.1f enemyHP=%.1f pet=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Travel,Hero->Health,Pet->Health,Enemy->Health,*Pet->PersistentId.ToString());FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
        }
        if(Now.Y<-2700 && Enemy->Disposition==ECreatureDisposition::Hostile)
        {
            Engaged=true;Input->Direction=FVector::ZeroVector;const auto View=(Enemy->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
            if(Hero->TraceCreature()==Enemy && Pet->FocusTarget!=Enemy)CFInputQA::Press(Hero,EKeys::Q);
            return true;
        }
        if(Index<Points.Num())
        {if(FVector::Dist2D(Now,Points[Index])<70){++Index;Input->Direction=FVector::ZeroVector;}else{Input->Direction=(Points[Index]-Now).GetSafeNormal2D();Hero->GetController()->SetControlRotation(Input->Direction.Rotation());}}
        return true;
    }),.1f);
}
