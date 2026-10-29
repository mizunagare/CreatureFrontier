#include "HomeFundingScenario.h"
#include "HomeScenario.h"
#include "CFPlayerCharacter.h"
#include "ResourceNode.h"
#include "ResourceDefinition.h"
#include "InventoryComponent.h"
#include "PlayerGatheringComponent.h"
#include "InputRoutingQA.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "CompanionCargoComponent.h"
#include "HomeModule.h"
#include "HomeBuildingComponent.h"
#include "StorageInteractionComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
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
    bool Carried=false,CargoDone=false,SawCargo=false;int32 CargoPhase=0;
    bool Befriended=false,Joined=false,MadeFood=false;
    FGuid OriginalPet;TWeakObjectPtr<UHomeMovementProbe> ApproachInput;
    FVector PreviousHero,PreviousLifePet;double ApproachAt=0,HeroTravel=0,LifePetTravel=0;
    TWeakObjectPtr<ACreatureCharacter> Carrier;TWeakObjectPtr<AHomeModule> Crate;
    FVector Previous;double Travel=0,CargoAt=0;
    const int32 Ids[6]={1,2,3,4,6,8};
    const int32 Amounts[6]={24,30,18,24,5,13};
    bool Fail(const TCHAR* Reason)
    {
        if(auto* Input=ApproachInput.Get()){Input->Direction=FVector::ZeroVector;Input->DestroyComponent();}
        Hero->Gathering->Stop();UE_LOG(LogTemp,Error,TEXT("CF_HOME_FUNDING FAIL step=%d reason=%s material=%d feedback=%s"),Step,Reason,Hero->Inventory->Count(EFrontierItem::Material),*Hero->Feedback);
        FPlatformMisc::RequestExitWithStatus(false,1);return false;
    }
    void Look(FVector Position,FVector Target)
    {
        Hero->SetActorLocation(Position);const auto View=(Target-(Position+FVector(0,0,64))).Rotation();
        Hero->SetActorRotation(FRotator(0,View.Yaw,0));Hero->FirstPersonCamera->SetRelativeRotation(FRotator::ZeroRotator);
        Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
    }
    bool BefriendTick()
    {
        auto* Pet=Carrier.Get();
        if(!MadeFood)
        {
            if(!CFInputQA::Press(Hero,EKeys::Three) || Hero->Inventory->Count(EFrontierItem::Material)!=4 || Hero->Inventory->Count(EFrontierItem::Food)!=4)return Fail(TEXT("befriend-food"));
            UE_LOG(LogTemp,Display,TEXT("CF_HOME_BEFRIEND_FOOD PASS material=4 food=4 cost=2"));
            auto* Input=NewObject<UHomeMovementProbe>(Hero);Input->RegisterComponent();ApproachInput=Input;
            PreviousHero=Hero->GetActorLocation();ApproachAt=World->GetTimeSeconds();MadeFood=true;
        }
        const FVector Now=Hero->GetActorLocation();const double Move=FVector::Dist2D(Now,PreviousHero);PreviousHero=Now;HeroTravel+=Move;
        if(Move>=80 || Hero->Health<=0 || !Hero->GetActorEnableCollision() || World->GetTimeSeconds()-ApproachAt>25)return Fail(TEXT("befriend-approach"));
        const auto View=(Pet->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
        Hero->SetActorRotation(FRotator(0,View.Yaw,0));Hero->FirstPersonCamera->SetRelativeRotation(FRotator::ZeroRotator);
        Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
        if(Hero->GetDistanceTo(Pet)>210)
        {ApproachInput->Direction=(Pet->GetActorLocation()-Now).GetSafeNormal2D();return true;}
        ApproachInput->Direction=FVector::ZeroVector;Hero->GetCharacterMovement()->StopMovementImmediately();Hero->ConsumeMovementInputVector();
        // 使用原J绑定和唯一结交事务，不修改原生生物的位置、健康、性情或驯服状态。
        if(HeroTravel<300 || !CFInputQA::Press(Hero,EKeys::J) || Pet->Tamer!=Hero->GetController() || Pet->Disposition!=ECreatureDisposition::Tamed ||
            Pet->Activity!=ECreatureActivity::Follow || Pet->PersistentId!=OriginalPet || Pet->BondPoints!=3 || Pet->Experience!=0 ||
            Hero->Inventory->Count(EFrontierItem::Food)!=1 || Hero->Inventory->Count(EFrontierItem::Material)!=4 || Hero->Inventory->Count(EFrontierItem::CaptureDevice)!=5 ||
            !Hero->HasJournalEntry(Pet->Definition,true))return Fail(TEXT("befriend-join"));
        Pet->Tags.AddUnique(TEXT("HomeFundingCarrier"));Pet->Tags.AddUnique(TEXT("HomeFundingBefriended"));
        UE_LOG(LogTemp,Display,TEXT("CF_HOME_BEFRIEND_JOIN PASS material=4 food=1 capture=5 bond=3 travel=%.1f pet=%s"),HeroTravel,*Pet->PersistentId.ToString());
        ApproachInput->DestroyComponent();ApproachInput.Reset();Hero->GetCharacterMovement()->DisableMovement();Joined=true;return true;
    }
    bool StartCargoOrder()
    {
        auto* Pet=Carrier.Get();auto* Node=FundingNode(World,1);
        if(!Pet || !Node)return Fail(TEXT("cargo-source"));
        Look(FVector(-800,-750,60),Crate->GetActorLocation()+FVector(0,0,40));
        if(!CFInputQA::Press(Hero,EKeys::V) || !Hero->HasDeliverySelection())return Fail(TEXT("cargo-selection"));
        Look(FVector(-200,290,40),Node->GetActorLocation());
        if(!CFInputQA::Press(Hero,EKeys::E) || Pet->GetFieldResource()!=Node || !Pet->Cargo->HasOrder() || Pet->Cargo->GetTarget()!=Crate.Get())return Fail(TEXT("cargo-order"));
        Look(FVector(-100,600,60),Node->GetActorLocation());
        Previous=Pet->GetActorLocation();Travel=0;CargoAt=World->GetTimeSeconds();CargoPhase=1;return true;
    }
    bool CargoTick()
    {
        if(CargoPhase==0)
        {
            if(Hero->Inventory->Count(EFrontierItem::Material)!=104)return Fail(TEXT("cargo-start-budget"));
            bool Built=CFInputQA::Press(Hero,EKeys::B) && CFInputQA::Press(Hero,EKeys::Eight);
            Built &= CFHomeInputQA::Place(World,Hero,EHomeModuleKind::Floor,FVector(-350,-1150,40),FVector(-400,-800,-50),FVector(-400,-800,-40));
            Built &= CFHomeInputQA::Place(World,Hero,EHomeModuleKind::Floor,FVector(-450,-900,60),FVector(-800,-800,-50),FVector(-800,-800,-40));
            Built &= CFHomeInputQA::Place(World,Hero,EHomeModuleKind::Storage,FVector(-800,-850,60),FVector(-900,-900,-30),FVector(-900,-900,-30));
            for(int32 Count=0;Hero->HomeBuilding->Selected!=EHomeModuleKind::Floor && Count<9;++Count)Built &= CFInputQA::Press(Hero,EKeys::V);
            Built &= CFInputQA::Action(Hero,TEXT("Capture"),EKeys::RightMouseButton);
            int32 Capital=0;for(TActorIterator<AHomeModule> It(World);It;++It)if(!It->bPreview)
            {It->Tags.AddUnique(TEXT("HomeFundingCapital"));++Capital;if(It->GetKind()==EHomeModuleKind::Storage)Crate=*It;}
            if(!Built || Capital!=3 || !Crate.IsValid() || Hero->Inventory->Count(EFrontierItem::Material)!=82)return Fail(TEXT("cargo-capital"));
            UE_LOG(LogTemp,Display,TEXT("CF_HOME_CARGO_CAPITAL PASS modules=3 cost=22 material=82"));
            if(Befriended)
            {
                // 原伙伴在手采期间正常跟随；先等它自行回到运输出发位置，不传送或冻结。
                Look(FVector(200,-800,60),FVector(600,-800,124));CargoAt=World->GetTimeSeconds();CargoPhase=3;return true;
            }
            const FTransform Transform(FVector(0,-800,60));
            auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if(!Pet)return Fail(TEXT("cargo-pet"));
            Pet->Definition=LoadObject<UCreatureDefinition>(nullptr,TEXT("/Game/Creatures/DA_Mosskip.DA_Mosskip"));Pet->FinishSpawning(Transform);Pet->Tame(Hero->GetController());Pet->Tags.AddUnique(TEXT("HomeFundingCarrier"));
            Carrier=Pet;return StartCargoOrder();
        }
        auto* Pet=Carrier.Get();auto* Box=Crate.Get();auto* Node=FundingNode(World,1);
        if(!Pet || !Box || !Node || Pet->Health<=0 || !Pet->GetActorEnableCollision())return Fail(TEXT("cargo-live"));
        if(CargoPhase==3)
        {
            if(World->GetTimeSeconds()-CargoAt>25)return Fail(TEXT("befriend-escort-timeout"));
            if(FVector::Dist2D(Pet->GetActorLocation(),Pet->GetFollowDestination())>65)return true;
            UE_LOG(LogTemp,Display,TEXT("CF_HOME_BEFRIEND_ESCORT PASS pet=%s travel=%.1f"),*Pet->PersistentId.ToString(),LifePetTravel);
            return StartCargoOrder();
        }
        const auto Now=Pet->GetActorLocation();const double Move=FVector::Dist2D(Now,Previous);Previous=Now;Travel+=Move;
        if(Move>=120)return Fail(TEXT("cargo-continuity"));
        if(CargoPhase==1)
        {
            const int32 Held=Pet->Cargo->Count(),Stored=Box->StorageInventory->Count(EFrontierItem::Material);
            SawCargo |= Held>0;
            if(Node->Remaining+Held+Stored!=3 || Hero->Inventory->Count(EFrontierItem::Material)!=82 || World->GetTimeSeconds()-CargoAt>60)return Fail(TEXT("cargo-conservation-or-timeout"));
            if(Stored!=3)return true;
            if(!SawCargo || Held!=0 || Node->Remaining!=0 || Travel<1500)return Fail(TEXT("cargo-delivery"));
            UE_LOG(LogTemp,Display,TEXT("CF_HOME_CARGO_DELIVER PASS source=0 cargo=0 stored=3 material=82 travel=%.1f pet=%s"),Travel,*Pet->PersistentId.ToString());
            Look(FVector(-800,-750,60),Box->GetActorLocation()+FVector(0,0,40));
            if(!CFInputQA::Press(Hero,EKeys::F) || Pet->Cargo->HasOrder() || !Hero->Storage->Open(Box) || !Hero->Storage->Transfer(EFrontierItem::Material,3,false))return Fail(TEXT("cargo-withdraw"));
            Hero->Storage->Close();
            if(Hero->Inventory->Count(EFrontierItem::Material)!=85 || Box->StorageInventory->Count(EFrontierItem::Material)!=0)return Fail(TEXT("cargo-withdraw-budget"));
            Look(FVector(500,-800,60),FVector(900,-800,124));CargoAt=World->GetTimeSeconds();CargoPhase=2;return true;
        }
        if(World->GetTimeSeconds()-CargoAt>15)return Fail(TEXT("cargo-park-timeout"));
        if(Pet->GetActorLocation().X<280 || FVector::Dist2D(Pet->GetActorLocation(),Pet->GetFollowDestination())>65)return true;
        Look(FVector(-450,0,40),Pet->GetActorLocation());
        if(!CFInputQA::Press(Hero,EKeys::T) || Pet->Activity!=ECreatureActivity::Rest)return Fail(TEXT("cargo-rest"));
        UE_LOG(LogTemp,Display,TEXT("CF_HOME_CARGO_WITHDRAW PASS material=85 stored=0 parked=1 pet=%s"),*Pet->PersistentId.ToString());
        CargoDone=true;return true;
    }
    bool Tick()
    {
        if(FPlatformTime::Seconds()>Deadline)return Fail(TEXT("deadline"));
        if(Befriended)
        {
            auto* Pet=Carrier.Get();
            if(!Pet || Pet->PersistentId!=OriginalPet || Pet->Health<=0 || !Pet->IsActorTickEnabled() || !Pet->GetActorEnableCollision() ||
                Pet->GetCharacterMovement()->MovementMode==MOVE_None)return Fail(TEXT("befriend-live"));
            const FVector Now=Pet->GetActorLocation();const double Move=FVector::Dist2D(Now,PreviousLifePet);PreviousLifePet=Now;LifePetTravel+=Move;
            if(Move>=120)return Fail(TEXT("befriend-continuity"));
            if(!Joined)return BefriendTick();
        }
        if(Step==6)
        {
            if(Carried && !CargoDone)return CargoTick();
            auto* Bag=Hero->Inventory.Get();
            if(Bag->Count(EFrontierItem::Material)!=(Carried?85:107) || Bag->Count(EFrontierItem::Fiber)!=13 || Bag->Count(EFrontierItem::Food)!=(Befriended?1:3))return Fail(TEXT("harvest-budget"));
            Hero->SetActorLocation(FVector(-450,0,40));
            for(int32 Index=0;Index<6;++Index)if(!Hero->CraftRecipe(TEXT("WeaveSupplies")))return Fail(TEXT("weave"));
            if(!Hero->CraftRecipe(TEXT("PlantingSeeds")) || Bag->Count(EFrontierItem::Material)!=(Carried?103:125) || Bag->Count(EFrontierItem::Seeds)!=3 ||
                Bag->Count(EFrontierItem::Fiber)!=0 || Bag->Count(EFrontierItem::Food)!=(Befriended?0:2) || !VerifyHomeFundingSources(World))return Fail(TEXT("craft-budget"));
            if(Befriended){UE_LOG(LogTemp,Display,TEXT("CF_HOME_BEFRIEND_FUNDING PASS material=103 prepaid=22 raw=103 foodCost=2 sharedFood=3 seeds=3 food=0"));}
            else if(Carried){UE_LOG(LogTemp,Display,TEXT("CF_HOME_CARGO_FUNDING PASS material=103 prepaid=22 raw=101 carried=3 seeds=3"));}
            else {UE_LOG(LogTemp,Display,TEXT("CF_HOME_FUNDING PASS material=125 seeds=3 food=2 raw=101 fiber=13 weave=6"));}
            RunHomeScenario(World,Hero,0,true);return false;
        }
        auto* Node=FundingNode(World,Ids[Step]);if(!Node || !Node->Definition)return Fail(TEXT("source-missing"));
        const int32 Need=Amounts[Step]-(Carried && Step==0?3:0)+(Befriended && Step==4?2:0);
        if(!Started)
        {
            if(Node->Remaining!=Node->Definition->Capacity || Node->Remaining<Need ||
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
        if(Taken>Need)return Fail(TEXT("over-harvest"));
        if(Taken<Need){if(!Hero->Gathering->IsGathering())return Fail(TEXT("hold-stopped"));return true;}
        if(!CFInputQA::Release(Hero,EKeys::M) || Hero->Gathering->IsGathering() ||
            World->GetTimeSeconds()-BatchAt+.1<(Need-1)*Node->Definition->HarvestInterval)return Fail(TEXT("release-or-cooldown"));
        UE_LOG(LogTemp,Display,TEXT("CF_HOME_FUND_SOURCE PASS id=%d taken=%d remaining=%d seconds=%.2f"),Ids[Step],Taken,Node->Remaining,World->GetTimeSeconds()-BatchAt);
        ++Step;Started=false;return true;
    }
};
}
bool VerifyHomeFundingSources(UWorld* World)
{
    const bool Befriended=FParse::Param(FCommandLine::Get(),TEXT("CFHomeBefriendedWork"));
    const int32 Ids[]={1,2,3,4,6,8},Remaining[]={0,0,0,0,Befriended?11:13,5};
    for(int32 Index=0;Index<6;++Index){const auto* Node=FundingNode(World,Ids[Index]);if(!Node || Node->Remaining!=Remaining[Index])return false;}
    return true;
}
void RunHomeFundingScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    if(Hero->Inventory->Count(EFrontierItem::Material)!=6 || Hero->Inventory->Count(EFrontierItem::Food)!=3 || Hero->Inventory->Count(EFrontierItem::Fiber)!=0 || Hero->Inventory->Count(EFrontierItem::Seeds)!=0)
    {UE_LOG(LogTemp,Error,TEXT("CF_HOME_FUNDING FAIL initial-budget"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    auto State=MakeShared<FHomeFundingTest>();State->World=World;State->Hero=Hero;
    State->Befriended=FParse::Param(FCommandLine::Get(),TEXT("CFHomeBefriendedWork"));State->Carried=FParse::Param(FCommandLine::Get(),TEXT("CFHomeCarriedWork"));
    if(State->Befriended)
    {
        if(!State->Carried){State->Fail(TEXT("befriend-mode"));return;}
        // 只选地图启动时即存在的温顺个体，不生成、移动或暂停待结交对象。
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->IsNetStartupActor() && !It->Tamer && It->Definition &&
            It->Definition->Form==ECreatureForm::Leaf && It->Definition->PeacefulFoodCost==3 && It->Health==It->GetMaxHealth())
        {if(State->Carrier.IsValid()){State->Fail(TEXT("befriend-ambiguous"));return;}State->Carrier=*It;}
        auto* Pet=State->Carrier.Get();if(!Pet){State->Fail(TEXT("befriend-original-missing"));return;}
        State->OriginalPet=Pet->PersistentId;State->PreviousLifePet=Pet->GetActorLocation();
        UE_LOG(LogTemp,Display,TEXT("CF_HOME_BEFRIEND_ORIGIN PASS startup=1 pet=%s actor=%s position=%s"),*Pet->PersistentId.ToString(),*Pet->GetName(),*Pet->GetActorLocation().ToString());
    }
    // 隔离无关野生AI；结交接近段真实移动，后续手采与建造仍固定玩家站位。
    for(TActorIterator<AWildEncounter> It(World);It;++It)It->SetActorTickEnabled(false);
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(*It!=State->Carrier.Get())
    {It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();}
    if(!State->Befriended)Hero->GetCharacterMovement()->DisableMovement();
    State->Deadline=FPlatformTime::Seconds()+(State->Befriended?260:State->Carried?210:135);
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float){return State->Tick();}));
}
