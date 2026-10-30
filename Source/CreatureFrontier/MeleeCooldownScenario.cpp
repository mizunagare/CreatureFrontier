#include "ConstructionScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "FrontierSaveGame.h"
#include "InventoryComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include <limits>

// 只在独立专项槽中检查边界和事务拒绝；普通接敌链另行验证真实时序。
void RunMeleeCooldownScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    auto End=[](bool Pass,const TCHAR* Reason){UE_LOG(LogTemp,Display,TEXT("CF_MELEE_COOLDOWN %s reason=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Reason);FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);};
    auto* Legacy=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("CooldownInput"),0));
    if(!Legacy || Legacy->Creatures.IsEmpty() || !Legacy->Creatures.ContainsByPredicate([](const FCreatureSaveRecord& R){return R.bAlpha;}) ||
        Legacy->Creatures.ContainsByPredicate([](const FCreatureSaveRecord& R){return R.CombatCooldownVersion!=0 || R.MeleeCooldown!=0;}) || !Hero->LoadProgress(TEXT("CooldownInput"))){End(false,TEXT("legacy-source"));return;}
    TMap<FGuid,float> Intervals;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It){Intervals.Add(It->PersistentId,It->AttackInterval);if(It->GetAttackCooldownRemaining()!=It->AttackInterval){End(false,TEXT("legacy-interval"));return;}}
    if(Intervals.Num()!=Legacy->Creatures.Num() || !Hero->SaveProgress(TEXT("CooldownRoundTrip"))){End(false,TEXT("legacy-save"));return;}
    auto* Fresh=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("CooldownRoundTrip"),0));
    if(!Fresh || Fresh->Creatures.ContainsByPredicate([&](const FCreatureSaveRecord& R){return R.CombatCooldownVersion!=1 || R.MeleeCooldown!=Intervals.FindRef(R.Id);})){End(false,TEXT("new-record"));return;}
    UE_LOG(LogTemp,Display,TEXT("CF_MELEE_COOLDOWN_LEGACY PASS creatures=%d revision=1 intervalRetained=1"),Intervals.Num());
    auto Accept=[&](float Fraction)
    {
        auto* Candidate=DuplicateObject<UFrontierSaveGame>(Fresh,GetTransientPackage());
        for(auto& Record:Candidate->Creatures)Record.MeleeCooldown=Intervals.FindRef(Record.Id)*Fraction;
        if(!UGameplayStatics::SaveGameToSlot(Candidate,TEXT("CooldownBoundary"),0) || !Hero->LoadProgress(TEXT("CooldownBoundary")))return false;
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->GetAttackCooldownRemaining()!=Intervals.FindRef(It->PersistentId)*Fraction)return false;
        return true;
    };
    if(!Accept(0) || !Accept(1) || !Accept(.5f) || !Hero->SaveProgress(TEXT("CooldownRoundTrip")) || !Hero->LoadProgress(TEXT("CooldownRoundTrip"))){End(false,TEXT("boundary"));return;}
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->GetAttackCooldownRemaining()!=Intervals.FindRef(It->PersistentId)*.5f){End(false,TEXT("roundtrip"));return;}
    UE_LOG(LogTemp,Display,TEXT("CF_MELEE_COOLDOWN_BOUNDARY PASS zero=1 maximum=1 positive=1 roundtrip=1"));
    auto* Baseline=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("CooldownRoundTrip"),0));
    for(int32 Case=0;Case<6;++Case)
    {
        const auto Position=Hero->GetActorTransform();const float HP=Hero->Health;const auto Items=Hero->Inventory->GetStacks();
        TMap<ACreatureCharacter*,TPair<float,FTransform>> Actors;
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)Actors.Add(*It,TPair<float,FTransform>(It->GetAttackCooldownRemaining(),It->GetActorTransform()));
        auto* Bad=DuplicateObject<UFrontierSaveGame>(Baseline,GetTransientPackage());auto& Last=Bad->Creatures.Last();
        if(Case==0)Last.CombatCooldownVersion=2;
        if(Case==1)Last.MeleeCooldown=-.01f;
        if(Case==2)Last.MeleeCooldown=Intervals.FindRef(Last.Id)+.01f;
        if(Case==3)Last.MeleeCooldown=std::numeric_limits<float>::quiet_NaN();
        if(Case==4)Last.MeleeCooldown=std::numeric_limits<float>::infinity();
        if(Case==5){Last.CombatCooldownVersion=0;Last.MeleeCooldown=.5f;}
        if(!UGameplayStatics::SaveGameToSlot(Bad,TEXT("CooldownInvalid"),0) || Hero->LoadProgress(TEXT("CooldownInvalid")) || !Hero->GetActorTransform().Equals(Position,.001) || Hero->Health!=HP || !Hero->Inventory->GetStacks().OrderIndependentCompareEqual(Items)){End(false,TEXT("reject-transaction"));return;}
        int32 Count=0;for(TActorIterator<ACreatureCharacter> It(World);It;++It){++Count;const auto* Before=Actors.Find(*It);if(!Before || It->GetAttackCooldownRemaining()!=Before->Key || !It->GetActorTransform().Equals(Before->Value,.001)){End(false,TEXT("reject-world"));return;}}
        if(Count!=Actors.Num()){End(false,TEXT("reject-count"));return;}
    }
    UE_LOG(LogTemp,Display,TEXT("CF_MELEE_COOLDOWN_REJECT PASS cases=6 worldRetained=1"));End(true,TEXT("complete"));
}
