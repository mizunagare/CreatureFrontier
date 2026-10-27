#include "SquadScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "WorkStation.h"
#include "BaseBuildingComponent.h"
#include "InventoryComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"

void RunSquadScenario(UWorld* World, ACFPlayerCharacter* Hero)
{
    TMap<ECreatureForm,UCreatureDefinition*> Definitions;
    for (TActorIterator<ACreatureCharacter> It(World); It; ++It) { if (It->Definition) Definitions.Add(It->Definition->Form,It->Definition); It->Destroy(); }
    if (Definitions.Num() != 3) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Hero->SetActorLocation(FVector(-350,-1050,40)); Hero->GetCharacterMovement()->DisableMovement(); Hero->Inventory->Restore({{EFrontierItem::Material,100}});
    Hero->Building->Selected = EWorkSpecialty::Farming;
    auto* Garden = Hero->Building->BuildAt(FTransform(FRotator::ZeroRotator,FVector(0,-700,-15),FVector(2,1,.7)));
    AWorkStation* Mine = nullptr;
    for (TActorIterator<AWorkStation> It(World); It; ++It) if (It->RequiredSpecialty == EWorkSpecialty::Mining) Mine = *It;
    if (!Garden || !Mine) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    auto Make = [&](ECreatureForm Form,FVector Position)
    {
        const FTransform Transform(FRotator::ZeroRotator,Position);
        auto* Pet = World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Pet->Definition = Definitions[Form]; Pet->FinishSpawning(Transform); Pet->Tame(Hero->GetController());
        Pet->SetActorTickEnabled(false); Pet->GetCharacterMovement()->DisableMovement(); return Pet;
    };
    auto* Leaf = Make(ECreatureForm::Leaf,FVector(0,-900,40));
    auto* Stone = Make(ECreatureForm::Stone,FVector(-500,-900,40));
    auto* Ember = Make(ECreatureForm::Ember,FVector(-900,-900,40));
    auto Aim = [&](AActor* Target) { Hero->FirstPersonCamera->SetWorldRotation((Target->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation()); };
    Aim(Leaf); Hero->SelectAimedCompanion();
    bool Select = Hero->IsCommandTarget(Leaf) && !Hero->IsCommandTarget(Stone) && !Hero->IsCommandTarget(Ember);
    Hero->CommandWork();
    bool Split = Leaf->WorkSite == Garden && Leaf->Activity == ECreatureActivity::Work && !Stone->WorkSite && !Ember->WorkSite;
    Select &= Hero->SelectCompanion(Stone); Hero->SetActorLocation(FVector(-650,-500,40)); Hero->CommandWork();
    Split &= Stone->WorkSite == Mine && Stone->Activity == ECreatureActivity::Work && Leaf->WorkSite == Garden && Leaf->Activity == ECreatureActivity::Work;
    Hero->CommandRest();
    bool ScopedRest = Stone->Activity == ECreatureActivity::Rest && Leaf->Activity == ECreatureActivity::Work;
    Hero->CommandFollow();
    ScopedRest &= Stone->Activity == ECreatureActivity::Follow && !Stone->WorkSite && Leaf->WorkSite == Garden;
    Select &= Hero->SelectCompanion(Ember);
    auto* Enemy = World->SpawnActor<ACreatureCharacter>(FVector(-650,-1100,40),FRotator::ZeroRotator);
    Enemy->Health = 1000; Enemy->SetActorTickEnabled(false); Enemy->GetCharacterMovement()->DisableMovement();
    Aim(Enemy); Hero->CommandFocus();
    bool Combat = Ember->FocusTarget == Enemy && !Leaf->FocusTarget && !Stone->FocusTarget;
    Hero->CommandSpecial(); Ember->Tick(0);
    Combat &= Ember->SpecialWindupRemaining > 0 && Ember->Stamina == 80 && Leaf->Stamina == 100 && Stone->Stamina == 100;
    Hero->CommandSpecial(); Stone->Tick(0);
    Combat &= Stone->SpecialWindupRemaining == 0 && Stone->Stamina == 100;
    bool Reject = !Hero->SelectCompanion(Enemy) && Hero->IsCommandTarget(Ember);
    const FVector Old = Leaf->GetActorLocation(); Leaf->SetActorLocation(FVector(1500,-900,40));
    Reject &= !Hero->SelectCompanion(Leaf) && Hero->IsCommandTarget(Ember); Leaf->SetActorLocation(Old);
    Ember->Health = 0; Hero->CommandFollow();
    bool Lost = !Hero->IsCommandTarget(Leaf) && !Hero->IsCommandTarget(Stone) && !Hero->IsCommandTarget(Ember) && Leaf->Activity == ECreatureActivity::Work;
    Ember->Destroy(); Hero->CommandFollow();
    Lost &= !Hero->IsCommandTarget(Leaf) && Leaf->WorkSite == Garden;
    Hero->ClearCompanionSelection(); const bool All = Hero->IsCommandTarget(Leaf) && Hero->IsCommandTarget(Stone);
    Hero->SelectCompanion(Leaf);
    const FString Slot = TEXT("FrontierSquadAutomation");
    bool Saved = Hero->SaveProgress(Slot) && Hero->LoadProgress(Slot);
    Leaf = nullptr; Stone = nullptr;
    for (TActorIterator<ACreatureCharacter> It(World); It; ++It) if (It->Tamer == Hero->GetController())
    { if (It->Definition->Form == ECreatureForm::Leaf) Leaf = *It; if (It->Definition->Form == ECreatureForm::Stone) Stone = *It; }
    Saved &= Leaf && Stone && Leaf->WorkSite && Leaf->WorkSite->RequiredSpecialty == EWorkSpecialty::Farming && !Hero->IsCommandTarget(Leaf) && !Hero->IsCommandTarget(Stone);
    Hero->CommandFollow(); Saved &= Leaf && Leaf->Activity == ECreatureActivity::Work;
    Hero->ClearCompanionSelection(); Hero->CommandFollow();
    const bool Reset = Leaf && Stone && Leaf->Activity == ECreatureActivity::Follow && Stone->Activity == ECreatureActivity::Follow && !Leaf->WorkSite;
    const bool Pass = Select && Split && ScopedRest && Combat && Reject && Lost && All && Saved && Reset;
    UE_LOG(LogTemp,Display,TEXT("CF_SQUAD %s select=%d split=%d rest=%d combat=%d reject=%d lost=%d all=%d save=%d reset=%d"),
        Pass ? TEXT("PASS") : TEXT("FAIL"),Select,Split,ScopedRest,Combat,Reject,Lost,All,Saved,Reset);
    FPlatformMisc::RequestExitWithStatus(false,Pass ? 0 : 1);
}
