#include "CaptureComponent.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "CFPlayerCharacter.h"
#include "InventoryComponent.h"
#include "GameFramework/Pawn.h"

UCaptureComponent::UCaptureComponent()
{
    SetIsReplicatedByDefault(true);
}

float UCaptureComponent::CalculateCaptureChance(const ACreatureCharacter* Creature) const
{
    if (!IsValid(Creature) || Creature->Health <= 0.0f ||
        Creature->Disposition == ECreatureDisposition::Tamed)
    {
        return 0.0f;
    }

    // Injured creatures are easier to capture. Chance is always capped to keep tension.
    const float InjuryBonus = 1.0f + (1.0f - Creature->GetHealthRatio()) * 1.5f;
    const float BaseRate = Creature->Definition ? Creature->Definition->BaseCaptureRate : 0.35f;
    return FMath::Clamp(BaseRate * CapturePower * InjuryBonus, 0.01f, 0.95f);
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
    if (!Hero->Inventory->Consume(EFrontierItem::CaptureDevice, 1))
    {
        Hero->Feedback = TEXT("No capture devices - craft with 1 at work station");
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

