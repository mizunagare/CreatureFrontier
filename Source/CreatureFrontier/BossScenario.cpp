#include "BossScenario.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "CompanionPersonalityComponent.h"
#include "CFPlayerCharacter.h"
#include "WorldLoot.h"
#include "InventoryComponent.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformMisc.h"

void RunBossScenario(UWorld* World, ACFPlayerCharacter* Hero)
{
    UCreatureDefinition* Stone = nullptr;
    for (TActorIterator<ACreatureCharacter> It(World); It; ++It)
    {
        if (It->Definition && It->Definition->Form == ECreatureForm::Stone) Stone = It->Definition;
        It->Destroy();
    }
    if (!Stone) { UE_LOG(LogTemp, Error, TEXT("CF_BOSS FAIL missing definition")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    auto Make = [&](FVector Position, bool Alpha, bool Owned)
    {
        const FTransform Transform(FRotator::ZeroRotator, Position);
        auto* Pet = World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(), Transform,
            nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Pet->Definition = Stone; Pet->Level = 5; Pet->bAlpha = Alpha;
        Pet->FinishSpawning(Transform);
        Pet->SetActorTickEnabled(false); Pet->GetCharacterMovement()->DisableMovement();
        if (Owned) Pet->Tame(Hero->GetController());
        return Pet;
    };
    Hero->GetCharacterMovement()->DisableMovement();
    Hero->SetActorLocation(FVector(250,-1200,40));
    auto* Boss = Make(FVector(0,-1200,40), true, false);
    Boss->AlphaCooldown = 0;
    Boss->Tick(0); Boss->Tick(.1f);
    bool Visual = false;
    TArray<UStaticMeshComponent*> Meshes;
    Boss->GetComponents(Meshes);
    for (auto* Mesh : Meshes) if (Mesh->GetFName() == TEXT("AlphaWarning")) Visual = Mesh->IsVisible() && FMath::IsNearlyEqual(Mesh->GetRelativeScale3D().X,7.6);
    const bool Warning = Boss->AlphaWindup > 1 && Hero->Health == 100 && Visual;
    Hero->SetActorLocation(FVector(550,-1200,40));
    Boss->Tick(1.2f);
    const bool Dodge = Hero->Health == 100 && Boss->AlphaWindup == 0;
    Hero->SetActorLocation(FVector(250,-1200,40));
    Boss->Tick(.1f);
    const bool Cooldown = Boss->AlphaWindup == 0 && Boss->AlphaCooldown > 8;
    auto* Ally = Make(FVector(150,-1020,40), false, true);
    auto* Guard = Make(FVector(150,-1380,40), false, true);
    auto* Wild = Make(FVector(-250,-1200,40), false, false);
    Guard->GuardRemaining = 5;
    Boss->AlphaCooldown = 0; Boss->Tick(0); Boss->Tick(1.3f);
    const bool Damage = FMath::IsNearlyEqual(Hero->Health,61) && FMath::IsNearlyEqual(Ally->Health,201) &&
        FMath::IsNearlyEqual(Guard->Health,220.5f) && Wild->Health == 240;
    Hero->Health = 100;
    Boss->AlphaCooldown = 0; Boss->Tick(0);
    Hero->SetActorLocation(FVector(250,-1200,120));
    Boss->Tick(1.3f);
    const float JumpHeight = FMath::Square(Hero->GetCharacterMovement()->JumpZVelocity) / (2 * FMath::Abs(Hero->GetCharacterMovement()->GetGravityZ()));
    const bool Jump = Hero->Health == 100 && JumpHeight > 80;
    Hero->SetActorLocation(FVector(250,-1200,40));
    Boss->AlphaCooldown = 0; Boss->Tick(0);
    auto* Wall = World->SpawnActor<AStaticMeshActor>(FVector(125,-1200,40), FRotator::ZeroRotator);
    Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
    Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Wall->SetActorScale3D(FVector(.3f,.8f,2));
    Boss->Tick(1.3f);
    const bool Cover = Hero->Health == 100;
    Wall->Destroy();
    Boss->AlphaCooldown = 0; Boss->Tick(0);
    const FString Slot = TEXT("FrontierBossAutomation");
    bool Saved = Hero->SaveProgress(Slot) && Hero->LoadProgress(Slot);
    Boss = nullptr;
    for (TActorIterator<ACreatureCharacter> It(World); It; ++It)
        if (It->bAlpha) { Boss = *It; break; }
    Saved &= Boss && Boss->AlphaWindup == 0 && FMath::IsNearlyEqual(Boss->AlphaCooldown,10);
    if (!Boss) { UE_LOG(LogTemp, Error, TEXT("CF_BOSS FAIL restore")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Boss->SetActorTickEnabled(false);
    // 捕捉会取消敌对蓄力，但绝不生成击杀奖励。
    Boss->AlphaWindup = 1; Boss->Tame(Hero->GetController()); Boss->Tick(0);
    const bool Capture = Boss->AlphaWindup == 0;
    Boss->Disposition = ECreatureDisposition::Wild; Boss->Tamer = nullptr;
    Hero->SetActorLocation(FVector(550,-1200,40));
    UGameplayStatics::ApplyDamage(Boss,1000,Hero->GetController(),Hero,nullptr);
    UGameplayStatics::ApplyDamage(Boss,1000,Hero->GetController(),Hero,nullptr);
    AWorldLoot* Drop = nullptr; int32 Drops = 0;
    for (TActorIterator<AWorldLoot> It(World); It; ++It) { Drop = *It; ++Drops; It->SetActorTickEnabled(false); }
    bool Loot = Drops == 1 && Drop && Drop->Materials == 8 && Drop->Medicine == 1;
    if (!Drop) { UE_LOG(LogTemp, Error, TEXT("CF_BOSS FAIL loot")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Hero->Inventory->Restore({{EFrontierItem::Material,998},{EFrontierItem::Medicine,999}});
    Loot &= !Drop->Collect(Hero); // 距离过远。
    Hero->SetActorLocation(Drop->GetActorLocation()+FVector(100,0,55));
    Loot &= Drop->Collect(Hero) && Drop->Materials == 7 && Drop->Medicine == 1 && Hero->Inventory->Count(EFrontierItem::Material) == 999;
    Loot &= !Drop->Collect(Hero); // 背包已满时，剩余掉落物保留在世界中。
    // 此夹具将已捕捉的首领手动改回野生状态（目前没有放生功能）。
    // 必须拒绝它残留的伙伴专用偏好数据。
    const bool RejectedFixture=Hero->SaveProgress(Slot) && !Hero->LoadProgress(Slot) && Hero->Feedback.Contains(TEXT("伙伴偏好无效"));
    UE_LOG(LogTemp,Display,TEXT("CF_BOSS_FIXTURE rejectedRetainedPreference=%d"),RejectedFixture);
    Boss->Personality->RestoreSnapshot(ECompanionPreference::None,false);
    Saved &= RejectedFixture && Hero->SaveProgress(Slot) && Hero->LoadProgress(Slot);
    Drop = nullptr; Drops = 0;
    for (TActorIterator<AWorldLoot> It(World); It; ++It) { Drop = *It; ++Drops; }
    Loot &= Drops == 1 && Drop && Drop->Materials == 7 && Drop->Medicine == 1 && Drop->GetActorScale3D().Equals(FVector::OneVector);
    if (Drop)
    {
        Hero->Inventory->Consume(EFrontierItem::Material,7); Hero->Inventory->Consume(EFrontierItem::Medicine,1);
        Loot &= Drop->Collect(Hero) && Drop->IsActorBeingDestroyed() && Hero->Inventory->Count(EFrontierItem::Material) == 999 && Hero->Inventory->Count(EFrontierItem::Medicine) == 999;
    }
    const bool Pass = Warning && Dodge && Cooldown && Damage && Jump && Cover && Saved && Capture && Loot;
    UE_LOG(LogTemp, Display, TEXT("CF_BOSS %s warning=%d dodge=%d cooldown=%d damage=%d jump=%d cover=%d save=%d capture=%d loot=%d"),
        Pass ? TEXT("PASS") : TEXT("FAIL"), Warning,Dodge,Cooldown,Damage,Jump,Cover,Saved,Capture,Loot);
    FPlatformMisc::RequestExitWithStatus(false, Pass ? 0 : 1);
}
