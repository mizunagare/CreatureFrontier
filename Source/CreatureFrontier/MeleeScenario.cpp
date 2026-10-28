#include "MeleeScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "PlayerEquipmentComponent.h"
#include "BaseBuildingComponent.h"
#include "FrontierSaveGame.h"
#include "InventoryComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "TimerManager.h"

void RunMeleeScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    for(TActorIterator<ACreatureCharacter> It(World);It;++It) It->Destroy();
    const FVector Origin(-350,-400,40);
    Hero->SetActorLocation(Origin); Hero->GetCharacterMovement()->DisableMovement(); Hero->Inventory->Restore({{EFrontierItem::Material,100}});
    FPlayerEquipmentState Gear; Gear.bOwnSpear=true; Gear.bSpearEquipped=true; Hero->Equipment->Restore(Gear);
    auto* Victim=World->SpawnActor<ACreatureCharacter>(Origin+FVector(300,0,0),FRotator::ZeroRotator); Victim->Level=20; Victim->Health=400; Victim->SetActorTickEnabled(false); Victim->GetCharacterMovement()->DisableMovement();
    struct FTrial
    {
        int32 Step=0;
        bool Timing=true,Range=true,Cover=true,Friendly=true,Tracking=true,Cancel=true,Snapshot=true,Death=true,Bare=true;
        TWeakObjectPtr<ACreatureCharacter> Target,Ally;
        TWeakObjectPtr<AActor> Wall;
    };
    auto Trial=MakeShared<FTrial>(); Trial->Target=Victim;
    auto Timer=MakeShared<FTimerHandle>();
    World->GetTimerManager().SetTimer(*Timer,[World,Hero,Origin,Trial,Timer]()
    {
        auto* Target=Trial->Target.Get();
        if(!Target) { UE_LOG(LogTemp,Error,TEXT("CF_MELEE FAIL target missing")); FPlatformMisc::RequestExitWithStatus(false,1); World->GetTimerManager().ClearTimer(*Timer); return; }
        auto Aim=[Hero](AActor* Actor)
        {
            const FRotator Look=(Actor->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
            Hero->GetController()->SetControlRotation(Look); Hero->FirstPersonCamera->SetWorldRotation(Look);
        };
        switch(Trial->Step++)
        {
            case 0:
                Aim(Target); Hero->Attack(); Trial->Timing &= Hero->IsMeleePending() && Target->Health==400;
                Hero->Attack(); Trial->Timing &= Target->Health==400; break;
            case 1:
                Trial->Timing &= Target->Health==365 && !Hero->IsMeleePending();
                Target->SetActorLocation(Origin+FVector(700,0,0)); Aim(Target); Hero->Attack(); break;
            case 2:
            {
                Trial->Range &= Target->Health==365;
                Target->SetActorLocation(Origin+FVector(300,0,0)); Aim(Target);
                auto* Wall=World->SpawnActor<AActor>(); auto* Mesh=NewObject<UStaticMeshComponent>(Wall); Wall->SetRootComponent(Mesh); Mesh->RegisterComponent();
                Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"))); Mesh->SetCollisionProfileName(TEXT("BlockAll"));
                Wall->SetActorLocation(Origin+FVector(150,0,50)); Wall->SetActorScale3D(FVector(.2,2,3)); Trial->Wall=Wall;
                Hero->Attack(); break;
            }
            case 3:
            {
                Trial->Cover &= Target->Health==365; Trial->Wall->Destroy();
                auto* Ally=World->SpawnActor<ACreatureCharacter>(Origin+FVector(150,0,0),FRotator::ZeroRotator); Ally->Tame(Hero->GetController()); Ally->SetActorTickEnabled(false); Ally->GetCharacterMovement()->DisableMovement();
                Trial->Ally=Ally; Aim(Target); Hero->Attack(); break;
            }
            case 4:
                Trial->Friendly &= Target->Health==365 && Trial->Ally->Health==100;
                Trial->Ally->Destroy(); Aim(Target); Hero->Attack(); Target->SetActorLocation(Origin+FVector(700,0,0)); break;
            case 5:
                Trial->Tracking &= Target->Health==365;
                Target->SetActorLocation(Origin+FVector(300,0,0)); Aim(Target); Hero->Attack();
                Hero->GetController()->SetControlRotation(FRotator(0,90,0)); Hero->FirstPersonCamera->SetWorldRotation(FRotator(0,90,0)); break;
            case 6:
                Trial->Tracking &= Target->Health==365;
                Aim(Target); Hero->Attack(); Hero->ToggleSpear(); Trial->Cancel &= !Hero->IsMeleePending(); break;
            case 7:
                Trial->Cancel &= Target->Health==365; Hero->ToggleSpear(); Aim(Target); Hero->Attack(); Hero->Building->Toggle(); Trial->Cancel &= !Hero->IsMeleePending(); break;
            case 8:
            {
                Trial->Cancel &= Target->Health==365; Hero->Building->Cancel(); Aim(Target);
                const FString Slot=TEXT("FrontierMeleeAutomation"); Trial->Snapshot &= Hero->SaveProgress(Slot);
                Hero->Attack(); Trial->Snapshot &= Hero->IsMeleePending() && Hero->LoadProgress(Slot) && !Hero->IsMeleePending();
                for(TActorIterator<ACreatureCharacter> It(World);It;++It) { Trial->Target=*It; It->SetActorTickEnabled(false); It->GetCharacterMovement()->DisableMovement(); break; }
                Hero->GetCharacterMovement()->DisableMovement(); break;
            }
            case 9:
                Trial->Snapshot &= Target->Health==365; Aim(Target); Hero->Attack();
                UGameplayStatics::ApplyDamage(Hero,5000,nullptr,nullptr,nullptr); Trial->Death &= Hero->Health==0 && !Hero->IsMeleePending(); break;
            case 10:
                Trial->Death &= Target->Health==365 && Hero->RespawnAtBase(); Hero->SetActorLocation(Origin); Hero->GetCharacterMovement()->DisableMovement();
                Hero->ToggleSpear(); Target->SetActorLocation(Origin+FVector(150,0,0)); Aim(Target); Hero->Attack();
                Trial->Bare &= Target->Health==340 && !Hero->IsMeleePending(); Hero->Attack(); Trial->Bare &= Target->Health==340; break;
            case 11:
                Target->SetActorLocation(Origin+FVector(700,0,0)); Aim(Target); Hero->Attack(); Trial->Bare &= Target->Health==340; break;
            default:
            {
                const bool Pass=Trial->Timing && Trial->Range && Trial->Cover && Trial->Friendly && Trial->Tracking && Trial->Cancel && Trial->Snapshot && Trial->Death && Trial->Bare && Target->Health==340;
                UE_LOG(LogTemp,Display,TEXT("CF_MELEE %s timing=%d range=%d cover=%d friendly=%d tracking=%d cancel=%d snapshot=%d death=%d bare=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Trial->Timing,Trial->Range,Trial->Cover,Trial->Friendly,Trial->Tracking,Trial->Cancel,Trial->Snapshot,Trial->Death,Trial->Bare);
                FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
                // 清除循环定时器会销毁捕获变量：此行之后不再访问。
                World->GetTimerManager().ClearTimer(*Timer); break;
            }
        }
    },.65f,true);
}
