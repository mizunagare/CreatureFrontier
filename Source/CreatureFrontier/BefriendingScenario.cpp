#include "BefriendingScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "InventoryComponent.h"
#include "InputRoutingQA.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
ACreatureCharacter* Owned(UWorld* World,ACFPlayerCharacter* Hero)
{for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController())return *It;return nullptr;}
bool Snapshot(UWorld* World,ACFPlayerCharacter* Hero)
{
    auto* Pet=Owned(World,Hero);
    return Pet && Pet->Disposition==ECreatureDisposition::Tamed && Pet->Activity==ECreatureActivity::Follow &&
        Pet->Health==Pet->GetMaxHealth() && Pet->BondPoints==3 && Pet->Experience==0 && Pet->PersistentId.IsValid() &&
        Hero->HasJournalEntry(Pet->Definition,true) && Hero->Inventory->Count(EFrontierItem::Food)==7 && Hero->Inventory->Count(EFrontierItem::CaptureDevice)==5;
}
}
void RunBefriendingScenario(UWorld* World,ACFPlayerCharacter* Hero,int Mode)
{
    if(Mode==0 && FParse::Param(FCommandLine::Get(),TEXT("CFBefriendPreview"))){RunBefriendingPreview(World,Hero);return;}
    bool Pass=true;
    if(Mode==2)
    {
        Pass=Hero->LoadProgress(TEXT("BefriendCross")) && Snapshot(World,Hero);
        if(auto* Pet=Owned(World,Hero)){const FGuid Id=Pet->PersistentId;Pass &= !Hero->BefriendWildCreature(Pet) && Snapshot(World,Hero) && Pet->PersistentId==Id;}
    }
    else
    {
        auto* Leaf=LoadObject<UCreatureDefinition>(nullptr,TEXT("/Game/Creatures/DA_Mosskip.DA_Mosskip"));
        auto* Stone=LoadObject<UCreatureDefinition>(nullptr,TEXT("/Game/Creatures/DA_Craglet.DA_Craglet"));
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)It->Destroy();
        if(!Leaf || !Stone || Leaf->PeacefulFoodCost!=3 || Stone->PeacefulFoodCost!=0)
        {UE_LOG(LogTemp,Error,TEXT("CF_BEFRIEND FAIL assets"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
        Hero->SetActorLocation(FVector(-450,-1100,40));Hero->GetCharacterMovement()->DisableMovement();
        Hero->Inventory->Restore({{EFrontierItem::Food,10},{EFrontierItem::CaptureDevice,5}});
        const FTransform At(FRotator::ZeroRotator,Hero->GetActorLocation()+FVector(180,0,0));
        auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),At);
        Pet->Definition=Leaf;Pet->FinishSpawning(At);Pet->GetCharacterMovement()->DisableMovement();
        const FGuid Id=Pet->PersistentId;
        const float HP=Hero->Health;Pet->Tick(.1f);
        bool Calm=Pet->Disposition==ECreatureDisposition::Wild && Pet->Activity==ECreatureActivity::Idle && Hero->Health==HP;
        const FTransform RetaliationAt(FRotator::ZeroRotator,Hero->GetActorLocation()+FVector(0,200,0));
        auto* Retaliator=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),RetaliationAt);
        Retaliator->Definition=Leaf;Retaliator->FinishSpawning(RetaliationAt);Retaliator->GetCharacterMovement()->DisableMovement();
        const bool Hurt=UGameplayStatics::ApplyDamage(Retaliator,1,Hero->GetController(),Hero,nullptr)>0;
        Retaliator->Tick(.1f);const bool Retaliation=Hurt && Retaliator->Disposition==ECreatureDisposition::Hostile && Retaliator->Activity==ECreatureActivity::Combat;
        Retaliator->Destroy();Hero->Health=HP;
        auto Reject=[&](){return !Hero->BefriendWildCreature(Pet) && Pet->Tamer==nullptr && Hero->Inventory->Count(EFrontierItem::Food)==10 && Hero->Inventory->Count(EFrontierItem::CaptureDevice)==5 && Pet->PersistentId==Id;};
        bool Gates=true;
        Pet->Health-=1;Gates &= Reject();Pet->Health=Pet->GetMaxHealth();
        Pet->bAlpha=true;Gates &= Reject();Pet->bAlpha=false;
        Pet->Tamer=Hero->GetController();Gates &= !Hero->BefriendWildCreature(Pet) && Hero->Inventory->Count(EFrontierItem::Food)==10;Pet->Tamer=nullptr;
        Pet->Disposition=ECreatureDisposition::Hostile;Gates &= Reject();Pet->Disposition=ECreatureDisposition::Wild;
        for(auto State:{ECreatureActivity::Combat,ECreatureActivity::Work,ECreatureActivity::ReturnHome}){Pet->Activity=State;Gates &= Reject();}Pet->Activity=ECreatureActivity::Idle;
        Pet->SetActorLocation(At.GetLocation()+FVector(300,0,0));Gates &= Reject();Pet->SetActorLocation(At.GetLocation());
        UGameplayStatics::SetGamePaused(World,true);Gates &= Reject();UGameplayStatics::SetGamePaused(World,false);
        Hero->Health=0;Gates &= Reject();Hero->Health=HP;
        const int32 Cost=Leaf->PeacefulFoodCost;for(int32 Bad:{0,-1,11}){Leaf->PeacefulFoodCost=Bad;Gates &= Reject();}Leaf->PeacefulFoodCost=Cost;
        auto* Wall=World->SpawnActor<AStaticMeshActor>(Hero->GetActorLocation()+FVector(90,0,40),FRotator::ZeroRotator);
        Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Wall->SetActorScale3D(FVector(.2,2,2));Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));Gates &= Reject();Wall->Destroy();
        auto* Threat=World->SpawnActor<ACreatureCharacter>(Hero->GetActorLocation()+FVector(0,400,0),FRotator::ZeroRotator);
        Threat->Disposition=ECreatureDisposition::Hostile;Threat->SetActorTickEnabled(false);Gates &= Reject();Threat->Destroy();
        Hero->Inventory->Restore({{EFrontierItem::Food,2},{EFrontierItem::CaptureDevice,5}});
        Gates &= !Hero->BefriendWildCreature(Pet) && Hero->Inventory->Count(EFrontierItem::Food)==2 && !Pet->Tamer;
        Hero->Inventory->Restore({{EFrontierItem::Food,10},{EFrontierItem::CaptureDevice,5}});
        const FRotator View=(Pet->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
        const bool Input=CFInputQA::Press(Hero,EKeys::J);
        const bool Joined=Snapshot(World,Hero) && Pet->PersistentId==Id && !Hero->BefriendWildCreature(Pet) && Snapshot(World,Hero);
        Pet->Nutrition=40;const bool Cooldown=!Hero->FeedCompanion(Pet) && Hero->Inventory->Count(EFrontierItem::Food)==7;Pet->Nutrition=100;
        const bool Saved=Hero->SaveProgress(TEXT("BefriendCross")) && Hero->LoadProgress(TEXT("BefriendCross")) && Snapshot(World,Hero) && Owned(World,Hero)->PersistentId==Id;
        Pass=Calm && Retaliation && Gates && Input && Joined && Cooldown && Saved;
        UE_LOG(LogTemp,Display,TEXT("CF_BEFRIEND_DETAIL calm=%d retaliation=%d gates=%d input=%d joined=%d cooldown=%d saved=%d"),Calm,Retaliation,Gates,Input,Joined,Cooldown,Saved);
    }
    UE_LOG(LogTemp,Display,TEXT("CF_BEFRIEND%s %s"),Mode==1?TEXT("_WRITE"):Mode==2?TEXT("_READ"):TEXT(""),Pass?TEXT("PASS"):TEXT("FAIL"));
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}
