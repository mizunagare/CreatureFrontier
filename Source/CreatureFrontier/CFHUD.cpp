#include "CFHUD.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "CreatureAffinity.h"
#include "PlayerEquipmentComponent.h"
#include "CaptureComponent.h"
#include "InventoryComponent.h"
#include "BaseBuildingComponent.h"
#include "WildEncounter.h"
#include "WorkStation.h"
#include "ResourceNode.h"
#include "ResearchComponent.h"
#include "Engine/Canvas.h"
#include "EngineUtils.h"

void ACFHUD::DrawHUD()
{
    Super::DrawHUD();
    auto* Player = Cast<ACFPlayerCharacter>(GetOwningPawn());
    if (!Canvas || !Player) return;
    if(Player->IsPauseMenuOpen()) { DrawPauseMenu(Player); return; }
    if(!Player->IsBackpackOpen() && !Player->IsHelpVisible() && !Player->IsJournalOpen() && !Player->IsRosterOpen())
    {
        const float Left=FMath::Max(530.f,Canvas->ClipX-620);
        DrawRect(FLinearColor(.02f,.035f,.06f,.88f),Left,18,600,72);
        DrawWrappedText(Player->Research->GetHint(),FLinearColor(.7f,.85f,1),Left+12,30,576,2);
    }
    const float X = Canvas->ClipX * 0.5f, Y = Canvas->ClipY * 0.5f;
    DrawLine(X-7, Y, X+7, Y, FLinearColor::White);
    DrawLine(X, Y-7, X, Y+7, FLinearColor::White);
    DrawRect(FLinearColor(0.02f,0.03f,0.05f,0.85f), 18, 18, 500, 180);
    DrawText(FString::Printf(TEXT("HEALTH %.0f / 100    MATERIALS %d"), Player->Health, Player->Inventory->Count(EFrontierItem::Material)), FLinearColor::White, 32, 30);
    int32 Companions = 0, Working = 0;
    for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
        if (It->Tamer == Player->GetController()) { ++Companions; if (It->Activity == ECreatureActivity::Work) ++Working; }
    DrawText(FString::Printf(TEXT("DEVICES %d | MEDICINE %d | FOOD %d"),Player->Inventory->Count(EFrontierItem::CaptureDevice),Player->Inventory->Count(EFrontierItem::Medicine),Player->Inventory->Count(EFrontierItem::Food)),FLinearColor::White,32,54);
    DrawText(FString::Printf(TEXT("COMPANIONS %d | WORKING %d"),Companions,Working),FLinearColor::Green,32,78);
    DrawText(Player->GetCommandScopeText(),FLinearColor::Green,32,102);
    DrawText(FString::Printf(TEXT("RMB %s | resonance stock %d | 0 switch"),Player->CaptureComponent->UsesResonance()?TEXT("RESONANCE"):TEXT("NORMAL"),Player->Inventory->Count(EFrontierItem::ResonanceDevice)),FLinearColor::Yellow,32,174);
    DrawText(FString::Printf(TEXT("%s | attack %.0f | reach %.1fm | vest %.0f%%"),Player->Equipment->GetState().bSpearEquipped?TEXT("SPEAR"):TEXT("UNARMED"),Player->GetPlayerAttackDamage(),Player->GetPlayerMeleeRange()/100,Player->Equipment->GetDamageReduction()*100),FLinearColor::White,32,126);
    DrawText(TEXT("C backpack | F1 controls | Esc pause | Tab team | I journal"),FLinearColor::White,32,150);
    DrawRect(FLinearColor(.02f,.04f,.06f,.88f),18,208,600,52);
    DrawWrappedText(Player->Feedback,FLinearColor::Yellow,32,218,570,2);
    const float JourneyY=Canvas->ClipY-200;
    DrawRect(FLinearColor(.02f,.04f,.06f,.88f),18,JourneyY,550,112);
    DrawText(Player->GetObjectiveStatus(),FLinearColor::Green,32,JourneyY+12);
    DrawText(Player->GetObjectiveHint(),FLinearColor::White,32,JourneyY+40);
    DrawText(Player->GetObjectiveRewardText(),FLinearColor::Yellow,32,JourneyY+68);
    if(Player->IsMeleePending()) DrawText(TEXT("SPEAR WINDUP - keep aim and stay in reach"),FLinearColor::Yellow,X-150,Y-70);
    if(Player->Health<=0) DrawText(TEXT("DOWNED | R: base respawn | F9: load save"),FLinearColor::Red,X-150,Y-100);
    else if(Player->GetRespawnProtectionRemaining()>0) DrawText(FString::Printf(TEXT("RESPAWN SHIELD %.1fs | attacks end it"),Player->GetRespawnProtectionRemaining()),FLinearColor::Green,X-150,Y-100);
    if (Player->Building->IsActive())
    {
        DrawRect(FLinearColor(.02f,.04f,.06f,.9f),18,JourneyY-68,550,56);
        DrawWrappedText(Player->Building->GetHint(),FLinearColor::Yellow,32,JourneyY-56,520,2);
    }
    if (auto* Site = Player->TraceFacility(); Site && Site->IsFoodStore())
        DrawText(FString::Printf(TEXT("FOOD STORE %d / 30 | U deposit | O withdraw"),Site->StoredFood),FLinearColor::Yellow,X-130,Y+25);
    if(auto* Node=Player->TraceResource()) DrawText(Node->GetStatus(),FLinearColor::Yellow,X-160,Y+25);
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
            const FString ResourceStatus=Creature->GetGatheringStatus();
            if(!ResourceStatus.IsEmpty()) DrawText(ResourceStatus,FLinearColor::Yellow,X-130,Y-95);
            DrawText(FString::Printf(TEXT("BOND %d/100 %s | Y pet (2.5m) | wait %.0fs"),Creature->BondPoints,*Creature->GetBondLabel(),FMath::CeilToFloat(Creature->PettingCooldown)),FLinearColor::Green,X-30,Y+165);
            if(Creature->CarriedFood>0 || (Creature->WorkSite && Creature->WorkSite->RequiredSpecialty==EWorkSpecialty::Transport))
                DrawText(FString::Printf(TEXT("TRANSPORT cargo %d/3 food | Z recover within 6m"),Creature->CarriedFood),FLinearColor::Yellow,X-130,Y-125);
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
        DrawText(Creature->Disposition==ECreatureDisposition::Tamed?CreatureAffinity::Describe(Creature->Definition):Player->GetGroupMatchupText(Creature),FLinearColor(.7f,.9f,1),X-130,Y+140);
    }
    if(Player->IsHelpVisible()) DrawControlHelp(Player);
    if(Player->IsBackpackOpen()) DrawBackpack(Player);
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
                DrawText(CreatureAffinity::Describe(Definition),FLinearColor(.7f,.9f,1),Left+14,270);
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
            if(Pet->CarriedFood>0) Status+=FString::Printf(TEXT(" | cargo %d"),Pet->CarriedFood);
            DrawText(Status,FLinearColor::Green,Left+14,RowY+43);
        }
    }
}
