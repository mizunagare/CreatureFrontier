#include "GatherHoldScenario.h"
#include "CFPlayerCharacter.h"
#include "PlayerGatheringComponent.h"
#include "ResourceNode.h"
#include "InventoryComponent.h"
#include "HomeBuildingComponent.h"
#include "HomeModule.h"
#include "InputRoutingQA.h"
#include "Camera/CameraComponent.h"
#include "CreatureCharacter.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/Ticker.h"

namespace
{
struct FGatherHoldTest
{
    UWorld* World;ACFPlayerCharacter* Hero;AResourceNode* Node;int32 Phase=0,InitialStock=0,Expected=0;
    double Next=0,Deadline=0;bool Pass=true;
    void Aim(bool AtNode=true)
    {
        Hero->SetActorLocation(FVector(-450,80,40));
        Hero->SetActorRotation(FRotator(0,90,0));Hero->FirstPersonCamera->SetRelativeRotation(FRotator::ZeroRotator);
        const auto View=AtNode?(Node->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation():FRotator(90,0,0);
        Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
    }
    bool Press(){return CFInputQA::Press(Hero,EKeys::M);}
    void Advance(int32 To,double Delay=1){Phase=To;Next=FPlatformTime::Seconds()+Delay;}
    bool Finish(bool Success)
    {
        Hero->Gathering->Stop();UGameplayStatics::SetGamePaused(World,false);
        UE_LOG(LogTemp,Display,TEXT("CF_GATHER_HOLD %s phase=%d material=%d stock=%d"),Success?TEXT("PASS"):TEXT("FAIL"),Phase,Hero->Inventory->Count(EFrontierItem::Material),IsValid(Node)?Node->Remaining:-1);
        FPlatformMisc::RequestExitWithStatus(false,Success?0:1);return false;
    }
    bool Tick()
    {
        const double Now=FPlatformTime::Seconds();if(Now>Deadline || !Pass)return Finish(false);
        const int32 Materials=Hero->Inventory->Count(EFrontierItem::Material);
        if(Phase==0)
        {
            if(Materials<8)return true;
            Pass &= Materials==8 && Node->Remaining==InitialStock-2 && CFInputQA::Release(Hero,EKeys::M) && !Hero->Gathering->IsGathering();
            Advance(1);return true;
        }
        if(Now<Next)return true;
        if(Phase==1)
        {
            Pass &= Materials==8 && Node->Remaining==InitialStock-2;
            Hero->SetActorLocation(FVector(-350,-1150,40));Hero->FirstPersonCamera->SetWorldRotation((FVector(-400,-800,-50)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());
            Pass &= CFInputQA::Press(Hero,EKeys::B) && CFInputQA::Press(Hero,EKeys::Eight) && CFInputQA::Action(Hero,TEXT("Attack"),EKeys::LeftMouseButton) && CFInputQA::Action(Hero,TEXT("Capture"),EKeys::RightMouseButton);
            int32 Floors=0;for(TActorIterator<AHomeModule> It(World);It;++It)Floors+=!It->bPreview && It->GetKind()==EHomeModuleKind::Floor;
            Pass &= Floors==1 && Hero->Inventory->Count(EFrontierItem::Material)==0;
            UE_LOG(LogTemp,Display,TEXT("CF_GATHER_BUILD %s initial=6 harvested=2 cost=8 material=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Hero->Inventory->Count(EFrontierItem::Material));
            Aim();Pass &= Press();Expected=1;Hero->ToggleHelp();Advance(2,.3);return true;
        }
        if(Phase==2){Pass &= !Hero->Gathering->IsGathering() && Materials==Expected;Hero->ToggleHelp();Advance(3);return true;}
        if(Phase==3){Pass &= Materials==Expected && Press();++Expected;Pass &= UGameplayStatics::SetGamePaused(World,true);Advance(4,.3);return true;}
        if(Phase==4){Pass &= !Hero->Gathering->IsGathering() && Materials==Expected;UGameplayStatics::SetGamePaused(World,false);Advance(5);return true;}
        if(Phase==5){Pass &= Materials==Expected && Press();++Expected;Aim(false);Advance(6);return true;}
        if(Phase==6){Pass &= Materials==Expected && !Hero->Gathering->IsGathering();Aim();Pass &= Press();++Expected;Hero->Gathering->HandleActivationChanged(false);Advance(7);return true;}
        if(Phase==7)
        {
            Pass &= Materials==Expected && !Hero->Gathering->IsGathering() && Press();++Expected;
            Pass &= Hero->SaveProgress(TEXT("GatherHoldAutomation")) && Hero->LoadProgress(TEXT("GatherHoldAutomation")) && !Hero->Gathering->IsGathering();
            Node=nullptr;for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,1))Node=*It;
            if(!Node)return Finish(false);Advance(8);return true;
        }
        if(Phase==8)
        {
            Pass &= Materials==Expected && !Hero->Gathering->IsGathering();
            const auto Bag=Hero->Inventory->GetStacks();auto Full=Bag;Full.Add(EFrontierItem::Material,999);const int32 Stock=Node->Remaining;
            Pass &= Hero->Inventory->Restore(Full) && Press() && !Hero->Gathering->IsGathering() && Node->Remaining==Stock && Hero->Inventory->Restore(Bag);
            Pass &= Press();++Expected;UGameplayStatics::ApplyDamage(Hero,1000,nullptr,nullptr,nullptr);
            Pass &= Hero->Health==0 && !Hero->Gathering->IsGathering() && Hero->RespawnAtBase() && !Hero->Gathering->IsGathering();Advance(9);return true;
        }
        Pass &= Materials==Expected && Expected==6 && InitialStock-Node->Remaining==8 && !Hero->Gathering->IsGathering();
        return Finish(Pass);
    }
};
}
void RunGatherHoldScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    auto State=MakeShared<FGatherHoldTest>();State->World=World;State->Hero=Hero;State->Node=nullptr;
    for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,1))State->Node=*It;
    if(!State->Node || Hero->Inventory->Count(EFrontierItem::Material)!=6){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    // 世界资源和开局背包不重置；固定观察站位仅隔离输入与事务，不是自然行走验收。
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)It->Destroy();
    Hero->GetCharacterMovement()->DisableMovement();State->InitialStock=State->Node->Remaining;State->Aim();
    State->Pass=CFInputQA::UniqueBindings(Hero) && State->Press();State->Deadline=FPlatformTime::Seconds()+20;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float){return State->Tick();}));
}
