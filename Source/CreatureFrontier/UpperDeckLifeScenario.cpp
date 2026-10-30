#include "ConstructionScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "BaseBuildingComponent.h"
#include "HomeModule.h"
#include "HomeLayout.h"
#include "HomeScenario.h"
#include "InventoryComponent.h"
#include "CompanionCargoComponent.h"
#include "FarmCropComponent.h"
#include "WorkStation.h"
#include "FrontierSaveGame.h"
#include "ResourceNode.h"
#include "ResourceDefinition.h"
#include "WildEncounter.h"
#include "InputRoutingQA.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/Ticker.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

namespace
{
bool Continuous(const ACharacter* Pawn,FVector Before,FVector Position,FVector PreviousVelocity,double Delta)
{
    if(!Pawn || !FMath::IsFinite(Delta) || Delta<0 || Position.ContainsNaN())return false;
    const auto* Movement=Pawn->GetCharacterMovement();if(!Movement)return false;
    // 与施工同行检查一致，按真实游戏时间、速度、重力和跨阶高度判断，不调整游戏参数。
    const double Horizontal=FMath::Max(static_cast<double>(Movement->GetMaxSpeed()),PreviousVelocity.Size2D())*Delta+3;
    const double Vertical=FMath::Max(FMath::Abs(Pawn->GetVelocity().Z),FMath::Abs(PreviousVelocity.Z))*Delta+
        .5*FMath::Abs(Pawn->GetWorld()->GetGravityZ())*Delta*Delta+Movement->MaxStepHeight+3;
    return FVector::Dist2D(Before,Position)<=Horizontal && FMath::Abs(Position.Z-Before.Z)<=Vertical;
}
struct FDeckLifeTest
{
    UWorld* World;ACFPlayerCharacter* Hero;TWeakObjectPtr<ACreatureCharacter> Pet;TWeakObjectPtr<AWorkStation> Farm;
    TWeakObjectPtr<UHomeMovementProbe> Input;TArray<FHomeModuleRecord> House;TArray<FFacilitySaveRecord> OriginalFacilities;
    TArray<FVector> Route;FString Nickname,ImageRoot;TSet<FString> Photos;FGuid FarmId;int32 Phase=0,Harvested=0,Point=0;bool Climbed=false,Descended=false,Preview=false;
    double Deadline=0,PreviousTime=0,HeroTravel=0,PetTravel=0,ProgressAt=0,ProductionAt=0,NextPhoto=0;
    FVector PreviousPet,PreviousHero,PetVelocity,HeroVelocity,Anchor;
    const FVector FarmPosition=FVector(-1200,-300,325);
    bool Bind(bool NeedFarm)
    {
        Pet.Reset();Farm.Reset();int32 Owned=0;
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(It->Tamer==Hero->GetController()){Pet=*It;++Owned;}
        for(TActorIterator<AWorkStation> It(World);It;++It)if(It->bPlayerBuilt && It->GetActorLocation().Equals(FarmPosition,.01))Farm=*It;
        if(Owned!=1 || !Pet.IsValid() || Pet->PersistentId!=FGuid(0x1209AAFE,0x4D5C4544,0x1D65AF94,0x1B0435D1) ||
            (NeedFarm && (!Farm.IsValid() || Farm->RequiredSpecialty!=EWorkSpecialty::Farming || (FarmId.IsValid() && Farm->PersistentId!=FarmId))))return false;
        if(Farm.IsValid())FarmId=Farm->PersistentId;
        PreviousPet=Pet->GetActorLocation();PreviousHero=Hero->GetActorLocation();PetVelocity=Pet->GetVelocity();HeroVelocity=Hero->GetVelocity();PreviousTime=World->GetTimeSeconds();
        for(TActorIterator<AWildEncounter> It(World);It;++It)It->SetActorTickEnabled(false);
        for(TActorIterator<ACreatureCharacter> It(World);It;++It)if(*It!=Pet.Get()){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();}
        return true;
    }
    bool SameHouse() const
    {
        const auto Current=HomeLayout::Collect(World);if(Current.Num()!=25)return false;
        for(const auto& Old:House){const auto* Now=Current.FindByPredicate([&](const FHomeModuleRecord& R){return R.Id==Old.Id;});
            if(!Now || Now->Definition!=Old.Definition || Now->FloorId!=Old.FloorId || !Now->Transform.Equals(Old.Transform,.01) || Now->PaidMaterials!=Old.PaidMaterials ||
                Now->DoorState!=Old.DoorState || !Now->StoredItems.OrderIndependentCompareEqual(Old.StoredItems) || !Now->PaidExtraItems.OrderIndependentCompareEqual(Old.PaidExtraItems))return false;}
        return true;
    }
    void Face(FVector Target){const auto Rotation=(Target-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(Rotation);Hero->FirstPersonCamera->SetWorldRotation(Rotation);}
    void Observe(FVector Position){Hero->SetActorLocation(Position);PreviousHero=Position;}
    void Photo(const TCHAR* Name,FVector Target)
    {
        if(!Preview || Photos.Contains(Name))return;Photos.Add(Name);Face(Target);
        FScreenshotRequest::RequestScreenshot(ImageRoot/(FString(Name)+TEXT(".png")),false,false);NextPhoto=FPlatformTime::Seconds()+.2;
    }
    bool End(bool Pass,const TCHAR* Reason)
    {
        if(Input.IsValid())Input->Direction=FVector::ZeroVector;const FString Feedback=Hero->Feedback;
        if(!Pass)Hero->SaveProgress(TEXT("UpperDeckLifeBlocked"));
        UE_LOG(LogTemp,Display,TEXT("CF_DECK_LIFE %s reason=%s phase=%d point=%d heroTravel=%.1f petTravel=%.1f hero=%s pet=%s feedback=%s"),
            Pass?TEXT("PASS"):TEXT("FAIL"),Reason,Phase,Point,HeroTravel,PetTravel,*Hero->GetActorLocation().ToString(),Pet.IsValid()?*Pet->GetActorLocation().ToString():TEXT("无"),*Feedback);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return false;
    }
    bool Build()
    {
        // 只在采集／建造夹具阶段固定观察者；开始生活移动后不再设置任何角色位置。
        Observe(FVector(-1330,-450,380.15));Hero->Building->Selected=EWorkSpecialty::Farming;
        Farm=Hero->Building->BuildAt(FTransform(FRotator::ZeroRotator,FarmPosition,FVector(2,1,.7)));
        if(!Farm.IsValid() || !HomeLayout::SupportsFacility(Farm->GetActorTransform(),House) || Hero->Inventory->Count(EFrontierItem::Material)!=0)return false;
        FarmId=Farm->PersistentId;Face(FarmPosition+FVector(0,0,30));
        if(Hero->TraceFacility()!=Farm.Get() || !CFInputQA::Press(Hero,EKeys::F2) || Farm->Crop->Seeds!=1 || Hero->Inventory->Count(EFrontierItem::Seeds)!=0)return false;
        Observe(FVector(-1200,200,40.15));Face(FarmPosition+FVector(0,0,30));
        if(Pet->GetActorLocation().Z>100 || !CFInputQA::Press(Hero,EKeys::E) || Pet->WorkSite!=Farm.Get() || Pet->Activity!=ECreatureActivity::Work || !Hero->Feedback.Contains(TEXT("瞄准设施")))return false;
        if(!Hero->SaveProgress(TEXT("UpperDeckLifeWork")))return false;
        UE_LOG(LogTemp,Display,TEXT("CF_DECK_LIFE_ORDER PASS version=37 downstairs=1 aimed=1 material=0 seeds=0 reserved=1 facilities=3 farm=%s"),*FarmId.ToString());
        ProductionAt=World->GetTimeSeconds();return true;
    }
    void Walk(TArray<FVector> Points,int32 Next)
    {
        if(!Input.IsValid()){Input=NewObject<UHomeMovementProbe>(Hero);Input->RegisterComponent();}
        Route=MoveTemp(Points);Point=0;Phase=Next;Anchor=Hero->GetActorLocation();ProgressAt=World->GetTimeSeconds();PreviousHero=Anchor;HeroVelocity=Hero->GetVelocity();
    }
    int32 Move()
    {
        const auto H=Hero->GetActorLocation();while(Point<Route.Num() && FVector::Dist2D(H,Route[Point])<15)++Point;
        if(Point==Route.Num()){Input->Direction=FVector::ZeroVector;return 1;}
        if(FVector::Dist2D(H,Anchor)>10){Anchor=H;ProgressAt=World->GetTimeSeconds();}
        if(World->GetTimeSeconds()-ProgressAt>15)return -1;
        Input->Direction=(Route[Point]-H).GetSafeNormal2D();Face(H+Input->Direction*500+FVector(0,0,64));return 0;
    }
    bool FinishSave()
    {
        if(!Hero->SaveProgress(TEXT("UpperDeckLifeDone")) || !Hero->LoadProgress(TEXT("UpperDeckLifeDone")) || !Bind(true) || !SameHouse())return false;
        const auto* Save=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("UpperDeckLifeDone"),0));
        if(!Save || Save->FormatVersion!=37 || Save->Facilities.Num()!=3 || Pet->Experience!=9 || Pet->BondPoints!=12 || Pet->Nickname!=Nickname || Pet->WorkSite ||
            Pet->Activity!=ECreatureActivity::Follow || Pet->Cargo->Count()!=0 || Pet->CarriedFood!=0 || Farm->StoredFood!=0 || Farm->Crop->Seeds!=0 || Farm->Crop->bPlanted || Farm->Crop->GrowthWork!=0)return false;
        for(const auto& Old:OriginalFacilities){const auto* Now=Save->Facilities.FindByPredicate([&](const FFacilitySaveRecord& R){return R.Id==Old.Id;});
            if(!Now || Now->Specialty!=Old.Specialty || !Now->Transform.Equals(Old.Transform,.01) || Now->StoredFood!=Old.StoredFood || Now->Seeds!=Old.Seeds || Now->bPlanted!=Old.bPlanted || Now->GrowthWork!=Old.GrowthWork)return false;}
        const auto* SavedPet=Save->Creatures.FindByPredicate([this](const FCreatureSaveRecord& R){return R.bOwned && R.Id==Pet->PersistentId;});
        return SavedPet && SavedPet->Nickname==Nickname && SavedPet->Transform.Equals(Pet->GetActorTransform(),.01) && Hero->GetActorTransform().Equals(Save->PlayerTransform,.01) &&
            SavedPet->Experience==9 && SavedPet->BondPoints==12 && !SavedPet->WorkSiteId.IsValid() &&
            Hero->Inventory->Count(EFrontierItem::Material)==0 && Hero->Inventory->Count(EFrontierItem::Seeds)==0 && Hero->Inventory->Count(EFrontierItem::Food)==0;
    }
    bool Tick()
    {
        if(!Pet.IsValid() || FPlatformTime::Seconds()>Deadline)return End(false,TEXT("lifetime"));
        const auto P=Pet->GetActorLocation(),H=Hero->GetActorLocation();const double Delta=World->GetTimeSeconds()-PreviousTime;PreviousTime=World->GetTimeSeconds();
        PetTravel+=FVector::Dist(P,PreviousPet);const bool PetContinuous=Continuous(Pet.Get(),PreviousPet,P,PetVelocity,Delta);PreviousPet=P;PetVelocity=Pet->GetVelocity();
        if(!PetContinuous || !Pet->GetActorEnableCollision() || !Pet->IsActorTickEnabled() || Pet->GetCharacterMovement()->MovementMode==MOVE_None || Pet->Health<=0 || Hero->Health<=0 || !SameHouse())return End(false,TEXT("continuity-or-house"));
        if(Phase>=2){HeroTravel+=FVector::Dist(H,PreviousHero);const bool HeroContinuous=Continuous(Hero,PreviousHero,H,HeroVelocity,Delta);PreviousHero=H;HeroVelocity=Hero->GetVelocity();if(!HeroContinuous)return End(false,TEXT("hero-continuity"));}
        // 截图等待不跳过位移采样，也不暂停世界或伙伴工作。
        if(FPlatformTime::Seconds()<NextPhoto)return true;
        if(Phase==0)
        {
            if(Harvested==18){if(Hero->Inventory->Count(EFrontierItem::Material)!=18 || !Build())return End(false,TEXT("build-seed-order"));Phase=1;return true;}
            for(TActorIterator<AResourceNode> It(World);It;++It)if(It->Definition && It->Definition->OutputItem==EFrontierItem::Material && It->Remaining>0 && It->HarvestCooldown<=0)
            {Observe(It->GetActorLocation()+FVector(0,-180,60));if(It->TryHarvest(Hero,Hero)){++Harvested;UE_LOG(LogTemp,Display,TEXT("CF_DECK_LIFE_HARVEST count=%d material=%d"),Harvested,Hero->Inventory->Count(EFrontierItem::Material));return true;}}
            return true;
        }
        if(!Farm.IsValid() || Farm->PersistentId!=FarmId)return End(false,TEXT("farm"));
        if(Phase==1)
        {
            Climbed|=P.Z>110 && P.Z<330;
            if(Preview && !Photos.Contains(TEXT("Work")))
            {
                Photo(TEXT("Work"),FarmPosition+FVector(0,0,30));if(Hero->TraceFacility()!=Farm.Get())return End(false,TEXT("work-preview-aim"));
                UE_LOG(LogTemp,Display,TEXT("CF_DECK_LIFE_AIM PASS downstairs=1 target=1"));return true;
            }
            if(Farm->Crop->Seeds+(Farm->Crop->bPlanted?1:0)+Farm->StoredFood!=1 || Hero->Inventory->Count(EFrontierItem::Food)!=0 || Pet->WorkSite!=Farm.Get())return End(false,TEXT("seed-conservation"));
            if(Farm->StoredFood==0){if(World->GetTimeSeconds()-ProductionAt>60)return End(false,TEXT("production-timeout"));return true;}
            if(!Climbed || P.Z<350 || Pet->Experience!=9 || Farm->StoredFood!=1 || Farm->Crop->Seeds!=0 || Farm->Crop->bPlanted)return End(false,TEXT("production"));
            UE_LOG(LogTemp,Display,TEXT("CF_DECK_LIFE_PRODUCED PASS upstairs=1 actualSteps=1 seed=0 food=1 xp=9"));
            Photo(TEXT("Production"),FarmPosition+FVector(0,0,30));
            Walk({FVector(-1650,200,40),FVector(-1650,-1080,40),FVector(-1200,-1080,40),FVector(-1250,-550,380),FVector(-1330,-450,380)},2);return true;
        }
        if(Phase==2)
        {
            const int32 Arrived=Move();if(Arrived<0)return End(false,TEXT("player-up-blocked"));if(!Arrived)return true;
            if(H.Z<350 || P.Z<350)return End(false,TEXT("upstairs"));Face(FarmPosition+FVector(0,0,30));
            if(Hero->TraceFacility()!=Farm.Get() || !CFInputQA::Press(Hero,EKeys::O) || Farm->StoredFood!=0 || Hero->Inventory->Count(EFrontierItem::Food)!=1)return End(false,TEXT("withdraw"));
            Phase=5;Photo(TEXT("Harvest"),FarmPosition+FVector(0,0,30));return true;
        }
        if(Phase==5)
        {
            Face(P);const float Nutrition=Pet->Nutrition,Stamina=Pet->Stamina;
            if(Hero->TraceCreature()!=Pet.Get() || !CFInputQA::Press(Hero,EKeys::F) || Pet->WorkSite || !CFInputQA::Press(Hero,EKeys::J) ||
                Hero->Inventory->Count(EFrontierItem::Food)!=0 || Pet->BondPoints!=12 || !FMath::IsNearlyEqual(Pet->Nutrition,FMath::Min(100.f,Nutrition+35),.01f) ||
                !FMath::IsNearlyEqual(Pet->Stamina,FMath::Min(100.f,Stamina+15),.01f))return End(false,TEXT("feed"));
            UE_LOG(LogTemp,Display,TEXT("CF_DECK_LIFE_FEED PASS upstairs=1 food=0 bond=12 nutritionBefore=%.2f nutritionAfter=%.2f"),Nutrition,Pet->Nutrition);
            Photo(TEXT("Fed"),P);
            Walk({FVector(-1250,-550,380),FVector(-1200,-1080,40),FVector(-1500,-1080,40)},3);return true;
        }
        Descended|=P.Z>110 && P.Z<330;
        if(Phase==3){const int32 Arrived=Move();if(Arrived<0)return End(false,TEXT("return-blocked"));if(!Arrived)return true;Phase=4;ProgressAt=World->GetTimeSeconds();}
        if(World->GetTimeSeconds()-ProgressAt>20)return End(false,TEXT("rejoin"));
        if(P.Z>100 || Pet->GetDistanceTo(Hero)>250)return true;
        if(H.Z>100 || !Descended || HeroTravel<3000 || PetTravel<1500 || !FinishSave())return End(false,TEXT("done-save"));
        UE_LOG(LogTemp,Display,TEXT("CF_DECK_LIFE_SAVED PASS version=37 modules=25 retainedFacilities=2 facilities=3 material=0 seeds=0 food=0 xp=9 bond=12"));return End(true,TEXT("complete"));
    }
};
}
void RunUpperDeckLifeScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read)
{
    auto State=MakeShared<FDeckLifeTest>();State->World=World;State->Hero=Hero;State->Deadline=FPlatformTime::Seconds()+300;
    const auto* Source=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("UpperDeckLifeInput"),0));
    if(!Source || Source->FormatVersion!=37 || Source->HomeModules.Num()!=25 || Source->Facilities.Num()!=(Read?3:2) || !Hero->LoadProgress(TEXT("UpperDeckLifeInput")) || !State->Bind(Read)) {State->End(false,TEXT("source"));return;}
    State->House=HomeLayout::Collect(World);State->Nickname=State->Pet->Nickname;State->OriginalFacilities=Source->Facilities;
    State->Preview=FParse::Param(FCommandLine::Get(),TEXT("CFUpperDeckLifePreview"));
    if(State->Preview){State->ImageRoot=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("UpperDeckLifePreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*State->ImageRoot,true);UE_LOG(LogTemp,Display,TEXT("CF_DECK_LIFE_PREVIEW_ROOT %s"),*State->ImageRoot);}
    State->OriginalFacilities.RemoveAll([State](const FFacilitySaveRecord& R){return R.Transform.GetLocation().Equals(State->FarmPosition,.01);});
    if(State->OriginalFacilities.Num()!=2 || Hero->Inventory->Count(EFrontierItem::Material)!=0 || Hero->Inventory->Count(EFrontierItem::Food)!=0 ||
        Hero->Inventory->Count(EFrontierItem::Seeds)!=(Read?0:1) || State->Pet->Experience!=8 || State->Pet->BondPoints!=9 || State->Pet->GetActorLocation().Z>100){State->End(false,TEXT("original"));return;}
    if(Read)
    {
        if(State->Pet->WorkSite!=State->Farm.Get() || State->Farm->Crop->Seeds!=1 || State->Farm->Crop->bPlanted || State->Farm->StoredFood!=0){State->End(false,TEXT("restore"));return;}
        State->Phase=1;State->ProductionAt=World->GetTimeSeconds();UE_LOG(LogTemp,Display,TEXT("CF_DECK_LIFE_RESTORE PASS version=37 downstairs=1 task=1 reserved=1 retained=25"));
    }
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float){return State->Tick();}));
}
