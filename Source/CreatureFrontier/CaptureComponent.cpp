#include "CaptureComponent.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "GameFramework/Pawn.h"

UCaptureComponent::UCaptureComponent()
{
    SetIsReplicatedByDefault(true);
}

float UCaptureComponent::CalculateCaptureChance(const ACreatureCharacter* Creature) const
{
    if (!IsValid(Creature) || !Creature->Definition ||
        Creature->Disposition == ECreatureDisposition::Tamed)
    {
        return 0.0f;
    }

    // Injured creatures are easier to capture. Chance is always capped to keep tension.
    const float InjuryBonus = 1.0f + (1.0f - Creature->GetHealthRatio()) * 1.5f;
    return FMath::Clamp(Creature->Definition->BaseCaptureRate * CapturePower * InjuryBonus, 0.01f, 0.95f);
}

void UCaptureComponent::TryCapture_Implementation(ACreatureCharacter* Creature)
{
    const float Chance = CalculateCaptureChance(Creature);
    const bool bSucceeded = Chance > 0.0f && FMath::FRand() <= Chance;
    if (bSucceeded)
    {
        APawn* OwnerPawn = Cast<APawn>(GetOwner());
        Creature->Tame(OwnerPawn ? OwnerPawn->GetController() : nullptr);
    }
    OnCaptureResolved.Broadcast(Creature, bSucceeded);
}

