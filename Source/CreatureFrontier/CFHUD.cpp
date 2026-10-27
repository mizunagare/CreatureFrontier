#include "CFHUD.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CaptureComponent.h"
#include "InventoryComponent.h"
#include "BaseBuildingComponent.h"
#include "WildEncounter.h"
#include "Engine/Canvas.h"
#include "EngineUtils.h"

void ACFHUD::DrawHUD()
{
    Super::DrawHUD();
    auto* Player = Cast<ACFPlayerCharacter>(GetOwningPawn());
    if (!Canvas || !Player) return;
    const float X = Canvas->ClipX * 0.5f, Y = Canvas->ClipY * 0.5f;
    DrawLine(X-7, Y, X+7, Y, FLinearColor::White);
    DrawLine(X, Y-7, X, Y+7, FLinearColor::White);
    DrawRect(FLinearColor(0.02f,0.03f,0.05f,0.85f), 18, 18, 550, 320);
    DrawText(FString::Printf(TEXT("HEALTH %.0f / 100    MATERIALS %d"), Player->Health, Player->Inventory->Count(EFrontierItem::Material)), FLinearColor::White, 32, 30);
    int32 Companions = 0, Working = 0;
    for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
        if (It->Tamer == Player->GetController()) { ++Companions; if (It->Activity == ECreatureActivity::Work) ++Working; }
    DrawText(FString::Printf(TEXT("COMPANIONS %d    WORKING %d"), Companions, Working), FLinearColor::Green, 32, 57);
    DrawText(TEXT("WASD move | Space jump | LMB hit | RMB capture"), FLinearColor::White, 32, 84);
    DrawText(TEXT("F follow | E work | R restart | F5 save | F9 load"), FLinearColor::White, 32, 108);
    DrawText(FString::Printf(TEXT("DEVICES %d    MEDICINE %d"), Player->Inventory->Count(EFrontierItem::CaptureDevice), Player->Inventory->Count(EFrontierItem::Medicine)), FLinearColor::White, 32, 136);
    DrawText(TEXT("1 craft device (3) | 2 craft medicine (5) | H heal"), FLinearColor::White, 32, 162);
    DrawText(TEXT("T rest at base | P treat aimed companion | Esc exit"), FLinearColor::White, 32, 190);
    DrawText(TEXT("Q focus aimed enemy | G companion special (20 stamina)"), FLinearColor::White, 32, 216);
    DrawText(TEXT("B build mode | V cycle facility | X dismantle (50% refund)"), FLinearColor::White,32,242);
    DrawText(Player->Feedback, FLinearColor::Yellow, 32, 278);
    if (Player->Building->IsActive()) DrawText(Player->Building->GetHint(), FLinearColor::Yellow,32,350);
    float RegionY = Canvas->ClipY - 90;
    for (TActorIterator<AWildEncounter> It(GetWorld()); It; ++It)
        if (Player->GetDistanceTo(*It) < 1800)
        {
            DrawText(It->GetStatus(), It->bBoss ? FLinearColor(1,0.5f,0.2f) : FLinearColor::Green, 32, RegionY);
            RegionY += 22;
        }
    if (auto* Creature = Player->TraceCreature())
    {
        if (Creature->Activity == ECreatureActivity::ReturnHome)
            DrawText(TEXT("RETURNING TO HABITAT - damage and capture disabled"),FLinearColor::Yellow,X-130,Y-30);
        if (Creature->bAlpha && Creature->Disposition != ECreatureDisposition::Tamed)
            DrawText(FString::Printf(TEXT("ALPHA - Crystal Quake %s %.1fs"), Creature->AlphaWindup > 0 ? TEXT("INCOMING") : TEXT("cooldown"),
                Creature->AlphaWindup > 0 ? Creature->AlphaWindup : Creature->AlphaCooldown), FLinearColor(1,.3f,.05f), X-130, Y+115);
        DrawText(FString::Printf(TEXT("%s (%s)  Lv%d  HP %.0f/%.0f  Capture %.0f%%"),
            *Creature->GetCreatureName(), Creature->Disposition == ECreatureDisposition::Tamed ? TEXT("Companion") : TEXT("Wild"),
            Creature->Level, Creature->Health, Creature->GetMaxHealth(), Player->CaptureComponent->CalculateCaptureChance(Creature)*100.0f),
            FLinearColor::White, X-130, Y+25);
        if (Creature->Disposition == ECreatureDisposition::Tamed)
        {
            DrawText(FString::Printf(TEXT("XP %d/%d  Stamina %.0f%%  %s"), Creature->Experience, Creature->Level*100,
                Creature->Stamina, *UEnum::GetValueAsString(Creature->Activity)), FLinearColor::Green, X-130, Y+48);
            DrawText(FString::Printf(TEXT("%s | cooldown %.1fs %s"), *Creature->GetSpecialName(), Creature->SpecialCooldownRemaining,
                Creature->SpecialWindupRemaining > 0 ? TEXT("CHARGING") : TEXT("")), FLinearColor::Yellow, X-130, Y+70);
        }
        if (Creature->SlowRemaining > 0 || Creature->GuardRemaining > 0)
            DrawText(FString::Printf(TEXT("SLOW %.1fs  GUARD %.1fs"), Creature->SlowRemaining, Creature->GuardRemaining), FLinearColor::Yellow, X-130, Y+92);
    }
}
