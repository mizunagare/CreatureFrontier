#include "HomeLifeScenario.h"
#include "HomeScenario.h"
#include "HomeFundingScenario.h"
#include "HomeLayout.h"
#include "HomeModule.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "CompanionCargoComponent.h"
#include "WorkStation.h"
#include "FarmCropComponent.h"
#include "InventoryComponent.h"
#include "PlayerPaceComponent.h"
#include "FrontierSaveGame.h"
#include "InputRoutingQA.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Containers/Ticker.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

namespace
{
bool SameHome(const TArray<FHomeModuleRecord>& Left,const TArray<FHomeModuleRecord>& Right)
{
    if(Left.Num()!=14 || Right.Num()!=Left.Num())return false;
    for(const auto& Record:Left)
    {
        const auto* Other=Right.FindByPredicate([&Record](const FHomeModuleRecord& Value){return Value.Id==Record.Id;});
        if(!Other || Other->FloorId!=Record.FloorId || Other->Definition!=Record.Definition || !Other->Transform.Equals(Record.Transform,.01) ||
            Other->DoorState!=Record.DoorState || Other->PaidMaterials!=Record.PaidMaterials ||
            !Other->PaidExtraItems.OrderIndependentCompareEqual(Record.PaidExtraItems) || !Other->StoredItems.OrderIndependentCompareEqual(Record.StoredItems))return false;
    }
    return true;
}
struct FHomeLifeTest
{
    UWorld* World;ACFPlayerCharacter* Hero;TFunction<void(bool)> Complete;
    FGuid PetId,FarmId;TWeakObjectPtr<ACreatureCharacter> Pet;TWeakObjectPtr<AWorkStation> Farm;TWeakObjectPtr<AHomeModule> Door;
    TWeakObjectPtr<UHomeMovementProbe> Input;TArray<FHomeModuleRecord> OriginalHome;
    TArray<FVector> Points;int32 Point=0,Phase=0;
    FVector PreviousHero,PreviousPet,ProgressAnchor;double HeroTravel=0,PetTravel=0,Deadline=0,ProgressAt=0,At=0;
    bool Entered=false,Exited=false,PetExited=false,VisitedCourt=false;
    void Face(FVector Target)
    {
        const auto View=(Target-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
        Hero->SetActorRotation(FRotator(0,View.Yaw,0));Hero->FirstPersonCamera->SetRelativeRotation(FRotator::ZeroRotator);
        Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
    }
    void Stop()
    {if(Input.IsValid())Input->Direction=FVector::ZeroVector;Hero->GetCharacterMovement()->StopMovementImmediately();Hero->ConsumeMovementInputVector();}
    bool Finish(bool Pass,const TCHAR* Reason)
    {
        Stop();if(Input.IsValid())Input->DestroyComponent();
        CFInputQA::Release(Hero,EKeys::LeftShift);
        UE_LOG(LogTemp,Display,TEXT("CF_HOME_LIFE %s phase=%d reason=%s entered=%d exited=%d petExited=%d heroTravel=%.1f petTravel=%.1f pet=%s"),
            Pass?TEXT("PASS"):TEXT("FAIL"),Phase,Reason,Entered,Exited,PetExited,HeroTravel,PetTravel,*PetId.ToString());
        const auto Done=Complete;Done(Pass);return false;
    }
    bool Cross(FVector From,FVector To,bool IsPet)
    {
        for(double Boundary:{-1000.,-600.})if((From.Y-Boundary)*(To.Y-Boundary)<0)
        {const double X=FMath::Lerp(From.X,To.X,(Boundary-From.Y)/(To.Y-From.Y));if(X>-190 && X<190)return false;}
        if((From.X>=-200 && To.X<-200) || (From.X<-200 && To.X>=-200))
        {
            const double Y=FMath::Lerp(From.Y,To.Y,(-200-From.X)/(To.X-From.X));
            if(FMath::Abs(Y+800)>=72 || !Door.IsValid() || Door->DoorState!=2)return false;
            if(IsPet)PetExited |= To.X>=-200;
            else {Entered |= To.X<-200;Exited |= To.X>=-200;}
        }
        return true;
    }
    int32 Move()
    {
        const auto Now=Hero->GetActorLocation();
        while(Point<Points.Num() && FVector::Dist2D(Now,Points[Point])<12)++Point;
        if(Point==Points.Num()){Stop();return 1;}
        const auto Direction=(Points[Point]-Now).GetSafeNormal2D();
        Face(Now+Direction*500+FVector(0,0,64));Input->Direction=Direction;
        if(FVector::Dist2D(Now,ProgressAnchor)>20){ProgressAnchor=Now;ProgressAt=World->GetTimeSeconds();}
        if(World->GetTimeSeconds()-ProgressAt>5)
        {
            UE_LOG(LogTemp,Error,TEXT("CF_HOME_LIFE_BLOCKED phase=%d point=%d hero=%s goal=%s pet=%s"),Phase,Point,*Now.ToString(),*Points[Point].ToString(),*Pet->GetActorLocation().ToString());
            return -1;
        }
        return 0;
    }
    bool SaveAndCheck()
    {
        auto* BeforePet=Pet.Get();auto* BeforeFarm=Farm.Get();
        const FString Nickname=BeforePet->Nickname;auto* Definition=BeforePet->Definition.Get();
        const int32 Experience=BeforePet->Experience,Level=BeforePet->Level;
        const float Nutrition=BeforePet->Nutrition,Stamina=BeforePet->Stamina,Health=BeforePet->Health;
        const auto PetTransform=BeforePet->GetActorTransform(),FarmTransform=BeforeFarm->GetActorTransform(),HeroTransform=Hero->GetActorTransform();
        const FString Slot=TEXT("HomeLifeAutomation");
        if(!Hero->SaveProgress(Slot) || !Hero->LoadProgress(Slot))return false;
        const auto* Save=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0));
        if(!Save || !SameHome(OriginalHome,Save->HomeModules) || !SameHome(OriginalHome,HomeLayout::Collect(World)) ||
            !VerifyHomeFundingSources(World) || !HeroTransform.Equals(Hero->GetActorTransform(),.01) || !Save->PlayerTransform.Equals(HeroTransform,.01) ||
            !Hero->Inventory->GetStacks().OrderIndependentCompareEqual(Save->InventoryStacks) || Hero->Inventory->Count(EFrontierItem::Material)!=0 ||
            Hero->Inventory->Count(EFrontierItem::Food)!=0 || Hero->Inventory->Count(EFrontierItem::Seeds)!=2 || Hero->Inventory->Count(EFrontierItem::CaptureDevice)!=5)return false;
        ACreatureCharacter* RestoredPet=nullptr;AWorkStation* RestoredFarm=nullptr;int32 Owned=0;
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController()){++Owned;if(It->PersistentId==PetId)RestoredPet=*It;}
        for(TActorIterator<AWorkStation> It(World);It;++It)if(It->PersistentId==FarmId && It->bPlayerBuilt)RestoredFarm=*It;
        const auto* Creature=Save->Creatures.FindByPredicate([this](const FCreatureSaveRecord& Record){return Record.bOwned && Record.Id==PetId;});
        const auto* Facility=Save->Facilities.FindByPredicate([this](const FFacilitySaveRecord& Record){return Record.Id==FarmId;});
        return Owned==1 && RestoredPet && RestoredFarm && Creature && Facility && Save->Facilities.Num()==1 &&
            Creature->Activity==ECreatureActivity::Follow && !Creature->WorkSiteId.IsValid() && !Creature->FieldResourceId.IsValid() && Creature->CargoItems.IsEmpty() && !Creature->DeliveryCrateId.IsValid() &&
            Creature->BondPoints==6 && Creature->Nickname==Nickname && Creature->Definition.Get()==Definition && Creature->Transform.Equals(PetTransform,.01) &&
            FMath::IsNearlyEqual(Creature->Nutrition,Nutrition,.01f) && FMath::IsNearlyEqual(Creature->Stamina,Stamina,.01f) && FMath::IsNearlyEqual(Creature->Health,Health,.01f) &&
            RestoredPet->Activity==ECreatureActivity::Follow && !RestoredPet->WorkSite && !RestoredPet->GetFieldResource() && !RestoredPet->Cargo->HasOrder() && RestoredPet->Cargo->Count()==0 &&
            RestoredPet->BondPoints==6 && RestoredPet->Nickname==Nickname && RestoredPet->Definition==Definition && RestoredPet->Experience==Experience && RestoredPet->Level==Level &&
            RestoredPet->GetActorTransform().Equals(PetTransform,.01) && FMath::IsNearlyEqual(RestoredPet->Nutrition,Nutrition,.01f) &&
            FMath::IsNearlyEqual(RestoredPet->Stamina,Stamina,.01f) && FMath::IsNearlyEqual(RestoredPet->Health,Health,.01f) && Hero->HasJournalEntry(Definition,true) &&
            Facility->StoredFood==0 && Facility->Seeds==0 && !Facility->bPlanted && Facility->GrowthWork==0 && Facility->Transform.Equals(FarmTransform,.01) &&
            RestoredFarm->StoredFood==0 && RestoredFarm->Crop->Seeds==0 && !RestoredFarm->Crop->bPlanted && RestoredFarm->Crop->GrowthWork==0 && RestoredFarm->GetActorTransform().Equals(FarmTransform,.01);
    }
    bool Tick()
    {
        auto* Partner=Pet.Get();auto* Garden=Farm.Get();
        if(FPlatformTime::Seconds()>Deadline || !Partner || !Garden || !Door.IsValid() || !Input.IsValid())return Finish(false,TEXT("lifetime"));
        const auto HeroNow=Hero->GetActorLocation(),PetNow=Partner->GetActorLocation();
        const double HeroStep=FVector::Dist(HeroNow,PreviousHero),PetStep=FVector::Dist(PetNow,PreviousPet);
        HeroTravel+=FVector::Dist2D(HeroNow,PreviousHero);PetTravel+=FVector::Dist2D(PetNow,PreviousPet);
        if(HeroStep>=80 || PetStep>=120 || Hero->Health<=0 || Partner->Health<=0 || HeroNow.Z<40 || HeroNow.Z>120 || PetNow.Z<40 || PetNow.Z>120 ||
            !Hero->GetActorEnableCollision() || !Partner->GetActorEnableCollision() || !Partner->IsActorTickEnabled() || !Hero->Pace->IsSlowWalking() ||
            Hero->GetCharacterMovement()->MovementMode!=MOVE_Walking || Partner->GetCharacterMovement()->MovementMode==MOVE_None || Partner->PersistentId!=PetId ||
            !Cross(PreviousHero,HeroNow,false) || !Cross(PreviousPet,PetNow,true))return Finish(false,TEXT("continuous-or-doorway"));
        PreviousHero=HeroNow;PreviousPet=PetNow;
        VisitedCourt |= FMath::Abs(HeroNow.X)<170 && FMath::Abs(HeroNow.Y+800)<160;
        if(Phase==0)
        {
            const int32 Arrived=Move();if(Arrived<0)return Finish(false,TEXT("entry-blocked"));if(!Arrived)return true;
            Face(Garden->GetActorLocation());
            if(!Entered || !VisitedCourt || Partner->WorkSite!=Garden || Partner->Activity!=ECreatureActivity::Work ||
                Hero->TraceFacility()!=Garden || Hero->Inventory->Count(EFrontierItem::Food)!=0 || Garden->StoredFood!=1 ||
                !CFInputQA::Press(Hero,EKeys::O) || Hero->Inventory->Count(EFrontierItem::Food)!=1 || Garden->StoredFood!=0)return Finish(false,TEXT("withdraw"));
            UE_LOG(LogTemp,Display,TEXT("CF_HOME_LIFE_WITHDRAW PASS entered=1 food=1 stored=0 heroTravel=%.1f pet=%s farm=%s"),HeroTravel,*PetId.ToString(),*FarmId.ToString());
            if(!CFInputQA::Press(Hero,EKeys::O) || Hero->Inventory->Count(EFrontierItem::Food)!=1 || Garden->StoredFood!=0)return Finish(false,TEXT("empty-store"));
            UE_LOG(LogTemp,Display,TEXT("CF_HOME_LIFE_EMPTY PASS food=1 stored=0"));
            Face(Partner->GetActorLocation());
            const float Nutrition=Partner->Nutrition,Stamina=Partner->Stamina;
            if(Nutrition>80 || Partner->BondPoints!=3 || Hero->TraceCreature()!=Partner || !CFInputQA::Press(Hero,EKeys::F) || Partner->WorkSite ||
                Partner->Activity!=ECreatureActivity::Follow || !CFInputQA::Press(Hero,EKeys::J) || Hero->Inventory->Count(EFrontierItem::Food)!=0 ||
                !FMath::IsNearlyEqual(Partner->Nutrition,FMath::Min(100.f,Nutrition+35),.01f) || !FMath::IsNearlyEqual(Partner->Stamina,FMath::Min(100.f,Stamina+15),.01f) ||
                Partner->BondPoints!=6)return Finish(false,TEXT("feed"));
            UE_LOG(LogTemp,Display,TEXT("CF_HOME_LIFE_FEED PASS food=0 stored=0 bond=6 nutritionBefore=%.2f nutritionAfter=%.2f pet=%s"),Nutrition,Partner->Nutrition,*PetId.ToString());
            if(!CFInputQA::Press(Hero,EKeys::J) || Partner->BondPoints!=6 || Hero->Inventory->Count(EFrontierItem::Food)!=0 ||
                !FMath::IsNearlyEqual(Partner->Nutrition,FMath::Min(100.f,Nutrition+35),.01f))return Finish(false,TEXT("feed-cooldown"));
            UE_LOG(LogTemp,Display,TEXT("CF_HOME_LIFE_FEED_REJECT PASS food=0 bond=6"));
            Points={FVector(-400,-800,60),FVector(100,-800,60),FVector(500,-800,60)};Point=0;Phase=1;
            ProgressAnchor=HeroNow;ProgressAt=World->GetTimeSeconds();return true;
        }
        if(Phase==1)
        {
            const int32 Arrived=Move();if(Arrived<0)return Finish(false,TEXT("exit-blocked"));if(!Arrived)return true;
            Phase=2;At=World->GetTimeSeconds();return true;
        }
        if(World->GetTimeSeconds()-At>15)return Finish(false,TEXT("rejoin-timeout"));
        if(!PetExited || FVector::Dist2D(PetNow,Partner->GetFollowDestination())>65)return true;
        if(!Entered || !Exited || HeroTravel<1400 || PetTravel<600 || Partner->Activity!=ECreatureActivity::Follow || Partner->WorkSite ||
            !SameHome(OriginalHome,HomeLayout::Collect(World)) || !VerifyHomeFundingSources(World))return Finish(false,TEXT("rejoin-state"));
        UE_LOG(LogTemp,Display,TEXT("CF_HOME_LIFE_REJOIN PASS entered=1 exited=1 petExited=1 continuous=1 heroTravel=%.1f petTravel=%.1f pet=%s"),HeroTravel,PetTravel,*PetId.ToString());
        // 存读前停止输入；旧工作态快照已另验，本槽要求生活后的跟随态，不放宽旧任务判据。
        Stop();const bool Saved=SaveAndCheck();
        UE_LOG(LogTemp,Display,TEXT("CF_HOME_LIFE_SAVE %s modules=14 stored=3 farmFood=0 food=0 seeds=2 bond=6 follow=1 pet=%s"),Saved?TEXT("PASS"):TEXT("FAIL"),*PetId.ToString());
        return Finish(Saved,TEXT("complete"));
    }
};
}
void RunHomeLifeScenario(UWorld* World,ACFPlayerCharacter* Hero,FGuid PetId,FGuid FarmId,TFunction<void(bool)> Complete)
{
    const auto State=MakeShared<FHomeLifeTest>();State->World=World;State->Hero=Hero;State->Complete=MoveTemp(Complete);State->PetId=PetId;State->FarmId=FarmId;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->PersistentId==PetId && It->Tamer==Hero->GetController())State->Pet=*It;
    for(TActorIterator<AWorkStation> It(World);It;++It)if(It->PersistentId==FarmId && It->bPlayerBuilt)State->Farm=*It;
    for(TActorIterator<AHomeModule> It(World);It;++It)if(It->GetKind()==EHomeModuleKind::Door && !It->bPreview)State->Door=*It;
    if(!State->Pet.IsValid() || !State->Farm.IsValid() || !State->Door.IsValid())
    {UE_LOG(LogTemp,Error,TEXT("CF_HOME_LIFE FAIL initial-identity"));State->Complete(false);return;}
    State->OriginalHome=HomeLayout::Collect(World);State->PreviousHero=Hero->GetActorLocation();State->PreviousPet=State->Pet->GetActorLocation();
    State->Points={FVector(80,-800,60),FVector(-400,-800,60),FVector(-400,-850,60)};
    State->ProgressAnchor=State->PreviousHero;State->ProgressAt=World->GetTimeSeconds();State->Deadline=FPlatformTime::Seconds()+90;
    auto* Input=NewObject<UHomeMovementProbe>(Hero);Input->RegisterComponent();State->Input=Input;
    Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    if(!CFInputQA::Press(Hero,EKeys::LeftShift) || !Hero->Pace->IsSlowWalking()) {State->Finish(false,TEXT("slow-input"));return;}
    UE_LOG(LogTemp,Display,TEXT("CF_HOME_LIFE_START pet=%s farm=%s nutrition=%.2f hero=%s partner=%s"),*PetId.ToString(),*FarmId.ToString(),State->Pet->Nutrition,*State->PreviousHero.ToString(),*State->PreviousPet.ToString());
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float){return State->Tick();}));
}
