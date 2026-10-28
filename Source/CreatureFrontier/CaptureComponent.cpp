#include "CaptureComponent.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "CFPlayerCharacter.h"
#include "InventoryComponent.h"
#include "GameFramework/Pawn.h"
#include "CraftingCatalog.h"
#include "Kismet/GameplayStatics.h"

UCaptureComponent::UCaptureComponent()
{
    SetIsReplicatedByDefault(true);
}

float UCaptureComponent::CalculateCaptureChance(const ACreatureCharacter* Creature) const
{
    if (!IsValid(Creature) || Creature->Health <= 0.0f ||
        Creature->Disposition == ECreatureDisposition::Tamed || Creature->Activity == ECreatureActivity::ReturnHome)
    {
        return 0.0f;
    }

    // 受伤生物更容易捕捉，但成功率始终设有上限，以保留不确定性。
    const float InjuryBonus = 1.0f + (1.0f - Creature->GetHealthRatio()) * 1.5f;
    const float BaseRate = Creature->Definition ? Creature->Definition->BaseCaptureRate : 0.35f;
    float Multiplier=0;
    if(const auto* Catalog=UCraftingCatalog::Load()) for(const auto& Item:Catalog->Items)
        if(Item.Item==(bUseResonance?EFrontierItem::ResonanceDevice:EFrontierItem::CaptureDevice)) Multiplier=Item.CaptureMultiplier;
    return Multiplier>0?FMath::Clamp(BaseRate * CapturePower * InjuryBonus * Multiplier, 0.01f, 0.95f):0;
}
void UCaptureComponent::ToggleDevice()
{
    auto* Hero=Cast<ACFPlayerCharacter>(GetOwner()); if(!Hero || !Hero->HasAuthority() || Hero->Health<=0 || UGameplayStatics::IsGamePaused(this)) return;
    bUseResonance=!bUseResonance;
    Hero->Feedback=bUseResonance?TEXT("已选共鸣捕捉，仅消耗共鸣器 | 0 普通捕捉"):TEXT("已选普通捕捉，仅消耗普通捕捉器 | 0 共鸣捕捉");
}

void UCaptureComponent::TryCapture_Implementation(ACreatureCharacter* Creature)
{
    if (GetWorld()->GetTimeSeconds() < NextCaptureTime) return;
    NextCaptureTime = GetWorld()->GetTimeSeconds() + 0.8;
    APawn* Pawn = Cast<APawn>(GetOwner());
    if (!Pawn || !Pawn->GetController() || !IsValid(Creature) ||
        FVector::DistSquared(Pawn->GetActorLocation(), Creature->GetActorLocation()) > FMath::Square(1300.0f)) return;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CaptureVisibility), false, Pawn);
    if (GetWorld()->LineTraceSingleByChannel(Hit, Pawn->GetPawnViewLocation(), Creature->GetActorLocation(), ECC_Visibility, Params)
        && Hit.GetActor() != Creature) return;
    const float Chance = CalculateCaptureChance(Creature);
    if (Chance <= 0.0f) return;
    auto* Hero = Cast<ACFPlayerCharacter>(Pawn);
    if (!Hero || Hero->Health <= 0.0f) return;
    if (!Hero->Inventory->Consume(bUseResonance?EFrontierItem::ResonanceDevice:EFrontierItem::CaptureDevice, 1))
    {
        Hero->Feedback = bUseResonance?TEXT("没有共鸣捕捉器，请完成水晶研究后制作；不会自动改用普通捕捉器"):TEXT("没有普通捕捉器，请在工作台按1制作");
        return;
    }
    const bool bSucceeded = Chance > 0.0f && FMath::FRand() <= Chance;
    if (bSucceeded)
    {
        APawn* OwnerPawn = Cast<APawn>(GetOwner());
        Creature->Tame(OwnerPawn ? OwnerPawn->GetController() : nullptr);
    }
    OnCaptureResolved.Broadcast(Creature, bSucceeded);
}

