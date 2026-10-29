#include "HomeFundingScenario.h"
#include "HomeScenario.h"
#include "CFPlayerCharacter.h"
#include "ResourceNode.h"
#include "ResourceDefinition.h"
#include "InventoryComponent.h"
#include "PlayerGatheringComponent.h"
#include "InputRoutingQA.h"
#include "CreatureCharacter.h"
#include "WildEncounter.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Containers/Ticker.h"
#include "EngineUtils.h"

namespace
{
AResourceNode* FundingNode(UWorld* World,int32 Index)
{
    for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,Index))return *It;
    return nullptr;
}
struct FHomeFundingTest
{
    UWorld* World;ACFPlayerCharacter* Hero;int32 Step=0,Before=0;bool Started=false;
    double Deadline=0,BatchAt=0;
    const int32 Ids[6]={1,2,3,4,6,8};
    const int32 Amounts[6]={24,30,18,24,5,13};
    bool Fail(const TCHAR* Reason)
    {
        Hero->Gathering->Stop();UE_LOG(LogTemp,Error,TEXT("CF_HOME_FUNDING FAIL step=%d reason=%s material=%d feedback=%s"),Step,Reason,Hero->Inventory->Count(EFrontierItem::Material),*Hero->Feedback);
        FPlatformMisc::RequestExitWithStatus(false,1);return false;
    }
    bool Tick()
    {
        if(FPlatformTime::Seconds()>Deadline)return Fail(TEXT("deadline"));
        if(Step==6)
        {
            auto* Bag=Hero->Inventory.Get();
            if(Bag->Count(EFrontierItem::Material)!=107 || Bag->Count(EFrontierItem::Fiber)!=13 || Bag->Count(EFrontierItem::Food)!=3)return Fail(TEXT("harvest-budget"));
            Hero->SetActorLocation(FVector(-450,0,40));
            for(int32 Index=0;Index<6;++Index)if(!Hero->CraftRecipe(TEXT("WeaveSupplies")))return Fail(TEXT("weave"));
            if(!Hero->CraftRecipe(TEXT("PlantingSeeds")) || Bag->Count(EFrontierItem::Material)!=125 || Bag->Count(EFrontierItem::Seeds)!=3 ||
                Bag->Count(EFrontierItem::Fiber)!=0 || Bag->Count(EFrontierItem::Food)!=2 || !VerifyHomeFundingSources(World))return Fail(TEXT("craft-budget"));
            UE_LOG(LogTemp,Display,TEXT("CF_HOME_FUNDING PASS material=125 seeds=3 food=2 raw=101 fiber=13 weave=6"));
            RunHomeScenario(World,Hero,0,true);return false;
        }
        auto* Node=FundingNode(World,Ids[Step]);if(!Node || !Node->Definition)return Fail(TEXT("source-missing"));
        if(!Started)
        {
            if(Node->Remaining!=Node->Definition->Capacity || Node->Remaining<Amounts[Step] ||
                Node->Definition->OutputItem!=(Step==5?EFrontierItem::Fiber:EFrontierItem::Material))return Fail(TEXT("source-budget"));
            const bool NorthSide=Ids[Step]==3;
            Hero->SetActorLocation(Node->GetActorLocation()+FVector(0,NorthSide?210:-210,40));
            Hero->SetActorRotation(FRotator(0,NorthSide?-90:90,0));Hero->FirstPersonCamera->SetRelativeRotation(FRotator::ZeroRotator);
            const auto View=(Node->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
            Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
            Before=Node->Remaining;BatchAt=World->GetTimeSeconds();Started=true;
            if(!CFInputQA::Press(Hero,EKeys::M))return Fail(TEXT("press"));
        }
        const int32 Taken=Before-Node->Remaining;
        if(Taken>Amounts[Step])return Fail(TEXT("over-harvest"));
        if(Taken<Amounts[Step]){if(!Hero->Gathering->IsGathering())return Fail(TEXT("hold-stopped"));return true;}
        if(!CFInputQA::Release(Hero,EKeys::M) || Hero->Gathering->IsGathering() ||
            World->GetTimeSeconds()-BatchAt+.1<(Amounts[Step]-1)*Node->Definition->HarvestInterval)return Fail(TEXT("release-or-cooldown"));
        UE_LOG(LogTemp,Display,TEXT("CF_HOME_FUND_SOURCE PASS id=%d taken=%d remaining=%d seconds=%.2f"),Ids[Step],Taken,Node->Remaining,World->GetTimeSeconds()-BatchAt);
        ++Step;Started=false;return true;
    }
};
}
bool VerifyHomeFundingSources(UWorld* World)
{
    const int32 Ids[]={1,2,3,4,6,8},Remaining[]={0,0,0,0,13,5};
    for(int32 Index=0;Index<6;++Index){const auto* Node=FundingNode(World,Ids[Index]);if(!Node || Node->Remaining!=Remaining[Index])return false;}
    return true;
}
void RunHomeFundingScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    if(Hero->Inventory->Count(EFrontierItem::Material)!=6 || Hero->Inventory->Count(EFrontierItem::Food)!=3 || Hero->Inventory->Count(EFrontierItem::Fiber)!=0 || Hero->Inventory->Count(EFrontierItem::Seeds)!=0)
    {UE_LOG(LogTemp,Error,TEXT("CF_HOME_FUNDING FAIL initial-budget"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    // 仅隔离经济链：固定玩家站位、冻结无关野生AI，但不改碰撞、源库存、冷却或产出。
    for(TActorIterator<AWildEncounter> It(World);It;++It)It->SetActorTickEnabled(false);
    for(TActorIterator<ACreatureCharacter> It(World);It;++It){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();}
    Hero->GetCharacterMovement()->DisableMovement();
    auto State=MakeShared<FHomeFundingTest>();State->World=World;State->Hero=Hero;State->Deadline=FPlatformTime::Seconds()+135;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float){return State->Tick();}));
}
