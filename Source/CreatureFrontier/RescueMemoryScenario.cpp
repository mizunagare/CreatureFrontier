#include "RescueMemoryScenario.h"
#include "CompanionMemoryComponent.h"
#include "CreatureCharacter.h"
#include "CFPlayerCharacter.h"
#include "WildEncounter.h"
#include "FrontierSaveGame.h"
#include "InventoryComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"

namespace CFRescueMemoryQA
{
FGuid PetId(){return FGuid(0xCF290000,0,0,1);}
ACreatureCharacter* Find(UWorld* World)
{for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->PersistentId==PetId())return *It;return nullptr;}
void Place(ACFPlayerCharacter* Hero,ACreatureCharacter* Pet,bool AtHome=false)
{
    // 这里只验证状态事务，不把夹具放置算作自然行走返家证据。
    const FVector Destination=AtHome?FVector(-450,-500,40):FVector(-2500,0,40);
    Hero->SetActorLocation(Destination+FVector(0,200,0));Pet->SetActorLocation(Destination);
    Pet->SetActorTickEnabled(false);Pet->GetCharacterMovement()->DisableMovement();
    const auto View=(Pet->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
    Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
}
}
void RunRescueMemoryScenario(UWorld* World,ACFPlayerCharacter* Hero,int32 Mode)
{
    using namespace CFRescueMemoryQA;
    AWildEncounter* Source=nullptr;
    for(TActorIterator<AWildEncounter> It(World);It;++It){It->SetActorTickEnabled(false);if(It->EncounterId==TEXT("EmberHollow"))Source=*It;}
    if(!Source || !Source->bRecordsRescueJourney){UE_LOG(LogTemp,Error,TEXT("CF_RESCUE_MEMORY FAIL source configuration"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)It->SetActorTickEnabled(false);
    bool Pass=true;
    if(Mode>=2)
    {
        const TCHAR* Slot=Mode==2?TEXT("RescueMemoryDowned"):Mode==3?TEXT("RescueMemoryRescued"):TEXT("RescueMemoryHome");
        Pass=Hero->LoadProgress(Slot);
        auto* Pet=Find(World);Pass &= Pet && Pet->Memory->GetRescueSource()==TEXT("EmberHollow") && Pet->Tamer==Hero->GetController();
        if(Pet)
        {
            const auto Expected=Mode==2?ERescueJourneyMemory::Downed:Mode==3?ERescueJourneyMemory::Rescued:ERescueJourneyMemory::Pending;
            Pass &= Pet->Memory->GetRescueMemory()==Expected && Hero->Inventory->Count(EFrontierItem::Medicine)==(Mode==2?1:0) && Pet->BondPoints==0 && Pet->Experience==0;
            Place(Hero,Pet,Mode>=3);
            for(TActorIterator<ACreatureCharacter> It(World);It;++It)
            {It->SetActorTickEnabled(false);if(It->Disposition!=ECreatureDisposition::Tamed)It->SetActorLocation(FVector(3000,-3000,40));}
            if(Mode==2){Hero->HealCompanion();Pass &= Pet->Health==40 && Pet->Memory->GetRescueMemory()==ERescueJourneyMemory::Rescued && Hero->SaveProgress(TEXT("RescueMemoryRescued"));}
            if(Mode==3){Pet->Memory->TickComponent(.25f,LEVELTICK_All,nullptr);Pass &= Pet->Memory->GetRescueMemory()==ERescueJourneyMemory::Pending && Hero->SaveProgress(TEXT("RescueMemoryHome"));}
        }
    }
    else
    {
        auto* Attacker=Source->GetResident();
        auto* Pet=World->SpawnActor<ACreatureCharacter>(FVector(-450,-500,40),FRotator::ZeroRotator);
        if(!Attacker || !Pet){UE_LOG(LogTemp,Error,TEXT("CF_RESCUE_MEMORY FAIL actors"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
        Pet->PersistentId=PetId();Pet->Tamer=Hero->GetController();Pet->Disposition=ECreatureDisposition::Tamed;Pet->SetActivity(ECreatureActivity::Rest);
        Place(Hero,Pet);Attacker->SetActorLocation(Pet->GetActorLocation()+FVector(250,0,0));Hero->Health=100;
        Hero->Inventory->Restore({{EFrontierItem::Medicine,1}});
        // 来源关闭、远程主人及无攻击者的伤害不能创建经历。
        Source->bRecordsRescueJourney=false;
        UGameplayStatics::ApplyDamage(Pet,10000,nullptr,Attacker,nullptr);Pass &= Pet->Health==0 && Pet->Memory->GetRescueMemory()==ERescueJourneyMemory::None;
        Source->bRecordsRescueJourney=true;Pet->Health=100;Hero->SetActorLocation(FVector(2000,2000,40));
        UGameplayStatics::ApplyDamage(Pet,10000,nullptr,Attacker,nullptr);Pass &= Pet->Memory->GetRescueMemory()==ERescueJourneyMemory::None;
        Place(Hero,Pet);Pet->Health=100;UGameplayStatics::ApplyDamage(Pet,10000,nullptr,nullptr,nullptr);Pass &= Pet->Memory->GetRescueMemory()==ERescueJourneyMemory::None;
        Pet->Tamer=nullptr;Pet->Health=100;UGameplayStatics::ApplyDamage(Pet,10000,nullptr,Attacker,nullptr);Pass &= Pet->Memory->GetRescueMemory()==ERescueJourneyMemory::None;Pet->Tamer=Hero->GetController();
        UGameplayStatics::SetGamePaused(World,true);Pet->Health=100;UGameplayStatics::ApplyDamage(Pet,10000,nullptr,Attacker,nullptr);Pass &= Pet->Memory->GetRescueMemory()==ERescueJourneyMemory::None;UGameplayStatics::SetGamePaused(World,false);
        Pet->Health=100;UGameplayStatics::ApplyDamage(Pet,10000,nullptr,Attacker,nullptr);
        Pass &= Pet->Health==0 && Pet->Memory->GetRescueMemory()==ERescueJourneyMemory::Downed && Pet->Memory->GetRescueSource()==Source->EncounterId;
        Pass &= Hero->SaveProgress(TEXT("RescueMemoryDowned"));
        if(Mode==0)
        {
            // 世界暂停必须同时阻止扣药、回血和经历推进，不能只靠UI屏蔽输入。
            UGameplayStatics::SetGamePaused(World,true);Hero->HealCompanion();
            const bool PausedHeal=Pet->Health==0 && Hero->Inventory->Count(EFrontierItem::Medicine)==1 && Pet->Memory->GetRescueMemory()==ERescueJourneyMemory::Downed;
            UE_LOG(LogTemp,Display,TEXT("CF_RESCUE_PAUSED_HEAL %s hp=%.1f medicine=%d state=%d"),PausedHeal?TEXT("PASS"):TEXT("FAIL"),Pet->Health,Hero->Inventory->Count(EFrontierItem::Medicine),int32(Pet->Memory->GetRescueMemory()));
            Pass &= PausedHeal;UGameplayStatics::SetGamePaused(World,false);
            // 恢复后续用药夹具，不抹掉上面失败的结果。
            Pet->Health=0;Hero->Inventory->Restore({{EFrontierItem::Medicine,1}});
            // 药品失败不能推进，成功只消耗一药；救助后重复治疗不重置经历。
            Hero->Inventory->Consume(EFrontierItem::Medicine,1);Hero->HealCompanion();Pass &= Pet->Health==0 && Pet->Memory->GetRescueMemory()==ERescueJourneyMemory::Downed;
            Hero->Inventory->Restore({{EFrontierItem::Medicine,1}});Hero->HealCompanion();
            Pass &= Pet->Health==40 && Pet->Memory->GetRescueMemory()==ERescueJourneyMemory::Rescued && Hero->Inventory->Count(EFrontierItem::Medicine)==0;
            Hero->HealCompanion();Pass &= Pet->Health==40 && Pet->Memory->GetRescueMemory()==ERescueJourneyMemory::Rescued;
            Attacker->SetActorLocation(FVector(3000,-3000,40));
            Place(Hero,Pet,true);
            UGameplayStatics::SetGamePaused(World,true);Pet->Memory->TickComponent(.25f,LEVELTICK_All,nullptr);Pass &= Pet->Memory->GetRescueMemory()==ERescueJourneyMemory::Rescued;UGameplayStatics::SetGamePaused(World,false);
            Pet->Memory->TickComponent(.25f,LEVELTICK_All,nullptr);Pass &= Pet->Memory->GetRescueMemory()==ERescueJourneyMemory::Pending && Pet->BondPoints==0 && Pet->Experience==0;
            Pass &= Hero->SaveProgress(TEXT("RescueMemoryHome"));
            for(int32 Case=0;Case<4;++Case)
            {
                auto* Bad=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("RescueMemoryHome"),0));
                auto* Record=Bad?Bad->Creatures.FindByPredicate([](const FCreatureSaveRecord& R){return R.Id==PetId();}):nullptr;
                if(!Record){Pass=false;break;}
                if(Case==0)Record->RescueJourney=255;
                if(Case==1)Record->RescueSource=TEXT("UnknownSource");
                if(Case==2)Record->RescueJourney=0;
                if(Case==3){Record->bOwned=false;Record->Disposition=ECreatureDisposition::Wild;Record->Preference=ECompanionPreference::None;Record->bPreferenceDiscovered=false;}
                Pass &= UGameplayStatics::SaveGameToSlot(Bad,TEXT("RescueMemoryBad"),0) && !Hero->LoadProgress(TEXT("RescueMemoryBad")) && Find(World)==Pet && Pet->Memory->GetRescueMemory()==ERescueJourneyMemory::Pending;
            }
            auto* Legacy=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("RescueMemoryHome"),0));
            if(Legacy){Legacy->FormatVersion=28;for(auto& R:Legacy->Creatures){R.RescueJourney=255;R.RescueSource=TEXT("UnknownSource");}Pass &= UGameplayStatics::SaveGameToSlot(Legacy,TEXT("RescueMemoryLegacy28"),0) && Hero->LoadProgress(TEXT("RescueMemoryLegacy28"));Pet=Find(World);Pass &= Pet && Pet->Memory->GetRescueMemory()==ERescueJourneyMemory::None && Pet->Memory->GetRescueSource().IsNone();}else Pass=false;
        }
    }
    UE_LOG(LogTemp,Display,TEXT("CF_RESCUE_MEMORY %s mode=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Mode);
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}
