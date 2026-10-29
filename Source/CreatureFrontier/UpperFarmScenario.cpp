#include "UpperFarmScenario.h"
#include "HomeScenario.h"
#include "HomeModule.h"
#include "HomeLayout.h"
#include "HomeBuildingComponent.h"
#include "BaseBuildingComponent.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "CompanionCargoComponent.h"
#include "WorkStation.h"
#include "FarmCropComponent.h"
#include "FrontierSaveGame.h"
#include "InventoryComponent.h"
#include "PlayerPaceComponent.h"
#include "InputRoutingQA.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Containers/Ticker.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

namespace
{
bool EqualHouse(const TArray<FHomeModuleRecord>& Left,const TArray<FHomeModuleRecord>& Right)
{
    if(Left.Num()!=12 || Right.Num()!=Left.Num())return false;
    for(const auto& Record:Left)
    {
        const auto* Other=Right.FindByPredicate([&](const FHomeModuleRecord& Value){return Value.Id==Record.Id;});
        if(!Other || Other->FloorId!=Record.FloorId || Other->Definition!=Record.Definition || !Other->Transform.Equals(Record.Transform,.01) ||
            Other->PaidMaterials!=Record.PaidMaterials || Other->DoorState!=Record.DoorState || !Other->StoredItems.OrderIndependentCompareEqual(Record.StoredItems) ||
            !Other->PaidExtraItems.OrderIndependentCompareEqual(Record.PaidExtraItems))return false;
    }
    return true;
}
struct FUpperFarmTest
{
    UWorld* World;ACFPlayerCharacter* Hero;bool Read=false;int32 Phase=0,Point=0;
    TWeakObjectPtr<ACreatureCharacter> Pet;TWeakObjectPtr<AWorkStation> Farm;TWeakObjectPtr<UHomeMovementProbe> Input;
    FGuid PetId,FarmId;TArray<FHomeModuleRecord> House;TArray<FVector> Points;
    FVector PreviousHero,PreviousPet,Anchor;double HeroTravel=0,PetTravel=0,Deadline=0,At=0,ProgressAt=0;
    bool PetClimbed=false,PetEntered=false,HeroClimbed=false,PetDescended=false;
    void Face(FVector Target)
    {
        const auto View=(Target-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
        Hero->SetActorRotation(FRotator(0,View.Yaw,0));Hero->FirstPersonCamera->SetRelativeRotation(FRotator::ZeroRotator);
        Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
    }
    void Stop(){if(Input.IsValid())Input->Direction=FVector::ZeroVector;Hero->GetCharacterMovement()->StopMovementImmediately();Hero->ConsumeMovementInputVector();}
    bool Finish(bool Pass,const TCHAR* Reason)
    {
        Stop();if(Input.IsValid())Input->DestroyComponent();CFInputQA::Release(Hero,EKeys::LeftShift);
        UE_LOG(LogTemp,Display,TEXT("CF_UPPER_FARM %s mode=%d phase=%d reason=%s heroTravel=%.1f petTravel=%.1f pet=%s farm=%s hero=%s partner=%s feedback=%s"),
            Pass?TEXT("PASS"):TEXT("FAIL"),Read?1:0,Phase,Reason,HeroTravel,PetTravel,*PetId.ToString(),*FarmId.ToString(),*Hero->GetActorLocation().ToString(),Pet.IsValid()?*Pet->GetActorLocation().ToString():TEXT("无"),*Hero->Feedback);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    bool FindActors()
    {
        Pet.Reset();Farm.Reset();int32 Owned=0,Built=0;
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController())
        {++Owned;if(!PetId.IsValid() || It->PersistentId==PetId)Pet=*It;}
        for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt)
        {++Built;if(!FarmId.IsValid() || It->PersistentId==FarmId)Farm=*It;}
        return Owned==1 && Built==1 && Pet.IsValid() && Farm.IsValid();
    }
    bool CheckSnapshot(const FString& Slot,bool Done)
    {
        const auto* Save=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0));
        if(!Save || (Save->FormatVersion<34 || Save->FormatVersion>FrontierCurrentSaveVersion) || Save->Facilities.Num()!=1 || !EqualHouse(Save->HomeModules,HomeLayout::Collect(World)) || !EqualHouse(House,Save->HomeModules) ||
            !FindActors() || !Hero->Inventory->GetStacks().OrderIndependentCompareEqual(Save->InventoryStacks) ||
            Hero->Inventory->Count(EFrontierItem::Material)!=94 || Hero->Inventory->Count(EFrontierItem::Seeds)!=1 || Hero->Inventory->Count(EFrontierItem::Food)!=(Done?1:0))return false;
        const auto* Creature=Save->Creatures.FindByPredicate([this](const FCreatureSaveRecord& Value){return Value.bOwned && Value.Id==Pet->PersistentId;});
        const auto& Facility=Save->Facilities[0];
        return Creature && (!PetId.IsValid() || Creature->Id==PetId) && (!FarmId.IsValid() || Facility.Id==FarmId) && Farm->PersistentId==Facility.Id &&
            Pet->Activity==(Done?ECreatureActivity::Follow:ECreatureActivity::Work) && Creature->Activity==Pet->Activity &&
            (Done?!Pet->WorkSite:Pet->WorkSite==Farm.Get()) && (Done?!Creature->WorkSiteId.IsValid():Creature->WorkSiteId==Facility.Id) &&
            !Pet->GetFieldResource() && !Pet->Cargo->HasOrder() && Pet->Cargo->Count()==0 && Pet->Experience==(Done?1:0) && Creature->Experience==Pet->Experience &&
            Pet->Nickname==Creature->Nickname && Pet->Definition==Creature->Definition.Get() && Pet->GetActorTransform().Equals(Creature->Transform,.01) &&
            FMath::IsNearlyEqual(Pet->Health,Creature->Health,.01f) && FMath::IsNearlyEqual(Pet->Nutrition,Creature->Nutrition,.01f) && FMath::IsNearlyEqual(Pet->Stamina,Creature->Stamina,.01f) &&
            Hero->GetActorTransform().Equals(Save->PlayerTransform,.01) && Pet->GetActorLocation().Z<100 &&
            Farm->RequiredSpecialty==EWorkSpecialty::Farming && Facility.Specialty==Farm->RequiredSpecialty &&
            Farm->GetActorTransform().Equals(FTransform(FRotator::ZeroRotator,FVector(100,-700,325),FVector(2,1,.7)),.01) && Facility.Transform.Equals(Farm->GetActorTransform(),.01) &&
            Farm->StoredFood==0 && Facility.StoredFood==0 && Farm->Crop->Seeds==(Done?0:1) && Facility.Seeds==Farm->Crop->Seeds &&
            !Farm->Crop->bPlanted && !Facility.bPlanted && Farm->Crop->GrowthWork==0 && Facility.GrowthWork==0;
    }
    bool Init()
    {
        House=HomeLayout::Collect(World);if(House.Num()!=12 || Hero->Inventory->Count(EFrontierItem::Material)!=112)return false;
        // 建造观察者与两份种子为明确夹具，之后仅用正常放置和F2存入，不直接设定作物进度。
        Hero->SetActorLocation(FVector(-160,-700,380));Face(FVector(100,-700,290));
        if(!CFInputQA::Press(Hero,EKeys::B) || !Hero->Building->IsActive() || Hero->HomeBuilding->GetPreview() || !CFInputQA::Action(Hero,TEXT("Attack"),EKeys::LeftMouseButton) ||
            !CFInputQA::Action(Hero,TEXT("Capture"),EKeys::RightMouseButton))return false;
        for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt && It->RequiredSpecialty==EWorkSpecialty::Farming)Farm=*It;
        if(!Farm.IsValid() || !HomeLayout::SupportsFacility(Farm->GetActorTransform(),House) || Hero->Inventory->Count(EFrontierItem::Material)!=94 || !Hero->Inventory->AddItem(EFrontierItem::Seeds,2))return false;
        Face(Farm->GetActorLocation()+FVector(0,0,30));
        if(!CFInputQA::Press(Hero,EKeys::F2) || Farm->Crop->Seeds!=1 || Hero->Inventory->Count(EFrontierItem::Seeds)!=1)return false;
        FarmId=Farm->PersistentId;
        UE_LOG(LogTemp,Display,TEXT("CF_UPPER_FARM_INPUT PASS modules=12 material=94 seeds=1 reserved=1 farm=%s"),*FarmId.ToString());
        Hero->SetActorLocation(FVector(820,-700,40));Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        const FTransform Start(FVector(-1100,-800,40));auto* Partner=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),Start,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Partner)return false;
        Partner->Definition=LoadObject<UCreatureDefinition>(nullptr,TEXT("/Game/Creatures/DA_Mosskip.DA_Mosskip"));Partner->FinishSpawning(Start);Partner->Tame(Hero->GetController());Pet=Partner;PetId=Partner->PersistentId;
        Face(Farm->GetActorLocation()+FVector(80,0,33));
        if(!CFInputQA::Press(Hero,EKeys::E) || Partner->WorkSite!=Farm.Get() || Partner->Activity!=ECreatureActivity::Work || !Hero->Feedback.Contains(TEXT("瞄准设施")))return false;
        UE_LOG(LogTemp,Display,TEXT("CF_UPPER_FARM_ORDER PASS downstairs=1 aimed=1 pet=%s farm=%s"),*PetId.ToString(),*FarmId.ToString());
        if(!Hero->SaveProgress(TEXT("UpperFarmAutomation")) || !Hero->LoadProgress(TEXT("UpperFarmAutomation")) || !CheckSnapshot(TEXT("UpperFarmAutomation"),false))return false;
        UE_LOG(LogTemp,Display,TEXT("CF_UPPER_FARM_SAVED PASS version=%d downstairs=1 task=1 seed=1 food=0 pet=%s farm=%s"),FrontierCurrentSaveVersion,*PetId.ToString(),*FarmId.ToString());
        return true;
    }
    int32 Move()
    {
        const auto Now=Hero->GetActorLocation();while(Point<Points.Num() && FVector::Dist2D(Now,Points[Point])<12)++Point;
        if(Point==Points.Num()){Stop();return 1;}
        const auto Direction=(Points[Point]-Now).GetSafeNormal2D();Face(Now+Direction*500+FVector(0,0,64));Input->Direction=Direction;
        if(FVector::Dist2D(Now,Anchor)>20){Anchor=Now;ProgressAt=World->GetTimeSeconds();}
        if(World->GetTimeSeconds()-ProgressAt>5){UE_LOG(LogTemp,Display,TEXT("CF_UPPER_FARM_BLOCKED phase=%d point=%d target=%s"),Phase,Point,*Points[Point].ToString());return -1;}
        return 0;
    }
    void Advance(int32 Next){Phase=Next;At=World->GetTimeSeconds();ProgressAt=At;Anchor=Hero->GetActorLocation();Point=0;}
    bool Tick()
    {
        if(FPlatformTime::Seconds()>Deadline || !Pet.IsValid() || !Farm.IsValid() || !Input.IsValid())return Finish(false,TEXT("lifetime"));
        const auto Now=Hero->GetActorLocation(),PetNow=Pet->GetActorLocation();
        const double HeroStep=FVector::Dist(Now,PreviousHero),PetStep=FVector::Dist(PetNow,PreviousPet);HeroTravel+=FVector::Dist2D(Now,PreviousHero);PetTravel+=FVector::Dist2D(PetNow,PreviousPet);PreviousHero=Now;PreviousPet=PetNow;
        if(HeroStep>=80 || PetStep>=120 || Hero->Health<=0 || Pet->Health<=0 || !Hero->GetActorEnableCollision() || !Pet->GetActorEnableCollision() || !Pet->IsActorTickEnabled() ||
            !Hero->Pace->IsSlowWalking() || Hero->GetCharacterMovement()->MovementMode!=MOVE_Walking || Pet->GetCharacterMovement()->MovementMode==MOVE_None ||
            Pet->PersistentId!=PetId || Farm->PersistentId!=FarmId || !EqualHouse(House,HomeLayout::Collect(World)))return Finish(false,TEXT("continuous-or-identity"));
        if(Phase<3){PetClimbed |= PetNow.Z>110 && PetNow.Z<330;PetEntered |= PetNow.Z>350 && PetNow.X>-150;}
        else PetDescended |= PetNow.Z>110 && PetNow.Z<330;
        HeroClimbed |= Phase<3 && Now.Z>110 && Now.Z<330;
        if(Phase==0)
        {
            if(Pet->WorkSite!=Farm.Get() || Pet->Activity!=ECreatureActivity::Work || Hero->Inventory->Count(EFrontierItem::Food)!=0 || Farm->Crop->Seeds+(Farm->Crop->bPlanted?1:0)+Farm->StoredFood!=1)return Finish(false,TEXT("work-conservation"));
            if(Farm->StoredFood==0){if(World->GetTimeSeconds()-At>30)return Finish(false,TEXT("production-timeout"));return true;}
            UE_LOG(LogTemp,Display,TEXT("CF_UPPER_FARM_PRODUCTION_STATE climbed=%d entered=%d seed=%d planted=%d growth=%.3f food=%d xp=%d travel=%.1f"),PetClimbed,PetEntered,Farm->Crop->Seeds,Farm->Crop->bPlanted,Farm->Crop->GrowthWork,Farm->StoredFood,Pet->Experience,PetTravel);
            if(!PetClimbed || !PetEntered || PetNow.Z<350 || !Farm->GetActorLocation().Equals(FVector(100,-700,325),.01) || Farm->StoredFood!=1 ||
                Farm->Crop->Seeds!=0 || Farm->Crop->bPlanted || Farm->Crop->GrowthWork!=0 || Pet->Experience!=1 || PetTravel<900)return Finish(false,TEXT("production"));
            UE_LOG(LogTemp,Display,TEXT("CF_UPPER_FARM_PRODUCED PASS upstairs=1 actualSteps=1 seed=0 food=1 xp=1 travel=%.1f pet=%s farm=%s"),PetTravel,*PetId.ToString(),*FarmId.ToString());
            Points={FVector(740,-700,40),FVector(740,-1150,40),FVector(-780,-1150,40),FVector(-780,-800,40),FVector(100,-800,380)};Advance(1);return true;
        }
        if(Phase==1)
        {
            const int32 Arrived=Move();if(Arrived<0)return Finish(false,TEXT("player-approach"));if(!Arrived)return true;
            if(Now.Z<350 || !HeroClimbed || Farm->StoredFood!=1 || Pet->Experience!=1)return Finish(false,TEXT("player-upstairs"));
            Face(Farm->GetActorLocation()+FVector(0,0,30));
            if(Hero->TraceFacility()!=Farm.Get() || !CFInputQA::Press(Hero,EKeys::O) || Farm->StoredFood!=0 || Hero->Inventory->Count(EFrontierItem::Food)!=1 ||
                !CFInputQA::Press(Hero,EKeys::F) || Pet->Activity!=ECreatureActivity::Follow || Pet->WorkSite)return Finish(false,TEXT("withdraw-follow"));
            UE_LOG(LogTemp,Display,TEXT("CF_UPPER_FARM_WITHDRAW PASS upstairs=1 food=1 stored=0 follow=1 pet=%s farm=%s"),*PetId.ToString(),*FarmId.ToString());
            Points={FVector(10,-800,380),FVector(10,-920,380),FVector(-150,-920,380),FVector(-400,-800,220),FVector(-950,-800,40)};Advance(3);return true;
        }
        if(Phase==3)
        {
            const int32 Arrived=Move();if(Arrived<0)return Finish(false,TEXT("player-return"));if(!Arrived)return true;Advance(4);return true;
        }
        if(World->GetTimeSeconds()-At>15)return Finish(false,TEXT("rejoin-timeout"));
        if(PetNow.Z>100 || PetNow.X>-650 || FVector::Dist2D(PetNow,Pet->GetFollowDestination())>65)return true;
        if(Now.Z>100 || !PetDescended || HeroTravel<3500 || PetTravel<1500 || Pet->Activity!=ECreatureActivity::Follow || Pet->WorkSite ||
            Farm->StoredFood!=0 || Farm->Crop->Seeds!=0 || Hero->Inventory->Count(EFrontierItem::Food)!=1)return Finish(false,TEXT("rejoin-state"));
        UE_LOG(LogTemp,Display,TEXT("CF_UPPER_FARM_RETURN PASS downstairs=1 continuous=1 food=1 seeds=1 material=94 pet=%s farm=%s"),*PetId.ToString(),*FarmId.ToString());
        Stop();const bool Saved=Hero->SaveProgress(TEXT("UpperFarmDoneAutomation")) && Hero->LoadProgress(TEXT("UpperFarmDoneAutomation")) && CheckSnapshot(TEXT("UpperFarmDoneAutomation"),true);
        UE_LOG(LogTemp,Display,TEXT("CF_UPPER_FARM_DONE_SAVE %s version=%d modules=12 farm=1 food=1 seeds=1 material=94 follow=1 pet=%s farmId=%s"),Saved?TEXT("PASS"):TEXT("FAIL"),FrontierCurrentSaveVersion,*PetId.ToString(),*FarmId.ToString());
        return Finish(Saved,TEXT("complete"));
    }
};
}
void RunUpperFarmScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    const auto State=MakeShared<FUpperFarmTest>();State->World=World;State->Hero=Hero;State->Read=Read;State->Deadline=FPlatformTime::Seconds()+90;
    if(Read)
    {
        if(!Hero->LoadProgress(TEXT("UpperFarmAutomation"))){State->Finish(false,TEXT("read"));return;}
        State->House=HomeLayout::Collect(World);
        if(!State->CheckSnapshot(TEXT("UpperFarmAutomation"),false)){State->Finish(false,TEXT("restore"));return;}
        State->PetId=State->Pet->PersistentId;State->FarmId=State->Farm->PersistentId;
        UE_LOG(LogTemp,Display,TEXT("CF_UPPER_FARM_RESTORE PASS version=%d modules=12 farm=1 downstairs=1 task=1 seed=1 food=0 pet=%s farmId=%s"),Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("UpperFarmAutomation"),0))->FormatVersion,*State->PetId.ToString(),*State->FarmId.ToString());
    }
    else if(!State->Init()){State->Finish(false,TEXT("construction-order-save"));return;}
    auto* Input=NewObject<UHomeMovementProbe>(Hero);Input->RegisterComponent();State->Input=Input;
    if(!CFInputQA::Press(Hero,EKeys::LeftShift) || !Hero->Pace->IsSlowWalking()){State->Finish(false,TEXT("slow-input"));return;}
    State->PreviousHero=Hero->GetActorLocation();State->PreviousPet=State->Pet->GetActorLocation();State->At=World->GetTimeSeconds();State->ProgressAt=State->At;State->Anchor=State->PreviousHero;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float){return State->Tick();}));
}
