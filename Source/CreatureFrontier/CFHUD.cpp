#include "CFHUD.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CaptureComponent.h"
#include "InventoryComponent.h"
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
    DrawRect(FLinearColor(0.02f,0.03f,0.05f,0.85f), 18, 18, 550, 260);
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
    DrawText(Player->Feedback, FLinearColor::Yellow, 32, 222);
    if (auto* Creature = Player->TraceCreature())
    {
        DrawText(FString::Printf(TEXT("%s  Lv%d  HP %.0f/%.0f  Capture %.0f%%"),
            Creature->Disposition == ECreatureDisposition::Tamed ? TEXT("Companion") : TEXT("Wild creature"),
            Creature->Level, Creature->Health, Creature->GetMaxHealth(), Player->CaptureComponent->CalculateCaptureChance(Creature)*100.0f),
            FLinearColor::White, X-130, Y+25);
        if (Creature->Disposition == ECreatureDisposition::Tamed)
            DrawText(FString::Printf(TEXT("XP %d/%d  Stamina %.0f%%  %s"), Creature->Experience, Creature->Level*100,
                Creature->Stamina, *UEnum::GetValueAsString(Creature->Activity)), FLinearColor::Green, X-130, Y+48);
    }
}
