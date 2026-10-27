#include "CFHUD.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "PlayerEquipmentComponent.h"
#include "CaptureComponent.h"
#include "InventoryComponent.h"
#include "BaseBuildingComponent.h"
#include "WildEncounter.h"
#include "WorkStation.h"
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
    DrawRect(FLinearColor(0.02f,0.03f,0.05f,0.85f), 18, 18, 550, 410);
    DrawText(FString::Printf(TEXT("HEALTH %.0f / 100    MATERIALS %d"), Player->Health, Player->Inventory->Count(EFrontierItem::Material)), FLinearColor::White, 32, 30);
    int32 Companions = 0, Working = 0;
    for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
        if (It->Tamer == Player->GetController()) { ++Companions; if (It->Activity == ECreatureActivity::Work) ++Working; }
    DrawText(FString::Printf(TEXT("COMPANIONS %d    WORKING %d"), Companions, Working), FLinearColor::Green, 32, 57);
    DrawText(TEXT("WASD move | Space jump | LMB melee | RMB capture"), FLinearColor::White, 32, 84);
    DrawText(TEXT("F follow | E work | R downed respawn | F5 save | F9 load"), FLinearColor::White, 32, 108);
    DrawText(FString::Printf(TEXT("DEVICES %d    MEDICINE %d"), Player->Inventory->Count(EFrontierItem::CaptureDevice), Player->Inventory->Count(EFrontierItem::Medicine)), FLinearColor::White, 32, 136);
    DrawText(TEXT("1 craft device (3) | 2 craft medicine (5) | H heal"), FLinearColor::White, 32, 162);
    DrawText(TEXT("T rest at base | P treat aimed companion | Esc exit"), FLinearColor::White, 32, 190);
    DrawText(TEXT("Q focus aimed enemy | G companion special (20 stamina)"), FLinearColor::White, 32, 216);
    DrawText(TEXT("B build mode | V cycle facility | X dismantle (50% refund)"), FLinearColor::White,32,242);
    DrawText(FString::Printf(TEXT("FOOD %d | 3 craft food (2 materials) | J feed aimed companion"),Player->Inventory->Count(EFrontierItem::Food)),FLinearColor::White,32,268);
    DrawText(TEXT("Trough: aim + U deposit food | O withdraw (up to 5)"),FLinearColor::White,32,294);
    DrawText(Player->GetCommandScopeText(),FLinearColor::Green,32,320);
    DrawText(TEXT("Tab team | [ / ] browse | Enter select | I journal"),FLinearColor::White,32,342);
    DrawText(Player->Feedback, FLinearColor::Yellow, 32, 368);
    DrawRect(FLinearColor(.02f,.04f,.06f,.88f),18,462,550,104);
    DrawText(Player->GetObjectiveStatus(),FLinearColor::Green,32,474);
    DrawText(Player->GetObjectiveHint(),FLinearColor::White,32,503);
    DrawText(Player->GetObjectiveRewardText(),FLinearColor::Yellow,32,532);
    if(Player->IsMeleePending()) DrawText(TEXT("SPEAR WINDUP - keep aim and stay in reach"),FLinearColor::Yellow,X-150,Y-70);
    DrawRect(FLinearColor(.02f,.04f,.06f,.88f),18,572,550,54);
    DrawText(FString::Printf(TEXT("GEAR: %s | attack %.0f | reach %.1fm | vest %.0f%%"),Player->Equipment->GetState().bSpearEquipped?TEXT("SPEAR"):TEXT("UNARMED"),Player->GetPlayerAttackDamage(),Player->GetPlayerMeleeRange()/100,Player->Equipment->GetDamageReduction()*100),FLinearColor::White,32,578);
    DrawText(FString::Printf(TEXT("4 craft spear (%d) | 5 craft vest (%d) | 6/7 equip toggle"),Player->Equipment->SpearCost,Player->Equipment->VestCost),FLinearColor::White,32,602);
    if(Player->Health<=0) DrawText(TEXT("DOWNED | R: base respawn | F9: load save"),FLinearColor::Red,X-150,Y-100);
    else if(Player->GetRespawnProtectionRemaining()>0) DrawText(FString::Printf(TEXT("RESPAWN SHIELD %.1fs | attacks end it"),Player->GetRespawnProtectionRemaining()),FLinearColor::Green,X-150,Y-100);
    if (Player->Building->IsActive()) DrawText(Player->Building->GetHint(), FLinearColor::Yellow,32,440);
    if (auto* Site = Player->TraceFacility(); Site && Site->RequiredSpecialty == EWorkSpecialty::Feeding)
        DrawText(FString::Printf(TEXT("TROUGH %d / 30 FOOD | U deposit | O withdraw"),Site->StoredFood),FLinearColor::Yellow,X-130,Y+25);
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
            if (Creature->bResumeWorkAfterRest)
                DrawText(TEXT("AUTO REST - resumes at 80 stamina / 20 satiety / 80% HP"),FLinearColor::Yellow,X-130,Y-70);
            if (Creature->FeedingSite.IsValid()) DrawText(TEXT("SEEKING FOOD - assignment will resume"),FLinearColor::Yellow,X-130,Y-50);
            DrawText(FString::Printf(TEXT("XP %d/%d  Stamina %.0f%%  Satiety %.0f%%  %s"), Creature->Experience, Creature->Level*100,
                Creature->Stamina,Creature->Nutrition, *UEnum::GetValueAsString(Creature->Activity)), FLinearColor::Green, X-130, Y+48);
            DrawText(FString::Printf(TEXT("%s | cooldown %.1fs %s"), *Creature->GetSpecialName(), Creature->SpecialCooldownRemaining,
                Creature->SpecialWindupRemaining > 0 ? TEXT("CHARGING") : TEXT("")), FLinearColor::Yellow, X-130, Y+70);
        }
        if (Creature->SlowRemaining > 0 || Creature->GuardRemaining > 0)
            DrawText(FString::Printf(TEXT("SLOW %.1fs  GUARD %.1fs"), Creature->SlowRemaining, Creature->GuardRemaining), FLinearColor::Yellow, X-130, Y+92);
    }
    if (Player->IsJournalOpen())
    {
        const auto& Entries=Player->GetJournal();
        int32 Captured=0; for (const auto& Entry:Entries) if(Entry.bCaptured) ++Captured;
        const float Left=Canvas->ClipX-488;
        DrawRect(FLinearColor(.015f,.025f,.04f,.96f),Left,18,470,440);
        DrawText(FString::Printf(TEXT("FIELD JOURNAL | %d seen | %d captured"),Entries.Num(),Captured),FLinearColor::Green,Left+14,30);
        DrawText(TEXT("I close | PageUp / PageDown browse | NOT paused"),FLinearColor::Yellow,Left+14,54);
        if(Entries.IsEmpty()) DrawText(TEXT("Aim at a living creature within 12m to discover it"),FLinearColor::White,Left+14,104);
        else
        {
            const int32 Index=Player->GetJournalPage()%Entries.Num();
            const auto& Entry=Entries[Index]; const auto* Definition=Entry.Definition.Get();
            if (Definition)
            {
                DrawText(FString::Printf(TEXT("%d / %d  %s"),Index+1,Entries.Num(),*Definition->DisplayName.ToString()),FLinearColor::White,Left+14,100);
                DrawText(Entry.bCaptured?TEXT("CAPTURED - permanent research record"):TEXT("SEEN - not captured yet"),Entry.bCaptured?FLinearColor::Green:FLinearColor::Yellow,Left+14,126);
                if (Entry.bCaptured)
                {
                    DrawText(FString::Printf(TEXT("Base HP %d | attack %d | speed %.1fm/s"),Definition->BaseMaxHealth,Definition->BaseAttack,Definition->MoveSpeed/100),FLinearColor::White,Left+14,166);
                    FString Jobs;
                    for (auto Job:Definition->WorkSpecialties) { FString Name=UEnum::GetValueAsString(Job); Name.RemoveFromStart(TEXT("EWorkSpecialty::")); Jobs+=(Jobs.IsEmpty()?TEXT(""):TEXT(" / "))+Name; }
                    DrawText(TEXT("Work: ")+Jobs,FLinearColor::White,Left+14,192);
                    DrawText(TEXT("Special: ")+Definition->SpecialName.ToString(),FLinearColor::White,Left+14,218);
                    DrawText(FString::Printf(TEXT("Range %.1fm | cooldown %.0fs | cost 20 stamina"),Definition->SpecialRange/100,Definition->SpecialCooldownSeconds),FLinearColor::White,Left+14,244);
                }
                else DrawText(TEXT("Capture to unlock base stats, work and skill notes"),FLinearColor::White,Left+14,166);
                TArray<FString> Words; Definition->Description.ToString().ParseIntoArrayWS(Words);
                FString Line; float DescY=290;
                for (const auto& Word:Words)
                {
                    FString Next=Line.IsEmpty()?Word:Line+TEXT(" ")+Word;
                    float W,H; GetTextSize(Next,W,H);
                    if(W>430 && !Line.IsEmpty()) { DrawText(Line,FLinearColor::White,Left+14,DescY); DescY+=22; Line=Word; }
                    else Line=Next;
                    if(DescY>400) break;
                }
                if(DescY<=400 && !Line.IsEmpty()) DrawText(Line,FLinearColor::White,Left+14,DescY);
                DrawText(TEXT("F5 saves research | F9 restores the saved snapshot"),FLinearColor::Yellow,Left+14,426);
            }
        }
    }
    if (Player->IsRosterOpen())
    {
        const auto Pets = Player->GetCompanionRoster();
        const int32 Highlight = Pets.IndexOfByKey(Player->GetRosterHighlight());
        const int32 Rows = FMath::Clamp(FMath::FloorToInt((Canvas->ClipY-160)/70),1,8);
        const int32 Start = Highlight == INDEX_NONE ? 0 : (Highlight/Rows)*Rows;
        const float Width = FMath::Min(470.f,Canvas->ClipX-36.f), Left = Canvas->ClipX-Width-18;
        DrawRect(FLinearColor(.015f,.025f,.04f,.96f),Left,18,Width,100+Rows*70);
        // Draw every background before text: Canvas may batch font glyphs across rows.
        if (Highlight != INDEX_NONE) DrawRect(FLinearColor(.08f,.22f,.28f,.9f),Left+6,103+(Highlight-Start)*70,Width-12,66);
        DrawText(FString::Printf(TEXT("TEAM  %d companions | %d-%d"),Pets.Num(),Pets.IsEmpty()?0:Start+1,FMath::Min(Start+Rows,Pets.Num())),FLinearColor::White,Left+14,30);
        DrawText(TEXT("[ / ] browse | Enter select | Tab close"),FLinearColor::Yellow,Left+14,52);
        DrawText(TEXT("Live world - NOT paused | selection: 12m + sight"),FLinearColor::White,Left+14,73);
        if (Pets.IsEmpty()) DrawText(TEXT("No companions yet - capture a wild creature"),FLinearColor::Green,Left+14,110);
        for (int32 I=Start; I<FMath::Min(Start+Rows,Pets.Num()); ++I)
        {
            const auto* Pet = Pets[I]; const float RowY = 103+(I-Start)*70;
            const auto Color = Pet->Health <= 0 ? FLinearColor(1,.35f,.3f) : FLinearColor::White;
            DrawText(FString::Printf(TEXT("%s %s Lv%d | %.1fm %s"),I==Highlight?TEXT(">>"):TEXT("  "),*Pet->GetCreatureName(),Pet->Level,Player->GetDistanceTo(Pet)/100,
                Player->IsCommandTarget(Pet)?TEXT("[COMMAND]"):TEXT("")),Color,Left+14,RowY+3);
            DrawText(FString::Printf(TEXT("HP %.0f/%.0f | stamina %.0f | food %.0f"),Pet->Health,Pet->GetMaxHealth(),Pet->Stamina,Pet->Nutrition),Color,Left+14,RowY+23);
            FString Status = UEnum::GetValueAsString(Pet->Activity); Status.RemoveFromStart(TEXT("ECreatureActivity::"));
            if (Pet->Health <= 0) Status = TEXT("DOWNED - aim + P to treat");
            else if (Pet->FeedingSite.IsValid()) Status = TEXT("Seeking food");
            else if (Pet->bResumeWorkAfterRest) Status = TEXT("Auto rest / work pending");
            if (Pet->WorkSite) { FString Job=UEnum::GetValueAsString(Pet->WorkSite->RequiredSpecialty); Job.RemoveFromStart(TEXT("EWorkSpecialty::")); Status+=TEXT(" | ")+Job; }
            DrawText(Status,FLinearColor::Green,Left+14,RowY+43);
        }
    }
}
