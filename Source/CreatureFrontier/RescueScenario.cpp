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
