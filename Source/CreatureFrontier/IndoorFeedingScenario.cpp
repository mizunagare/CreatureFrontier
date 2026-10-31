#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "WorkStation.h"
#include "HomeModule.h"
#include "FrontierSaveGame.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "ResourceNode.h"
#include "ResourceDefinition.h"
#include "InventoryComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void RunIndoorFeedingScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    if(FParse::Param(FCommandLine::Get(),TEXT("CFPantryInspect")))
    {
        if(!Hero->LoadProgress(TEXT("IndoorFeedingInput"))){UE_LOG(LogTemp,Error,TEXT("CF_PANTRY_INSPECT FAIL source"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
        UE_LOG(LogTemp,Display,TEXT("CF_PANTRY_SOURCE hero=%s material=%d food=%d medicine=%d"),*Hero->GetActorLocation().ToString(),Hero->Inventory->Count(EFrontierItem::Material),Hero->Inventory->Count(EFrontierItem::Food),Hero->Inventory->Count(EFrontierItem::Medicine));
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController())UE_LOG(LogTemp,Display,TEXT("CF_PANTRY_PET id=%s nutrition=%.3f stamina=%.3f xp=%d activity=%d pos=%s"),*It->PersistentId.ToString(),It->Nutrition,It->Stamina,It->Experience,static_cast<int32>(It->Activity),*It->GetActorLocation().ToString());
        for(TActorIterator<AResourceNode> It(World);It;++It)UE_LOG(LogTemp,Display,TEXT("CF_PANTRY_RESOURCE id=%s definition=%s stock=%d pos=%s"),*It->PersistentId.ToString(),*GetNameSafe(It->Definition.Get()),It->Remaining,*It->GetActorLocation().ToString());
        if(FParse::Param(FCommandLine::Get(),TEXT("CFPantryReturnInspect"))){FTimerHandle Observe;World->GetTimerManager().SetTimer(Observe,[](){UE_LOG(LogTemp,Display,TEXT("CF_PANTRY_INSPECT PASS noSave=1 noGrant=1"));FPlatformMisc::RequestExitWithStatus(false,0);},5.f,false);return;}
        UE_LOG(LogTemp,Display,TEXT("CF_PANTRY_INSPECT PASS noSave=1 noGrant=1"));FPlatformMisc::RequestExitWithStatus(false,0);return;
    }
    // 明确的室内寻路夹具：借用真实七模块房屋，设置饥饿、食槽和起点；不作为真实生产／资源来源证据。
    if(!Hero->LoadProgress(TEXT("IndoorFeedingInput")))
    {UE_LOG(LogTemp,Error,TEXT("CF_INDOOR_FEED FAIL source"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    AWorkStation* Trough=nullptr;AWorkStation* Farm=nullptr;ACreatureCharacter* Pet=nullptr;
    for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt){if(It->RequiredSpecialty==EWorkSpecialty::Crafting)Trough=*It;if(It->RequiredSpecialty==EWorkSpecialty::Farming)Farm=*It;}
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController() && It->Definition && It->Definition->Form==ECreatureForm::Ember)Pet=*It;
    if(!Trough || !Farm || !Pet){UE_LOG(LogTemp,Error,TEXT("CF_INDOOR_FEED FAIL fixture"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    Trough->RequiredSpecialty=EWorkSpecialty::Feeding;Trough->CraftRecipeId=NAME_None;Trough->CraftWork=0;Trough->StoredFood=0;Trough->AdjustFoodStock(2);
    Pet->SetActorLocation(FVector(-550,-400,40));Pet->WorkSite=Farm;Pet->SetActivity(ECreatureActivity::Work);Pet->Nutrition=30;Pet->AutoFeedCooldown=0;Pet->bResumeWorkAfterRest=false;
    Hero->FirstPersonCamera->SetWorldRotation((FVector(-200,-400,245)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());
    auto* Door=AHomeModule::TraceDoor(Hero);if(!Door || (Door->DoorState==1 && !Door->ToggleDoor(Hero)))
    {UE_LOG(LogTemp,Error,TEXT("CF_INDOOR_FEED FAIL door feedback=%s"),*Hero->Feedback);FPlatformMisc::RequestExitWithStatus(false,1);return;}
    const FGuid PetId=Pet->PersistentId,FarmId=Farm->PersistentId;const float HeroHealth=Hero->Health;
    FTimerHandle Observe;
    World->GetTimerManager().SetTimer(Observe,[World,Hero,Pet,Trough,Farm,PetId,FarmId,HeroHealth]()
    {
        const bool Meal=Trough->StoredFood==1 && Pet->Nutrition>50;
        const bool Task=Pet->PersistentId==PetId && Pet->WorkSite==Farm && Farm->PersistentId==FarmId && Pet->Activity==ECreatureActivity::Work && Pet->GetDistanceTo(Farm)<=220;
        const bool Live=Pet->GetActorEnableCollision() && Pet->IsActorTickEnabled() && Pet->GetCharacterMovement()->MovementMode==MOVE_Walking && Hero->Health==HeroHealth;
        const bool Pass=Meal && Task && Live;
        UE_LOG(LogTemp,Display,TEXT("CF_INDOOR_FEED %s meal=%d task=%d live=%d stock=%d nutrition=%.2f pos=%s target=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Meal,Task,Live,Trough->StoredFood,Pet->Nutrition,*Pet->GetActorLocation().ToString(),*GetNameSafe(Pet->FeedingSite.Get()));
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    },20.f,false);
}
