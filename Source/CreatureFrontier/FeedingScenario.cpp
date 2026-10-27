#include "FeedingScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "BaseBuildingComponent.h"
#include "InventoryComponent.h"
#include "WorkStation.h"
#include "FarmCropComponent.h"
#include "FrontierSaveGame.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "HAL/PlatformMisc.h"

void RunFeedingScenario(UWorld* World, ACFPlayerCharacter* Hero)
{
    UCreatureDefinition* Leaf = nullptr;
    for (TActorIterator<ACreatureCharacter> It(World); It; ++It)
    { if (It->Definition && It->Definition->Form == ECreatureForm::Leaf) Leaf = It->Definition; It->Destroy(); }
    if (!Leaf) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Hero->SetActorLocation(FVector(-350,-1050,40)); Hero->GetCharacterMovement()->DisableMovement();
    Hero->Inventory->Restore({{EFrontierItem::Material,100},{EFrontierItem::Food,10}});
    auto* Builder = Hero->Building.Get();
    auto At = [](float X) { return FTransform(FRotator::ZeroRotator,FVector(X,-700,-15),FVector(2,1,.7)); };
    Builder->Selected = EWorkSpecialty::Feeding; auto* Trough = Builder->BuildAt(At(0));
    Builder->Selected = EWorkSpecialty::Farming; auto* Garden = Builder->BuildAt(At(-700));
    if (!Trough || !Garden) { UE_LOG(LogTemp,Error,TEXT("CF_FEED FAIL build")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Garden->Crop->Seeds=3; // The return-to-work assertion includes seeded production after eating.
    const FGuid Id = Trough->PersistentId, GardenId = Garden->PersistentId;
    Hero->FirstPersonCamera->SetWorldRotation((Trough->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());
    Hero->DepositTroughFood(); Hero->DepositTroughFood();
    bool Transfer = Trough->StoredFood == 10 && Hero->Inventory->Count(EFrontierItem::Food) == 0 && !Trough->TransferFood(Hero,true);
    Hero->WithdrawTroughFood(); Transfer &= Trough->StoredFood == 5 && Hero->Inventory->Count(EFrontierItem::Food) == 5;
    Trough->StoredFood = 30; Transfer &= !Trough->TransferFood(Hero,true) && Hero->Inventory->Count(EFrontierItem::Food) == 5;
    Hero->Inventory->Restore({{EFrontierItem::Material,70},{EFrontierItem::Food,999}});
    Transfer &= !Trough->TransferFood(Hero,false) && Trough->StoredFood == 30;
    Hero->Inventory->Restore({{EFrontierItem::Material,70}}); Trough->StoredFood = 6;
    auto Make = [&](FVector Position)
    {
        const FTransform Transform(FRotator::ZeroRotator,Position);
        auto* Pet = World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Pet->Definition = Leaf; Pet->FinishSpawning(Transform); Pet->Tame(Hero->GetController());
        Pet->SetActorTickEnabled(false); Pet->GetCharacterMovement()->DisableMovement(); Pet->Nutrition = 30; Pet->Stamina = 20;
        return Pet;
    };
    auto* First = Make(FVector(-150,-800,40)); auto* Second = Make(FVector(150,-800,40));
    bool Serve = Trough->ServeFood(First) && !Trough->ServeFood(First) && Trough->ServeFood(Second) && Trough->StoredFood == 4 && First->Nutrition == 65 && First->Stamina == 35;
    First->AutoFeedCooldown = 0; First->Nutrition = 30; First->SetActivity(ECreatureActivity::Combat);
    Serve &= !Trough->ServeFood(First); First->SetActivity(ECreatureActivity::Follow);
    First->Health = 0; Serve &= !Trough->ServeFood(First); First->Health = First->GetMaxHealth();
    bool Remove = !Builder->DismantleStation(Trough) && IsValid(Trough) && Hero->Inventory->Count(EFrontierItem::Material) == 70;
    First->SetActorLocation(FVector(-500,-700,40)); First->Stamina = 100; First->WorkSite = Garden; First->SetActivity(ECreatureActivity::Work);
    First->GetCharacterMovement()->SetMovementMode(MOVE_Walking); First->SetActorTickEnabled(true);
    FTimerHandle AfterTrip;
    World->GetTimerManager().SetTimer(AfterTrip,[World,Hero,First,Id,GardenId,Transfer,Serve,Remove]() mutable
    {
        AWorkStation* Trough = nullptr;
        for (TActorIterator<AWorkStation> It(World); It; ++It) if (It->PersistentId == Id) Trough = *It;
        UE_LOG(LogTemp,Display,TEXT("CF_FEED_TRIP pos=%s food=%d nutrition=%.2f activity=%d jobDist=%.1f bag=%d cooldown=%.2f seeking=%d"),
            *First->GetActorLocation().ToString(),Trough ? Trough->StoredFood : -1,First->Nutrition,static_cast<int32>(First->Activity),
            First->WorkSite ? First->GetDistanceTo(First->WorkSite) : -1,Hero->Inventory->Count(EFrontierItem::Food),First->AutoFeedCooldown,First->FeedingSite.IsValid());
        const bool Trip = Trough && Trough->StoredFood == 3 && First->Nutrition > 50 && First->WorkSite && First->WorkSite->PersistentId == GardenId &&
            First->Activity == ECreatureActivity::Work && FVector::Dist2D(First->GetActorLocation(),First->WorkSite->GetActorLocation()) <= 220 && First->WorkSite->StoredFood>0;
        if (!Trough) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
        First->FeedingSite = Trough; First->Nutrition = 30; First->AutoFeedCooldown = 0; First->Tick(0);
        const bool Timeout = !First->FeedingSite.IsValid() && First->AutoFeedCooldown == 5; // Expired transient target falls back to its job.
        First->Nutrition = 65; First->AutoFeedCooldown = 2.5f;
        const FString Slot = TEXT("FrontierFeedingAutomation");
        bool Saved = Hero->SaveProgress(Slot) && Hero->LoadProgress(Slot);
        UE_LOG(LogTemp,Display,TEXT("CF_FEED_SAVE loaded=%d feedback=%s"),Saved,*Hero->Feedback);
        Trough = nullptr;
        int32 Count = 0;
        for (TActorIterator<AWorkStation> It(World); It; ++It) if (It->bPlayerBuilt)
        { ++Count; if (It->PersistentId == Id) Trough = *It; }
        Saved &= Count == 2 && Trough && Trough->StoredFood == 3;
        bool Cooldown = false;
        for (TActorIterator<ACreatureCharacter> It(World); It; ++It)
            if (It->WorkSite && It->WorkSite->PersistentId == GardenId) Cooldown = It->AutoFeedCooldown == 2.5f;
        Saved &= Cooldown;
        auto* Bad = Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0));
        bool Reject = Bad != nullptr;
        if (Bad)
        {
            for (auto& Record : Bad->Facilities) if (Record.Id == Id) Record.StoredFood = 31;
            Reject &= UGameplayStatics::SaveGameToSlot(Bad,TEXT("FrontierInvalidTroughAutomation"),0) && !Hero->LoadProgress(TEXT("FrontierInvalidTroughAutomation"));
        }
        if (Trough)
        {
            Remove &= !Hero->Building->DismantleStation(Trough);
            Remove &= Trough->TransferFood(Hero,false) && Trough->StoredFood == 0 && Hero->Building->DismantleStation(Trough);
            Remove &= Hero->LoadProgress(Slot);
            int32 Restored = 0;
            for (TActorIterator<AWorkStation> It(World); It; ++It) if (It->PersistentId == Id) { ++Restored; Remove &= It->StoredFood == 3; }
            Remove &= Restored == 1;
        }
        const bool Pass = Transfer && Serve && Trip && Timeout && Saved && Reject && Remove;
        UE_LOG(LogTemp,Display,TEXT("CF_FEED %s transfer=%d serve=%d trip=%d timeout=%d save=%d reject=%d dismantle=%d"),Pass ? TEXT("PASS") : TEXT("FAIL"),Transfer,Serve,Trip,Timeout,Saved,Reject,Remove);
        FPlatformMisc::RequestExitWithStatus(false,Pass ? 0 : 1);
    },10.0f,false);
}
