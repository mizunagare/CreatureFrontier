#include "HollowScenario.h"
#include "CFPlayerCharacter.h"
#include "CFHUD.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "WildEncounter.h"
#include "WorldLoot.h"
#include "ResourceNode.h"
#include "InventoryComponent.h"
#include "ResearchComponent.h"
#include "HomeScenario.h"
#include "InputRoutingQA.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Controller.h"
#include "EngineUtils.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformMisc.h"
#include "Algo/Reverse.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

namespace
{
AResourceNode* HollowCache(UWorld* World)
{for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,11))return *It;return nullptr;}
AWildEncounter* HollowCamp(UWorld* World)
{for(TActorIterator<AWildEncounter> It(World);It;++It)if(It->EncounterId==TEXT("EmberHollow"))return *It;return nullptr;}
ACreatureCharacter* AdventurePet(UWorld* World,ACFPlayerCharacter* Hero)
{for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController())return *It;return nullptr;}
bool AdventureSnapshot(UWorld* World,ACFPlayerCharacter* Hero)
{
    auto* Cache=HollowCache(World);auto* Camp=HollowCamp(World);auto* Pet=AdventurePet(World,Hero);
    int32 Owned=0;for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController())++Owned;
    return Cache && Cache->Remaining==0 && Cache->RegrowRemaining==0 && Camp && !Camp->GetResident() && Camp->GetSavedRemaining()>0 &&
        Owned==1 && Pet && Pet->PersistentId.IsValid() && Pet->Definition && Pet->Definition->Form==ECreatureForm::Stone && Pet->Health>0 && Pet->Level==2 && Pet->Experience==50 &&
        Hero->Health>0 && Hero->Inventory->Count(EFrontierItem::Material)==6 && Hero->Inventory->Count(EFrontierItem::Food)==9 &&
        Hero->Inventory->Count(EFrontierItem::Scrap)==4 && Hero->Inventory->Count(EFrontierItem::Crystal)==0 && Hero->Inventory->Count(EFrontierItem::ResonanceDevice)==2;
}
struct FAdventure
{
    UWorld* World;ACFPlayerCharacter* Hero;ACreatureCharacter* Pet;ACreatureCharacter* Enemy;AWildEncounter* Camp;UHomeMovementProbe* Input;
    TArray<FVector> Points;int32 Index=0,Harvested=0,Mode=0;bool Battle=false,Defeated=false,LootDone=false,Returning=false,Skill=false,Incoming=false;
    FVector Previous,PreviousPet;float Travel=0,PetSouth=0;double Deadline=0;
    FString Shots;TSet<FString> Taken;double FinishAfter=0;
    void Shot(const TCHAR* Name)
    {
        if(Shots.IsEmpty() || Taken.Contains(Name))return;
        auto* PC=Cast<APlayerController>(Hero->GetController());auto* HUD=PC?Cast<ACFHUD>(PC->GetHUD()):nullptr;
        if(!HUD || !HUD->GetLayoutAudit().bFits || !HUD->GetLayoutAudit().bNoOverlap || !HUD->GetLayoutAudit().bCenterClear)return;
        Taken.Add(Name);FScreenshotRequest::RequestScreenshot(Shots/(FString(Name)+TEXT(".png")),true,false);
        UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_ADVENTURE_SHOT %s viewport=%.0fx%.0f frame=%llu"),Name,HUD->GetLayoutAudit().Viewport.X,HUD->GetLayoutAudit().Viewport.Y,HUD->GetLayoutAudit().Frame);
    }
    bool Fail(const TCHAR* Reason)
    {UE_LOG(LogTemp,Error,TEXT("CF_HOLLOW_ADVENTURE FAIL %s index=%d return=%d hero=%s hp=%.1f pet=%s hp=%.1f enemy=%s hp=%.1f skill=%d incoming=%d"),Reason,Index,Returning,*Hero->GetActorLocation().ToString(),Hero->Health,*Pet->GetActorLocation().ToString(),Pet->Health,*Enemy->GetActorLocation().ToString(),Enemy->Health,Skill,Incoming);FPlatformMisc::RequestExitWithStatus(false,1);return false;}
    void Drive(const FVector& Goal)
    {const auto Delta=Goal-Hero->GetActorLocation();Input->Direction=FVector(Delta.X,Delta.Y,0).GetSafeNormal()*.32f;Hero->SetActorRotation(Input->Direction.Rotation());Hero->GetController()->SetControlRotation(Input->Direction.Rotation());}
    bool Tick()
    {
        const FVector Now=Hero->GetActorLocation(),PetNow=Pet->GetActorLocation();
        const float Step=FVector::Dist(Now,Previous),PetStep=FVector::Dist(PetNow,PreviousPet);Travel+=FVector::Dist2D(Now,Previous);Previous=Now;PreviousPet=PetNow;PetSouth=FMath::Min(PetSouth,PetNow.Y);
        Incoming |= Hero->Health<100 || Pet->Health<Pet->GetMaxHealth();Skill |= Pet->SpecialCooldownRemaining>0 && Pet->Stamina<=80;
        if(!Returning && Now.Y<-2200)Shot(TEXT("Entrance"));
        if(Pet->SpecialWindupRemaining>0)Shot(TEXT("Skill"));
        if(FPlatformTime::Seconds()>Deadline || Step>160 || PetStep>160 || Now.Z<0 || PetNow.Z<-10 || Hero->Health<=0 || Pet->Health<=0 ||
            Hero->GetCapsuleComponent()->GetCollisionEnabled()==ECollisionEnabled::NoCollision || Pet->GetCapsuleComponent()->GetCollisionEnabled()==ECollisionEnabled::NoCollision ||
            !Pet->IsActorTickEnabled() || !Enemy->IsActorTickEnabled() || !Camp->IsActorTickEnabled())return Fail(TEXT("continuous/alive/deadline"));
        if(!Defeated && Enemy->Disposition==ECreatureDisposition::Hostile && Now.Y<-2700)Battle=true;
        if(Battle && !Defeated)
        {
            Input->Direction=FVector::ZeroVector;
            if(Enemy->Health>0)
            {
                const auto View=(Enemy->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
                if(Hero->TraceCreature()==Enemy)
                {if(Pet->FocusTarget!=Enemy)CFInputQA::Press(Hero,EKeys::Q);if(!Skill)CFInputQA::Press(Hero,EKeys::G);}
                return true;
            }
            if(!Skill || !Incoming)return Fail(TEXT("combat-evidence"));
            Defeated=true;UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_ADVENTURE_BATTLE PASS heroHP=%.1f petHP=%.1f skill=1 incoming=1"),Hero->Health,Pet->Health);
        }
        if(Defeated && !LootDone)
        {
            const int32 Bag=Hero->Inventory->Count(EFrontierItem::Material);int32 OnGround=0;AWorldLoot* Target=nullptr;
            for(TActorIterator<AWorldLoot> It(World);It;++It){OnGround+=It->Materials;if(It->Materials>0)Target=*It;}
            if(Bag+OnGround!=6)return Fail(TEXT("loot-conservation"));
            if(Bag==6)LootDone=true;
            else if(Target){if(FVector::Dist2D(Now,Target->GetActorLocation())>170)Drive(Target->GetActorLocation());else Input->Direction=FVector::ZeroVector;return true;}
            else return Fail(TEXT("loot-missing"));
        }
        if(Index<Points.Num())
        {if(FVector::Dist2D(Now,Points[Index])<70){++Index;Input->Direction=FVector::ZeroVector;}else Drive(Points[Index]);return true;}
        Input->Direction=FVector::ZeroVector;
        if(!Returning)
        {
            if(!Defeated || !LootDone)return Fail(TEXT("bypassed-combat"));
            auto* Cache=HollowCache(World);if(!Cache)return Fail(TEXT("cache"));
            if(Harvested<6)
            {
                if(Cache->HarvestCooldown>0)return true;
                const auto View=(Cache->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
                const int32 Before=Hero->Inventory->Count(EFrontierItem::Scrap);
                if(!CFInputQA::Press(Hero,EKeys::M) || Hero->Inventory->Count(EFrontierItem::Scrap)!=Before+1)return Fail(TEXT("harvest"));
                ++Harvested;return true;
            }
            if(PetSouth>-3600 || Pet->GetDistanceTo(Hero)>330)return true;
            Shot(TEXT("Cache"));
            Algo::Reverse(Points);Points.Add(FVector(-250,-400,40));Index=0;Returning=true;return true;
        }
        if(Pet->GetDistanceTo(Hero)>300)return true;
        if(!Shots.IsEmpty())
        {
            if(FinishAfter==0){Shot(TEXT("Home"));FinishAfter=FPlatformTime::Seconds()+.6;return true;}
            if(FPlatformTime::Seconds()<FinishAfter)return true;
        }
        const FGuid Id=Pet->PersistentId;
        bool Pass=Travel>7000 && PetSouth<-3600 && Hero->CraftRecipe(TEXT("ScrapResonator")) && AdventureSnapshot(World,Hero);
        Pass &= Hero->SaveProgress(TEXT("HollowAdventureCross")) && Hero->LoadProgress(TEXT("HollowAdventureCross")) && AdventureSnapshot(World,Hero);
        auto* Restored=AdventurePet(World,Hero);Pass &= Restored && Restored->PersistentId==Id;
        UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_ADVENTURE%s %s travel=%.1f petSouth=%.1f harvested=%d skill=%d incoming=%d"),Mode==1?TEXT("_WRITE"):TEXT(""),Pass?TEXT("PASS"):TEXT("FAIL"),Travel,PetSouth,Harvested,Skill,Incoming);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
};
}
void RunHollowAdventure(UWorld* World,ACFPlayerCharacter* Hero,int Mode)
{
    if(Mode==2)
    {const bool Pass=Hero->LoadProgress(TEXT("HollowAdventureCross")) && AdventureSnapshot(World,Hero);UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_ADVENTURE_READ %s"),Pass?TEXT("PASS"):TEXT("FAIL"));FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return;}
    auto* Camp=HollowCamp(World);auto* Enemy=Camp?Camp->GetResident():nullptr;auto* Stone=LoadObject<UCreatureDefinition>(nullptr,TEXT("/Game/Creatures/DA_Craglet.DA_Craglet"));
    if(!Camp || !Enemy || !Stone || Camp->bBoss || Enemy->Level!=3 || !Camp->IsActorTickEnabled() || AdventurePet(World,Hero))
    {UE_LOG(LogTemp,Error,TEXT("CF_HOLLOW_ADVENTURE FAIL natural-population"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    // 仅设置出发夹具；途中保留所有自然住民，不造敌人、不直接扣血或发奖励。
    Hero->SetActorLocation(FVector(-250,-400,40));Hero->Inventory->Restore({{EFrontierItem::Material,4},{EFrontierItem::Crystal,1},{EFrontierItem::Food,9}});
    FResearchProgress Study;Study.bCrystalSample=true;Study.bCrystalStudy=true;Hero->Research->Restore(Study);
    const FTransform At(Hero->GetActorLocation()+FVector(0,180,0));auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),At);
    if(!Pet){FPlatformMisc::RequestExitWithStatus(false,1);return;}Pet->Definition=Stone;Pet->FinishSpawning(At);Pet->Tame(Hero->GetController());
    auto State=MakeShared<FAdventure>();State->World=World;State->Hero=Hero;State->Pet=Pet;State->Enemy=Enemy;State->Camp=Camp;State->Mode=Mode;
    State->Input=NewObject<UHomeMovementProbe>(Hero);State->Input->RegisterComponent();State->Points=MakeHollowRoutePoints();State->Previous=Hero->GetActorLocation();State->PreviousPet=Pet->GetActorLocation();State->Deadline=FPlatformTime::Seconds()+75;
    if(FParse::Param(FCommandLine::Get(),TEXT("CFHollowAdventurePreview")))
    {State->Shots=FPaths::ProjectSavedDir()/TEXT("HollowAdventurePreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits);IFileManager::Get().MakeDirectory(*State->Shots,true);UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_ADVENTURE_ROOT %s"),*State->Shots);}
    UE_LOG(LogTemp,Display,TEXT("CF_HOLLOW_ADVENTURE_START resident=%s hp=%.1f petHP=%.1f"),*Enemy->GetActorLocation().ToString(),Enemy->Health,Pet->Health);
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float){return State->Tick();}),.1f);
}
