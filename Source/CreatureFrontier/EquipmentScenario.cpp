#include "EquipmentScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "PlayerEquipmentComponent.h"
#include "InventoryComponent.h"
#include "FrontierSaveGame.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "TimerManager.h"

void RunEquipmentScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    for(TActorIterator<ACreatureCharacter> It(World);It;++It) It->Destroy();
    auto* Gear=Hero->Equipment.Get(); Hero->Inventory->Restore({{EFrontierItem::Material,100}});
    Hero->GetCharacterMovement()->DisableMovement(); Hero->SetActorLocation(FVector(1500,-1200,40));
    Hero->CraftSpear(); bool Gates=!Gear->GetState().bOwnSpear && Hero->Inventory->Count(EFrontierItem::Material)==100;
    Hero->SetActorLocation(FVector(-350,-400,40)); Hero->Health=0; Hero->CraftVest(); Gates &= !Gear->GetState().bOwnVest;
    Hero->Health=100; Hero->Inventory->Restore({{EFrontierItem::Material,7}}); Hero->CraftSpear(); Gates &= !Gear->GetState().bOwnSpear && Hero->Inventory->Count(EFrontierItem::Material)==7;
    Hero->ToggleSpear(); Hero->ToggleVest(); Gates &= !Gear->GetState().bSpearEquipped && !Gear->GetState().bVestEquipped;
    Hero->Inventory->Restore({{EFrontierItem::Material,100}}); Hero->CraftSpear(); Hero->CraftVest();
    bool Craft=Gear->GetState().bOwnSpear && Gear->GetState().bOwnVest && Gear->GetState().bSpearEquipped && Gear->GetState().bVestEquipped && Hero->Inventory->Count(EFrontierItem::Material)==80;
    Hero->CraftSpear(); Hero->CraftVest(); Craft &= Hero->Inventory->Count(EFrontierItem::Material)==80;
    Hero->ToggleSpear(); bool Toggle=Hero->GetPlayerAttackDamage()==25 && !Gear->IsWeaponVisible(); Hero->CraftSpear(); Toggle &= Hero->Inventory->Count(EFrontierItem::Material)==80 && !Gear->GetState().bSpearEquipped;
    Hero->ToggleSpear(); Toggle &= Hero->GetPlayerAttackDamage()==35 && Gear->IsWeaponVisible();
    Hero->ToggleVest(); const float BareDamage=UGameplayStatics::ApplyDamage(Hero,20,nullptr,nullptr,nullptr);
    Hero->ToggleVest(); const float ArmoredDamage=UGameplayStatics::ApplyDamage(Hero,20,nullptr,nullptr,nullptr);
    bool Armor=BareDamage==20 && FMath::IsNearlyEqual(ArmoredDamage,16.f) && FMath::IsNearlyEqual(Hero->Health,64.f);
    auto* Enemy=World->SpawnActor<ACreatureCharacter>(FVector(-50,-400,40),FRotator::ZeroRotator); Enemy->Level=6; Enemy->Health=200; Enemy->SetActorTickEnabled(false); Enemy->GetCharacterMovement()->DisableMovement();
    const FRotator Aim=(Enemy->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation(); Hero->GetController()->SetControlRotation(Aim); Hero->FirstPersonCamera->SetWorldRotation(Aim);
    Hero->Attack(); const bool Early=Enemy->Health==200 && Hero->IsMeleePending();
    TArray<UStaticMeshComponent*> Pieces; Hero->GetComponents(Pieces);
    bool Visual=Gear->GetWeaponParts().Num()==2; Gear->TickComponent(.1f,LEVELTICK_All,nullptr);
    UE_LOG(LogTemp,Display,TEXT("CF_EQUIPMENT_VISUAL count=%d"),Gear->GetWeaponParts().Num());
    for(const auto& Piece:Gear->GetWeaponParts())
    {
        UE_LOG(LogTemp,Display,TEXT("CF_EQUIPMENT_PART %s collision=%d nav=%d parentX=%.2f"),*Piece->GetName(),static_cast<int32>(Piece->GetCollisionEnabled()),Piece->CanEverAffectNavigation(),Piece->GetAttachParent()->GetRelativeLocation().X);
        Visual &= Piece->GetCollisionEnabled()==ECollisionEnabled::NoCollision && !Piece->CanEverAffectNavigation() && Piece->GetAttachParent()->GetRelativeLocation().X>20;
    }
    const int32 MeshCount=Pieces.Num();
    FTimerHandle SpearHit;
    World->GetTimerManager().SetTimer(SpearHit,[World,Hero,Enemy,Gates,Craft,Toggle,Armor,Visual,Early,MeshCount]()
    {
    auto* Gear=Hero->Equipment.Get(); const bool Attack=Early && Enemy->Health==165;
    const FString Slot=TEXT("FrontierEquipmentAutomation");
    bool Saved=Hero->SaveProgress(Slot) && Hero->LoadProgress(Slot) && Hero->LoadProgress(Slot) && Hero->GetPlayerAttackDamage()==35 && FMath::IsNearlyEqual(Gear->GetDamageReduction(),.2f) && Hero->Inventory->Count(EFrontierItem::Material)==80;
    TArray<UStaticMeshComponent*> RestoredPieces; Hero->GetComponents(RestoredPieces); Saved &= RestoredPieces.Num()==MeshCount && Gear->IsWeaponVisible();
    auto* Invalid=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0)); Invalid->Equipment.bOwnSpear=false;
    UGameplayStatics::SaveGameToSlot(Invalid,TEXT("FrontierEquipmentInvalid"),0);
    bool Validation=!Hero->LoadProgress(TEXT("FrontierEquipmentInvalid")) && Hero->GetPlayerAttackDamage()==35 && Hero->Inventory->Count(EFrontierItem::Material)==80;
    Invalid->Equipment.bOwnSpear=true; Invalid->Equipment.bOwnVest=false;
    UGameplayStatics::SaveGameToSlot(Invalid,TEXT("FrontierEquipmentInvalid"),0);
    Validation &= !Hero->LoadProgress(TEXT("FrontierEquipmentInvalid")) && FMath::IsNearlyEqual(Gear->GetDamageReduction(),.2f) && Hero->Inventory->Count(EFrontierItem::Material)==80;
    auto* Legacy=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0)); Legacy->FormatVersion=13;
    UGameplayStatics::SaveGameToSlot(Legacy,TEXT("FrontierEquipmentLegacy"),0);
    bool Migration=Hero->LoadProgress(TEXT("FrontierEquipmentLegacy")) && !Gear->GetState().bOwnSpear && !Gear->GetState().bOwnVest && Hero->GetPlayerAttackDamage()==25 && Gear->GetDamageReduction()==0 && !Gear->IsWeaponVisible() && Hero->Inventory->Count(EFrontierItem::Material)==80;
    Saved &= Hero->LoadProgress(Slot);
    UGameplayStatics::ApplyDamage(Hero,1000,nullptr,nullptr,nullptr);
    bool Respawn=Hero->Health==0 && !Gear->IsWeaponVisible() && Hero->RespawnAtBase() && Hero->GetPlayerAttackDamage()==35 && FMath::IsNearlyEqual(Gear->GetDamageReduction(),.2f) && Gear->IsWeaponVisible() && Hero->Inventory->Count(EFrontierItem::Material)==80;
    Saved &= Hero->LoadProgress(Slot);
    FTimerHandle Unarmed;
    World->GetTimerManager().SetTimer(Unarmed,[World,Hero,Gates,Craft,Toggle,Armor,Attack,Visual,Saved,Validation,Migration,Respawn]()
    {
        ACreatureCharacter* Target=nullptr; for(TActorIterator<ACreatureCharacter> It(World);It;++It) { Target=*It; break; }
        bool Bare=false;
        if(Target)
        {
            Target->SetActorTickEnabled(false); Target->GetCharacterMovement()->DisableMovement();
            Target->SetActorLocation(Hero->GetActorLocation()+FVector(150,0,0));
            const float Before=Target->Health; Hero->ToggleSpear();
            const FRotator Aim=(Target->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation(); Hero->GetController()->SetControlRotation(Aim); Hero->FirstPersonCamera->SetWorldRotation(Aim);
            Hero->Attack(); Bare=FMath::IsNearlyEqual(Before-Target->Health,25.f) && !Hero->Equipment->IsWeaponVisible(); Hero->ToggleSpear();
        }
        const bool Pass=Gates && Craft && Toggle && Armor && Attack && Visual && Saved && Validation && Migration && Respawn && Bare;
        UE_LOG(LogTemp,Display,TEXT("CF_EQUIPMENT %s gates=%d craft=%d toggle=%d armor=%d attack=%d visual=%d save=%d invalid=%d migration=%d respawn=%d bare=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Gates,Craft,Toggle,Armor,Attack,Visual,Saved,Validation,Migration,Respawn,Bare);
        if(Pass && FParse::Param(FCommandLine::Get(),TEXT("CFEquipmentPreview")))
        {
            Hero->Health=100; Hero->GetCharacterMovement()->DisableMovement();
            const FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("EquipmentPreview")); IFileManager::Get().MakeDirectory(*Root,true);
            FTimerHandle Shot,Unequip,Shot2,Done;
            World->GetTimerManager().SetTimer(Shot,[Root](){FScreenshotRequest::RequestScreenshot(Root/TEXT("equipped.png"),false,false);},3.f,false);
            World->GetTimerManager().SetTimer(Unequip,[Hero](){Hero->ToggleSpear();Hero->ToggleVest();},4.f,false);
            World->GetTimerManager().SetTimer(Shot2,[Root](){FScreenshotRequest::RequestScreenshot(Root/TEXT("unequipped.png"),false,false);},6.f,false);
            World->GetTimerManager().SetTimer(Done,[](){FPlatformMisc::RequestExitWithStatus(false,0);},8.f,false); return;
        }
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    },.6f,false);
    },.2f,false);
}
