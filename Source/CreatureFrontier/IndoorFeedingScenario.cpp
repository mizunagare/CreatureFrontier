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
#include "FarmCropComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Containers/Ticker.h"
#include "HomeLayout.h"
#include "CompanionCargoComponent.h"
#include "GameFramework/Controller.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "HomeScenario.h"

// 来自真实送粮完成档的自然生活观察；不改饱食、时间倍率、任务或碰撞。
static void RunHomeLifeScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    struct FLife
    {
        UWorld* World;ACFPlayerCharacter* Hero;
        TWeakObjectPtr<ACreatureCharacter> Farmer,Courier;TWeakObjectPtr<AWorkStation> Farm,Post,Trough;
        TMap<EFrontierItem,int32> Bag;TMap<FGuid,int32> Resources;TMap<FGuid,FTransform> Facilities;
        TArray<FHomeModuleRecord> House;FVector HeroPosition;float Health=0,FarmerNutrition=0,CourierNutrition=0;
        int32 FarmerMeals=0,CourierMeals=0;bool Rested=false,Resumed=false,Mid=false;double Start=0,Deadline=0,NextLog=0;
        int32 InitialStock=4;bool Resume=false,Recovery=false;
        bool Preview=false;FString ImageRoot;TSet<FString> Photos;double LastPhoto=0;
        TWeakObjectPtr<UHomeMovementProbe> ObserverInput;
        FVector LastHero;double LastTick=0;int32 WalkPoint=0;bool Outside=false,Walking=false,WalkOut=false;
        void Observe()
        {
            if(!Preview || !ObserverInput.IsValid())return;
            const bool WantOutside=Photos.Contains(TEXT("FarmerMeal")) && (!Photos.Contains(TEXT("Rest")) || Photos.Contains(TEXT("CourierMeal")));
            if(!Walking && WantOutside!=Outside && FPlatformTime::Seconds()-LastPhoto>.4){Walking=true;WalkOut=WantOutside;WalkPoint=0;}
            ObserverInput->Direction=FVector::ZeroVector;
            if(!Walking)return;
            // 沿屋外西侧绕开门扇及制作台；原移动组件执行碰撞和加减速。
            const TArray<FVector> Route=WalkOut?TArray<FVector>{FVector(-900,HeroPosition.Y,0),FVector(-900,-1080,0),FVector(-280,-1080,0)}:TArray<FVector>{FVector(-900,-1080,0),FVector(-900,HeroPosition.Y,0),FVector(HeroPosition.X,HeroPosition.Y,0)};
            FVector Delta=Route[WalkPoint]-Hero->GetActorLocation();Delta.Z=0;
            if(Delta.Size2D()<25){if(++WalkPoint==Route.Num()){Walking=false;Outside=WalkOut;UE_LOG(LogTemp,Display,TEXT("CF_HOME_LIFE_OBSERVER outside=%d pos=%s"),Outside,*Hero->GetActorLocation().ToString());}return;}
            ObserverInput->Direction=Delta.GetSafeNormal()*FMath::Clamp(Delta.Size2D()/160.f,.1f,1.f);
        }
        void Photo(const TCHAR* Name,ACreatureCharacter* Subject)
        {
            if(!Preview || Photos.Contains(Name) || !Subject || Walking)return;
            const bool WorkPhoto=FCString::Strcmp(Name,TEXT("Rest"))==0 || FCString::Strcmp(Name,TEXT("Returned"))==0;
            if(WorkPhoto!=Outside)return;
            Photos.Add(Name);
            // 实际行走到观察位后只转动第一人称视角；不移动伙伴，不暂停模拟。
            const auto Rotation=(Subject->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
            Hero->GetController()->SetControlRotation(Rotation);Hero->FirstPersonCamera->SetWorldRotation(Rotation);
            FScreenshotRequest::RequestScreenshot(ImageRoot/(FString(Name)+TEXT(".png")),false,false);
            LastPhoto=FPlatformTime::Seconds();
        }
        bool End(bool Pass,const TCHAR* Why)
        {
            if(ObserverInput.IsValid())ObserverInput->Direction=FVector::ZeroVector;
            const bool Saved=Hero->SaveProgress(Pass?TEXT("HomeLifeDone"):TEXT("HomeLifeBlocked"));
            UE_LOG(LogTemp,Display,TEXT("CF_HOME_LIFE %s reason=%s farmerMeals=%d courierMeals=%d rested=%d resumed=%d stock=%d elapsed=%.3f"),Pass && Saved?TEXT("PASS"):TEXT("FAIL"),Why,FarmerMeals,CourierMeals,Rested,Resumed,Trough.IsValid()?Trough->StoredFood:-1,World->GetTimeSeconds()-Start);
            FPlatformMisc::RequestExitWithStatus(false,Pass && Saved?0:1);return false;
        }
        bool Tick()
        {
            const double SampleTime=World->GetTimeSeconds();
            if(LastTick>0 && FVector::Dist(Hero->GetActorLocation(),LastHero)>Hero->GetCharacterMovement()->GetMaxSpeed()*(SampleTime-LastTick)+10)return End(false,TEXT("observer-continuity"));
            LastHero=Hero->GetActorLocation();LastTick=SampleTime;Observe();
            if(!Farmer.IsValid() || !Courier.IsValid() || !Farm.IsValid() || !Post.IsValid() || !Trough.IsValid())return End(false,TEXT("identity"));
            if(Farmer->Nutrition-FarmerNutrition>20){++FarmerMeals;UE_LOG(LogTemp,Display,TEXT("CF_HOME_LIFE_MEAL farmer=1 before=%.3f after=%.3f pos=%s"),FarmerNutrition,Farmer->Nutrition,*Farmer->GetActorLocation().ToString());}
            if(Courier->Nutrition-CourierNutrition>20){++CourierMeals;UE_LOG(LogTemp,Display,TEXT("CF_HOME_LIFE_MEAL courier=1 before=%.3f after=%.3f pos=%s"),CourierNutrition,Courier->Nutrition,*Courier->GetActorLocation().ToString());}
            FarmerNutrition=Farmer->Nutrition;CourierNutrition=Courier->Nutrition;
            if(FarmerMeals>0)Photo(TEXT("FarmerMeal"),Farmer.Get());
            if(CourierMeals>0)Photo(TEXT("CourierMeal"),Courier.Get());
            if(Courier->Activity==ECreatureActivity::Rest && Courier->bResumeWorkAfterRest && Courier->GetDistanceTo(Post.Get())<=220)Photo(TEXT("Rest"),Courier.Get());
            if(Courier->Activity==ECreatureActivity::Rest && Courier->bResumeWorkAfterRest)Rested=true;
            if(Rested && Courier->Activity==ECreatureActivity::Work && !Courier->bResumeWorkAfterRest)Resumed=true;
            if(Hero->Health!=Health || (!Preview && !Hero->GetActorLocation().Equals(HeroPosition,.1)) || !Hero->Inventory->GetStacks().OrderIndependentCompareEqual(Bag) || Farmer->Health!=110 || Courier->Health!=80 || Farmer->Experience!=44 || Courier->Experience!=20 || Farmer->WorkSite!=Farm.Get() || Courier->WorkSite!=Post.Get() || Farmer->Activity!=ECreatureActivity::Work || (Courier->Activity!=ECreatureActivity::Work && Courier->Activity!=ECreatureActivity::Rest) || Farmer->CarriedFood || Courier->CarriedFood || Farmer->Cargo->Count() || Courier->Cargo->Count() || Farm->StoredFood || Farm->Crop->IsOccupied() || Trough->StoredFood+FarmerMeals+CourierMeals!=InitialStock)return End(false,TEXT("budget"));
            int32 Count=0;for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt){++Count;const auto* Pose=Facilities.Find(It->PersistentId);if(!Pose || !It->GetActorTransform().Equals(*Pose,.01))return End(false,TEXT("facilities"));}if(Count!=4)return End(false,TEXT("facility-count"));
            for(TActorIterator<AResourceNode> It(World);It;++It){const auto* Before=Resources.Find(It->PersistentId);if(!Before || It->Remaining!=*Before)return End(false,TEXT("resources"));}
            const auto Layout=HomeLayout::Collect(World);if(Layout.Num()!=House.Num())return End(false,TEXT("house-count"));
            for(const auto& R:House){const auto* Now=Layout.FindByPredicate([&](const FHomeModuleRecord& V){return V.Id==R.Id;});if(!Now || Now->Definition!=R.Definition || !Now->Transform.Equals(R.Transform,.01) || !Now->StoredItems.OrderIndependentCompareEqual(R.StoredItems))return End(false,TEXT("house"));}
            for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Health>0 && (!It->IsActorTickEnabled() || !It->GetActorEnableCollision() || It->GetCharacterMovement()->MovementMode==MOVE_None))return End(false,TEXT("ecology"));
            if(!Mid && FarmerMeals+CourierMeals>0){Mid=true;if(!Hero->SaveProgress(TEXT("HomeLifeMid")))return End(false,TEXT("mid-save"));}
            if((FarmerMeals>0 || Resume || Recovery) && CourierMeals>0 && Resumed && Farmer->Activity==ECreatureActivity::Work && Courier->Activity==ECreatureActivity::Work && !Farmer->FeedingSite.IsValid() && !Courier->FeedingSite.IsValid() && Farmer->GetDistanceTo(Farm.Get())<=220 && Courier->GetDistanceTo(Post.Get())<=220)
            {
                Photo(TEXT("Returned"),Courier.Get());
                if(!Preview || (Photos.Contains(TEXT("Returned")) && FPlatformTime::Seconds()-LastPhoto>.3))return End(true,Recovery?TEXT("failed-state-fed-rest-return"):Resume?TEXT("mid-state-fed-rest-return"):TEXT("natural-meals-rest-return"));
            }
            if(FPlatformTime::Seconds()>Deadline)return End(false,TEXT("timeout"));
            if(World->GetTimeSeconds()>=NextLog){NextLog=World->GetTimeSeconds()+15;UE_LOG(LogTemp,Display,TEXT("CF_HOME_LIFE_STATE farmerNutrition=%.3f courierNutrition=%.3f stamina=%.3f rest=%d resume=%d stock=%d"),FarmerNutrition,CourierNutrition,Courier->Stamina,Rested,Resumed,Trough->StoredFood);}
            return true;
        }
    };
    auto S=MakeShared<FLife>();S->World=World;S->Hero=Hero;
    S->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFHomeLifePreview"));
    if(S->Preview){S->ImageRoot=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("HomeLifePreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*S->ImageRoot,true);UE_LOG(LogTemp,Display,TEXT("CF_HOME_LIFE_PREVIEW_ROOT %s"),*S->ImageRoot);}
    S->Resume=FParse::Param(FCommandLine::Get(),TEXT("CFHomeLifeRead"));S->Recovery=FParse::Param(FCommandLine::Get(),TEXT("CFHomeLifeRecovery"));
    S->InitialStock=S->Recovery?2:S->Resume?3:4;
    if(!Hero->LoadProgress(TEXT("HomeLifeInput"))){S->End(false,TEXT("source"));return;}
    S->Bag=Hero->Inventory->GetStacks();S->Health=Hero->Health;S->HeroPosition=Hero->GetActorLocation();S->House=HomeLayout::Collect(World);
    if(S->Preview){auto* Input=NewObject<UHomeMovementProbe>(Hero);Input->RegisterComponent();S->ObserverInput=Input;}
    for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt){S->Facilities.Add(It->PersistentId,It->GetActorTransform());if(It->RequiredSpecialty==EWorkSpecialty::Farming)S->Farm=*It;if(It->RequiredSpecialty==EWorkSpecialty::Transport)S->Post=*It;if(It->RequiredSpecialty==EWorkSpecialty::Feeding)S->Trough=*It;}
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController() && It->Definition){if(It->Definition->Form==ECreatureForm::Leaf)S->Farmer=*It;if(It->Definition->Form==ECreatureForm::Ember)S->Courier=*It;}
    for(TActorIterator<AResourceNode> It(World);It;++It)S->Resources.Add(It->PersistentId,It->Remaining);
    if(!S->Farmer.IsValid() || !S->Courier.IsValid() || !S->Trough.IsValid() || S->Trough->StoredFood!=S->InitialStock || (S->Resume && S->Recovery) || (S->Recovery && (S->Courier->Nutrition>20 || S->Courier->Activity!=ECreatureActivity::Rest || !S->Courier->bResumeWorkAfterRest))){S->End(false,TEXT("source-state"));return;}
    UE_LOG(LogTemp,Display,TEXT("CF_HOME_LIFE_SOURCE resume=%d recovery=%d stock=%d farmerNutrition=%.3f courierNutrition=%.3f"),S->Resume,S->Recovery,S->InitialStock,S->Farmer->Nutrition,S->Courier->Nutrition);
    S->FarmerNutrition=S->Farmer->Nutrition;S->CourierNutrition=S->Courier->Nutrition;S->Start=World->GetTimeSeconds();S->Deadline=FPlatformTime::Seconds()+180;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float){return S->Tick();}));
}

void RunIndoorFeedingScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    if(FParse::Param(FCommandLine::Get(),TEXT("CFHomeLifeTest"))){RunHomeLifeScenario(World,Hero);return;}
    if(FParse::Param(FCommandLine::Get(),TEXT("CFPantryInspect")))
    {
        if(!Hero->LoadProgress(TEXT("IndoorFeedingInput"))){UE_LOG(LogTemp,Error,TEXT("CF_PANTRY_INSPECT FAIL source"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
        UE_LOG(LogTemp,Display,TEXT("CF_PANTRY_SOURCE hero=%s material=%d food=%d medicine=%d"),*Hero->GetActorLocation().ToString(),Hero->Inventory->Count(EFrontierItem::Material),Hero->Inventory->Count(EFrontierItem::Food),Hero->Inventory->Count(EFrontierItem::Medicine));
        UE_LOG(LogTemp,Display,TEXT("CF_PANTRY_HEALTH value=%.3f"),Hero->Health);
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(!It->Tamer)
            UE_LOG(LogTemp,Display,TEXT("CF_PANTRY_WILD id=%s name=%s health=%.3f pos=%s home=%s habitat=%d leash=%.1f activity=%d speed=%.1f distance=%.1f"),*It->PersistentId.ToString(),*It->GetCreatureName(),It->Health,*It->GetActorLocation().ToString(),*It->HabitatHome.ToString(),It->bHasHabitat,It->LeashRadius,static_cast<int32>(It->Activity),It->GetCharacterMovement()->GetMaxSpeed(),It->GetDistanceTo(Hero));
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController())UE_LOG(LogTemp,Display,TEXT("CF_PANTRY_PET id=%s nutrition=%.3f stamina=%.3f xp=%d activity=%d pos=%s"),*It->PersistentId.ToString(),It->Nutrition,It->Stamina,It->Experience,static_cast<int32>(It->Activity),*It->GetActorLocation().ToString());
        for(TActorIterator<AResourceNode> It(World);It;++It)UE_LOG(LogTemp,Display,TEXT("CF_PANTRY_RESOURCE id=%s definition=%s stock=%d pos=%s output=%d interval=%.3f"),*It->PersistentId.ToString(),*GetNameSafe(It->Definition.Get()),It->Remaining,*It->GetActorLocation().ToString(),It->Definition?static_cast<int32>(It->Definition->OutputItem):-1,It->Definition?It->Definition->HarvestInterval:0.f);
        // 只读来源核验需要区分现有库存与尚未收获的作物，不能由旧测试结果推算。
        for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt)
            UE_LOG(LogTemp,Display,TEXT("CF_PANTRY_FACILITY id=%s type=%d food=%d seeds=%d planted=%d growth=%.3f pos=%s"),*It->PersistentId.ToString(),static_cast<int32>(It->RequiredSpecialty),It->StoredFood,It->Crop->Seeds,It->Crop->bPlanted,It->Crop->GrowthWork,*It->GetActorLocation().ToString());
        if(FParse::Param(FCommandLine::Get(),TEXT("CFPantryReturnInspect"))){FTimerHandle Observe;World->GetTimerManager().SetTimer(Observe,[World,Hero](){for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController())UE_LOG(LogTemp,Display,TEXT("CF_PANTRY_OBSERVED id=%s nutrition=%.3f stamina=%.3f activity=%d pos=%s feeding=%d cooldown=%.3f resume=%d"),*It->PersistentId.ToString(),It->Nutrition,It->Stamina,static_cast<int32>(It->Activity),*It->GetActorLocation().ToString(),It->FeedingSite.IsValid(),It->AutoFeedCooldown,It->bResumeWorkAfterRest);UE_LOG(LogTemp,Display,TEXT("CF_PANTRY_INSPECT PASS noSave=1 noGrant=1"));FPlatformMisc::RequestExitWithStatus(false,0);},5.f,false);return;}
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
